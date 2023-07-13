//
// Created by dayong.li on 2023/7/13.
//

#include "MTXR_SocketClient.h"

#include <string>
#include <mutex>
#include <thread>
#include <iostream>
#include <sstream>
#include <iomanip>

#if defined(ANDROID)
#include <android/log.h>
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "hello_xr", __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, "hello_xr", __VA_ARGS__)
#endif
namespace Log {

  enum class Level {
    Verbose, Info, Warning, Error
  };

  Level g_minSeverity{Level::Verbose};
  std::mutex g_logLock;

  void SetLevel(Level minSeverity) { g_minSeverity = minSeverity; }

  void Write(Level severity, const std::string &msg) {
    if (severity < g_minSeverity) {
      return;
    }

    const auto now = std::chrono::system_clock::now();
    const time_t now_time = std::chrono::system_clock::to_time_t(now);
    tm now_tm;
#ifdef _WIN32
    localtime_s(&now_tm, &now_time);
#else
    localtime_r(&now_time, &now_tm);
#endif
    // time_t only has second precision. Use the rounding error to get sub-second precision.
    const auto secondRemainder =
        now - std::chrono::system_clock::from_time_t(now_time);
    const int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        secondRemainder).count();

    std::ostringstream out;
    out.fill('0');
    out << "[" << std::setw(2) << now_tm.tm_hour << ":" << std::setw(2)
        << now_tm.tm_min << ":" << std::setw(2) << now_tm.tm_sec
        << "." << std::setw(3) << milliseconds << "]" << msg << std::endl;

    std::lock_guard<std::mutex> lock(g_logLock);  // Ensure output is serialized
    ((severity == Level::Error) ? std::clog : std::cout) << out.str();

#if defined(ANDROID)
    if (severity == Level::Error)
      ALOGE("%s", out.str().c_str());
    else
      ALOGV("%s", out.str().c_str());
#endif
  }
}  // namespace Log

inline std::string Fmt(const char *fmt, ...) {
  va_list vl;
  va_start(vl, fmt);
  int size = std::vsnprintf(nullptr, 0, fmt, vl);
  va_end(vl);

  if (size != -1) {
    std::unique_ptr<char[]> buffer(new char[size + 1]);

    va_start(vl, fmt);
    size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
    va_end(vl);
    if (size != -1) {
      return std::string(buffer.get(), size);
    }
  }
  return "";
}

void XRSocketClient::Start(const char *server_ip, std::shared_ptr<SocketMessageListener> processor) {
  Log::Write(Log::Level::Info, Fmt("XR-Socket %s starts with server:%s", m_name.c_str(), server_ip ? server_ip : ""));

  if (server_ip) {
    m_server_ip = server_ip;
  }
  m_data_processor = processor;
  m_receive_thread = std::make_shared<std::thread>([this]() {
    std::string threadName = m_name + "ClientXRReceivingLoop";
    pthread_setname_np(pthread_self(), threadName.c_str());

    RunLoop();
  });

  m_sending_loop_running = true;
  m_sending_thread = std::make_shared<std::thread>([this] {
    std::string threadName = m_name + "ClientXRSendingLoop";
    pthread_setname_np(pthread_self(), threadName.c_str());

    while (m_sending_loop_running) {
      {
        std::unique_lock<std::mutex> lock(m_msg_lock);
        m_msg_cv.wait(lock);
      }

      {
        std::lock_guard<std::mutex> auto_lock(m_resource_lock);
        // 发送pose线程
        if(m_context) {
          g_main_context_invoke(m_context, (GSourceFunc) SendDataAsyncFn, this);
        }
      }
    }
    Log::Write(Log::Level::Info, Fmt("XR-Socket %s sending thread stops", m_name.c_str()));
  });
}

void XRSocketClient::RunLoop() {
  GMainContext *context = g_main_context_new();

  Log::Write(Log::Level::Info, Fmt("XR-Socket %s starts to connect server async with context:%p", m_name.c_str(), context));

  g_main_context_invoke(context, (GSourceFunc) AsyncConnectFn, this);
  g_main_context_push_thread_default(context);

  GMainLoop *loop = g_main_loop_new(context, FALSE);
  m_receive_loop = loop;

  m_context = context;

  //m_receive_loop 还没赋值就Quit,会导致线程退不出去
  //还没执行g_main_loop_run, Quit里执行了g_main_loop_quit，也导致线程退不出去
  g_main_loop_run(loop);

  {
    std::lock_guard<std::mutex> auto_lock(m_resource_lock);
    m_context = NULL;
  }
  m_receive_loop = NULL;

  //TODO: 还未建立connection就Quit, 如何清理已经异步建立的connection？
  if (m_connection) {
    SoupWebsocketState state = soup_websocket_connection_get_state(m_connection);
    if (SOUP_WEBSOCKET_STATE_OPEN == state)
    {
      Log::Write(Log::Level::Info, Fmt("XR-Socket-Connection %s close %p", m_name.c_str(), m_connection));
      soup_websocket_connection_close(m_connection, 1000, "");
    } else {
      int a = 0;
    }
    g_object_unref(m_connection);
    m_connection = NULL;
  } else {
    int a = 0;
  }
  if (m_cancellable) {
    g_object_unref(m_cancellable);
    m_cancellable = NULL;
  }
  if (m_soup_session) {
    soup_session_abort(m_soup_session);
    g_object_unref(m_soup_session);
    m_soup_session = NULL;
  }
  if (m_soup_message) {
    g_object_unref(m_soup_message);
    m_soup_message = NULL;
  }
  if (m_logger) {
    g_object_unref(m_logger);
    m_logger = NULL;
  }

  g_main_context_pop_thread_default(context);

  g_main_loop_unref(loop);
  g_main_context_unref(context);

  Log::Write(Log::Level::Info, Fmt("XR-Socket %s main loop stops", m_name.c_str()));
  m_receive_loop_running = false;
}

void XRSocketClient::Quit() {
  //没有m_context和m_receive_loop时怎么办？
  {
    std::lock_guard<std::mutex> auto_lock(m_resource_lock);
    if (m_context) {
      Log::Write(Log::Level::Info, Fmt("XR-Socket %s starts async quitting", m_name.c_str()));
      g_main_context_invoke(m_context, (GSourceFunc) AsyncQuitFn, this);
    } else {
      Log::Write(Log::Level::Info, Fmt("XR-Socket %s fail to start async quitting", m_name.c_str()));
    }
  }

  m_sending_loop_running = false;
  m_msg_cv.notify_one();
  if (m_sending_thread) {
    m_sending_thread->join();
    m_sending_thread = nullptr;
  }

  while(m_receive_loop_running) {
    Log::Write(Log::Level::Info, Fmt("XR-Socket %s waiting loop to end", m_name.c_str()));
  }
  if (m_receive_thread) {
    m_receive_thread->join();
    m_receive_thread = nullptr;
  }
}

gboolean XRSocketClient::AsyncQuitFn(gpointer user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->OnQuitting();
  }
  return G_SOURCE_REMOVE;
}
void XRSocketClient::OnQuitting() {
  Log::Write(Log::Level::Info, Fmt("XR-Socket %s quitting loop:%p", m_name.c_str(), m_receive_loop));
  if (!m_connection && m_cancellable) {
    g_cancellable_cancel(m_cancellable);
  }
  if (m_receive_loop) {
    g_main_loop_quit(m_receive_loop);
  }
}

gboolean XRSocketClient::AsyncConnectFn(gpointer user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->ConnectServer();
  }
  return G_SOURCE_REMOVE;
}

void XRSocketClient::ConnectServer() {
  const char *https_aliases[] = {"wss", NULL};

  SoupSession *soup_session = soup_session_new_with_options(
      SOUP_SESSION_SSL_STRICT, TRUE,
      SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);
  // Set up SoupLogger
  SoupLogger *logger = soup_logger_new(SOUP_LOGGER_LOG_BODY, -1);
//  soup_logger_set_printer(logger, log_callback, NULL, NULL);
  soup_session_add_feature(soup_session, SOUP_SESSION_FEATURE(logger));

  gchar *server_url = g_strdup(m_server_ip.c_str());
  SoupMessage *soup_message = soup_message_new(SOUP_METHOD_GET, server_url);
  g_free(server_url);

  Log::Write(Log::Level::Info, Fmt("XR-Socket %s connecting Server %s with session:%p, msg:%p", m_name.c_str(), server_url, soup_session, soup_message));
  // Once connected, we will register
  m_cancellable = g_cancellable_new();
  soup_session_websocket_connect_async(soup_session, soup_message, NULL,
                                       NULL, m_cancellable,
                                       (GAsyncReadyCallback) ServerConnectedCallback,
                                       this);
  m_soup_session = soup_session;
  m_logger = logger;
  m_soup_message = soup_message;
}

gboolean XRSocketClient::ServerConnectedCallback(SoupSession *session,
                                                 GAsyncResult *res,
                                                 void *user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    return client->OnServerConnected(session, res);
  }
  return FALSE;
}

bool
XRSocketClient::OnServerConnected(SoupSession *session, GAsyncResult *res) {
  GError *error = NULL;

  // 创建连接对像
  SoupWebsocketConnection *connection = soup_session_websocket_connect_finish(session, res, &error);
  if (error) {
    Log::Write(Log::Level::Error, Fmt("XR-Socket %s fails to connected code:%d, resaon:%s", m_name.c_str(), error->code, error->message));
    g_error_free(error);
    if (m_receive_loop) {
      g_main_loop_quit(m_receive_loop);
    }
    return false;
  }
  Log::Write(Log::Level::Info, Fmt("XR-Socket-Connection %s gets %p connected", m_name.c_str(), connection));
  soup_websocket_connection_set_max_incoming_payload_size(connection,
                                                          16 * 1024 * 1024);
  // 心跳时间
  soup_websocket_connection_set_keepalive_interval(connection, 1);

  g_signal_connect (connection, "message",
                    G_CALLBACK(ServerMessageCallback), this);
  g_signal_connect (connection, "closed", G_CALLBACK(ServerClosedCallback),
                    this);
  g_signal_connect (connection, "error", G_CALLBACK(ConnectionErrorCallback),
                    this);

  if (m_cancellable) {
    g_object_unref(m_cancellable);
    m_cancellable = NULL;
  }
  if (m_connection) {
    Log::Write(Log::Level::Info, Fmt("XR-Socket-Connection %s close %p", m_name.c_str(), m_connection));
    soup_websocket_connection_close(m_connection, 1000, "");
    g_object_unref(m_connection);
  }
  m_connection = connection;
  m_name = m_name + "-" + std::to_string(GetConnectionID());
  return true;
}

void XRSocketClient::ServerClosedCallback(SoupWebsocketConnection *conn,
                                          void *user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->OnServerClosed(conn);
  }
}

void XRSocketClient::OnServerClosed(SoupWebsocketConnection *connection) {
  SoupWebsocketState state = soup_websocket_connection_get_state(connection);

  gushort close_code = soup_websocket_connection_get_close_code(connection);
  const char *close_reason = soup_websocket_connection_get_close_data(connection);
  SoupWebsocketCloseCode closeCode = static_cast<SoupWebsocketCloseCode>(close_code);
  Log::Write(Log::Level::Info, Fmt("XR-Socket-Connection %s gets %p closed, state:%d, code:%d:%d, reason:%s", m_name.c_str(), m_connection, state, close_code, closeCode, close_reason ? close_reason : ""));
  if (m_receive_loop) {
    g_main_loop_quit(m_receive_loop);
  }

  // 如果连接已经被关闭，就尝试重新建立连接
  if (state == SOUP_WEBSOCKET_STATE_CLOSED) {
    //TODO: @dayong 添加重连策略
  }
}

void XRSocketClient::ConnectionErrorCallback (SoupWebsocketConnection *connection, GError *error, gpointer user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->OnConnectionError(connection, error);
  }
}
void XRSocketClient::OnConnectionError(SoupWebsocketConnection *connection, GError *error){
  gchar* msg = "";
  if (error && error->message) {
    msg = error->message;
  }
  Log::Write(Log::Level::Info, Fmt("XR-Socket-Connection %s gets %p error, code:%d, msg:%s", m_name.c_str(), connection, error ? error->code : -1, msg));
}

void XRSocketClient::ServerMessageCallback(SoupWebsocketConnection *conn,
                                           SoupWebsocketDataType type,
                                           GBytes *message, void *user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->OnServerMessage(conn, type, message);
  }
}

void XRSocketClient::OnServerMessage(SoupWebsocketConnection *connection,
                                     SoupWebsocketDataType type,
                                     GBytes *message) {
  std::shared_ptr<SocketMessageListener> processor = m_data_processor.lock();
  if (processor) {
    gsize size;
    gchar *data = (gchar *) g_bytes_unref_to_data(message, &size);
    processor->OnMessage(connection, type, data, size);
    g_free(data);
  }
}

gboolean XRSocketClient::SendDataAsyncFn(void *user_data) {
  XRSocketClient *client = (XRSocketClient *) user_data;
  if (client) {
    client->OnSendingData();
  }
  return G_SOURCE_REMOVE;
}

void XRSocketClient::OnSendingData() {
  std::shared_ptr<ClientMessageObject> message;
  {
    std::lock_guard<std::mutex> auto_lock(m_msg_lock);
    message = m_current_message;
    m_current_message = nullptr;
  }
  if (!message) {
    return;
  }
  SoupWebsocketDataType type = message->GetType();
  if (SOUP_WEBSOCKET_DATA_TEXT == type) {
//      Log::Write(Log::Level::Verbose, Fmt("XR-Socket %s sends text with connection:%p", m_name.c_str(), m_connection));
    if (NULL != m_connection && SOUP_WEBSOCKET_STATE_OPEN == soup_websocket_connection_get_state(m_connection)) {
      soup_websocket_connection_send_text(m_connection, message->ToString().c_str());
    }
  } else if (SOUP_WEBSOCKET_DATA_BINARY == type) {
    std::unique_ptr<std::vector<unsigned char> > binary_data = message->ToBinary();
    if (!binary_data) {
      return;
    }
    Log::Write(Log::Level::Verbose, Fmt("XR-Socket %s sends binary with connection:%p", m_name.c_str(), m_connection));
    if (NULL != m_connection && SOUP_WEBSOCKET_STATE_OPEN == soup_websocket_connection_get_state(m_connection)) {
      soup_websocket_connection_send_binary(m_connection, binary_data->data(),
                                            binary_data->size());
    }
  }
}
