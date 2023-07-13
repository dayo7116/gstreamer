#include "SoupServer.h"

#include <gst/gst.h>
#include <gobject\gsignal.h>

#include <libsoup/soup.h>

#include <algorithm>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <map>

#define VIDEO_PORT 8088
#define AUDIO_PORT 8089


class CThread {
public:
  CThread() : m_pThread(NULL) {}
  virtual ~CThread() { Join(); }
  virtual bool Init() { return true; }
  virtual void Run() = 0;
  void Start() {
    if (Init()) {
      m_pThread = new std::thread(&CThread::Run, this);
    }
  }
  void Join() {
    if (m_pThread) {
      m_pThread->join();
      delete m_pThread;
      m_pThread = NULL;
    }
  }

private:
  std::thread* m_pThread;
};

class DataSender {
public:
  virtual void OnSendingData(SoupWebsocketConnection* connection) = 0;
};


std::string GetTime() {
  // 获取当前时间点
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

  // 将时间戳转换为字符串
  std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
  std::tm* timeinfo = std::localtime(&timestamp);
  char buffer[80];
  std::strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", timeinfo);

  // 创建文件名
  std::ostringstream timeStr;
  timeStr << buffer << "_" << ms;
  return timeStr.str();
}

std::mutex g_lock_alive;
int g_alive_connection_count = 0;
class CustomSoupServer : public CThread {
public:
  CustomSoupServer(int port, const char* server_name, const char* path)
    : m_port(port), m_name(server_name), m_path(path)
  {}

  bool Init() override;
  void Run() override;

  void SetSender(std::shared_ptr<DataSender> sender) {
    m_sender = sender;
  }

  static void soup_websocket_closed_cb(SoupWebsocketConnection* connection, gpointer user_data) {
    CustomSoupServer* server = (CustomSoupServer*)user_data;
    if (server) {
      server->OnClientClosed(connection);
    }
  }

  static void soup_websocket_error_cb(SoupWebsocketConnection* connection,
                                      GError* error, gpointer user_data) {
    CustomSoupServer* server = (CustomSoupServer*)user_data;
    if (server) {
      server->OnClientError(connection, error);
    }
  }

  static void soup_websocket_pong_cb(SoupWebsocketConnection* connection,
                                     GBytes* message, gpointer user_data) {
    CustomSoupServer* server = (CustomSoupServer*)user_data;
    if (server) {
      server->OnClientPong(connection, message);
    }
  }

  static void soup_websocket_message_cb(G_GNUC_UNUSED SoupWebsocketConnection* connection,
    SoupWebsocketDataType data_type, GBytes* message, gpointer user_data) {
    CustomSoupServer* server = (CustomSoupServer*)user_data;
    if (server) {
      server->OnMessage(connection, data_type, message);
    }
  }

  static void soup_websocket_handler(SoupServer* server, SoupWebsocketConnection* connection, const char* path, SoupClientContext* client, gpointer user_data) {
    {
      std::lock_guard<std::mutex> auto_lock(g_lock_alive);
      g_alive_connection_count++;
    }
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnClientConnected(server, connection, path, client);
    }
  }

  static gboolean send_frame_data_async(gpointer user_data) {
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnSendingData();
    }

    return G_SOURCE_REMOVE;
  }

  static void soup_server_request_abort(SoupServer* server, SoupMessage* message,
    SoupClientContext* client,
    gpointer user_data) {
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnRequestAbort(server, message, client);
    }
  }

  static void soup_server_request_finished(SoupServer* server, SoupMessage* message,
    SoupClientContext* client,
    gpointer user_data) {
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnRequestFinish(server, message, client);
    }
  }
  static void soup_server_request_read(SoupServer* server,
    SoupMessage* message,
    SoupClientContext* client,
    gpointer user_data) {
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnRequestRead(server, message, client);
    }
  }
  static void soup_server_request_started(SoupServer* server, SoupMessage* message,
    SoupClientContext* client,
    gpointer user_data) {
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnRequestStarted(server, message, client);
    }
  }


protected:
  void OnRequestAbort(SoupServer* server, SoupMessage* message,
    SoupClientContext* client) {
   printf("XR-Server server:%s request-aborted client:%p at %s \n", m_name.c_str(), client, GetTime().c_str());
  }
  void OnRequestFinish(SoupServer* server, SoupMessage* message,
                SoupClientContext* client) {
    printf("XR-Server server:%s request-finished client:%p at %s \n", m_name.c_str(), client, GetTime().c_str());

  }
  void OnRequestRead(SoupServer* server, SoupMessage* message,
                     SoupClientContext* client) {
    //printf("XR-Server server:%s request-read client:%p at %s \n", m_name.c_str(), client, GetTime().c_str());
  }
  void OnRequestStarted(SoupServer* server, SoupMessage* message,
    SoupClientContext* client) {
    //printf("XR-Server server:%s request-started client:%p at %s \n", m_name.c_str(), client, GetTime().c_str());
  }
  void OnClientConnected(SoupServer* server, SoupWebsocketConnection* connection,
    const char* path, SoupClientContext* client) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count();

    printf("XR-Server server:%s-%d get connection:%p, client:%p at %s \n",
           m_name.c_str(), ++ m_connection_count, connection,
           client, GetTime().c_str());

    g_signal_connect(G_OBJECT(connection), "closed",
      G_CALLBACK(soup_websocket_closed_cb), this);

    {
      std::lock_guard<std::mutex> auto_lock(m_connection_lock);
      auto finder = m_alive_connections.find(connection);
      if (finder == m_alive_connections.end()) {
        g_object_ref(G_OBJECT(connection));
        m_alive_connections[connection].borntime = ms;
        m_alive_connections[connection].connection_index = m_connection_count;
      } else {
        // finder->second.borntime = ms;
        int a = 0;
      }
    }

    g_signal_connect(G_OBJECT(connection), "message",
      G_CALLBACK(soup_websocket_message_cb), this);

    g_signal_connect(connection, "error", G_CALLBACK(soup_websocket_error_cb),
                     this);
    g_signal_connect(connection, "pong", G_CALLBACK(soup_websocket_pong_cb),
                     this);

    m_sending_loop = true;
    if (!m_send_data_thread) {
      m_send_data_thread =
        std::make_shared<std::thread>([this]() {

          while (m_sending_loop) {
            g_main_context_invoke(m_context, (GSourceFunc)send_frame_data_async, this);
            g_usleep(500);
          }
        });
    }

  }
  void OnMessage(G_GNUC_UNUSED SoupWebsocketConnection* connection,
    SoupWebsocketDataType data_type, GBytes* message) {

    //printf("XR-Server %s connection:%p get msg \n", m_name.c_str(), connection);

    const gchar* data = nullptr;
    gsize size;

    switch (data_type) {
    case SOUP_WEBSOCKET_DATA_BINARY:
      g_error("Received unknown binary message, ignoring\n");
      break;

    case SOUP_WEBSOCKET_DATA_TEXT:
      data = (const gchar*)g_bytes_get_data(message, &size);
      gchar* data_string = g_strndup(data, size);
      g_free(data_string);
      break;
    }
  }

  void OnClientError(SoupWebsocketConnection* connection, GError* error) {
    gchar* message = g_strdup(g_strerror(error->code));
    printf("XR-Server %s connection:%p error code:%d, reason:%s:%s \n",
           m_name.c_str(), connection, error->code, message,
           error->message);
    g_free(message);
  }
  void OnClientPong(SoupWebsocketConnection* connection, GBytes* message) {
    gsize size = 0;
    const gchar* data = (const gchar*)g_bytes_get_data(message, &size);
    printf(
        "XR-Server %s connection:%p pong %s \n", m_name.c_str(), connection, data);
  }

  void OnClientClosed(SoupWebsocketConnection* connection) {
    int alive_count = 0, wrong_alive_count = 0;
    {
      std::lock_guard<std::mutex> auto_lock(m_connection_lock);
      alive_count = m_alive_connections.size() - 1;
    }
    {
      std::lock_guard<std::mutex> auto_lock(g_lock_alive);
      g_alive_connection_count--;
      wrong_alive_count = g_alive_connection_count;
    }
    printf("XR-Server %s connection:%p closed at %s, alive connection:%d(wrong:%d) \n", m_name.c_str(), connection, GetTime().c_str(), alive_count,
        wrong_alive_count);

    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch())
                  .count();
    {
      std::lock_guard<std::mutex> auto_lock(m_connection_lock);
      auto finder = m_alive_connections.find(connection);
      if (finder != m_alive_connections.end()) {
        m_alive_connections.erase(finder);
        g_object_unref(G_OBJECT(connection));
      } else {
        printf("XR-Server %s connection:%p not in alive records !!!!!!!!!!!!! \n", m_name.c_str(), connection);
      }

      for (auto iter = m_alive_connections.begin(); iter != m_alive_connections.end(); iter++) {
        SoupWebsocketConnection* conn = iter->first;
        SoupWebsocketState state = soup_websocket_connection_get_state(conn);
        if (ms - iter->second.borntime > 10000) {
          printf("XR-Server %s connection:%p has actived for 10s, state:%d, connected at %d !!!!!!!!!!! \n", m_name.c_str(), conn, state, iter->second.connection_index);
        }
      }
    }

    m_sending_loop = false;
    if (m_send_data_thread) {
      m_send_data_thread->join();
      m_send_data_thread = nullptr;
    }
  }

  void OnSendingData() {
    std::lock_guard<std::mutex> auto_lock(m_connection_lock);
    if (m_alive_connections.empty()) {
      return;
    }
    SoupWebsocketConnection*  connection = m_alive_connections.begin()->first;
    SoupWebsocketState state = soup_websocket_connection_get_state(connection);
    if (SOUP_WEBSOCKET_STATE_OPEN != state) {
      return;
    }
    if (m_sender) {
      m_sender->OnSendingData(connection);
    }
  }
  
protected:
  GMainLoop* m_loop = NULL;
  GMainContext* m_context = nullptr;

  SoupServer* m_server = NULL;
  std::mutex m_connection_lock;
  struct LifeTime {
    uint64_t borntime;
    int connection_index;
  };
  std::map<SoupWebsocketConnection*, LifeTime> m_alive_connections;

  int m_port = 0;
  std::string m_name;
  std::string m_path;

  bool m_sending_loop = false;
  std::shared_ptr<std::thread> m_send_data_thread;

  std::shared_ptr<DataSender> m_sender;

  int m_connection_count = 0;
};

bool CustomSoupServer::Init() {

  int argc = 0;
  gst_init(&argc, NULL);

  m_context = g_main_context_default();
  m_loop = g_main_loop_new(m_context, false);

  m_server = soup_server_new(SOUP_SERVER_SERVER_HEADER, m_name.c_str(), NULL);

  g_signal_connect(m_server, "request-aborted", G_CALLBACK(soup_server_request_abort),
                   this);
  g_signal_connect(m_server, "request-finished",
                   G_CALLBACK(soup_server_request_finished),
                   this);
  g_signal_connect(m_server, "request-read",
                   G_CALLBACK(soup_server_request_read),
                   this);
  g_signal_connect(m_server, "request-started",
                   G_CALLBACK(soup_server_request_started), this);

  // Set up the request handler
  soup_server_add_websocket_handler(m_server, m_path.c_str(), NULL, NULL,
    soup_websocket_handler, this, NULL);

  // Set the port to listen on
  soup_server_listen_all(m_server, m_port, SOUP_SERVER_LISTEN_IPV4_ONLY, NULL);

  printf("XR-Server %s,%p-%d starts: \n", m_name.c_str(), m_server, m_connection_count);
  return true;
}
void CustomSoupServer::Run() {
  if (nullptr == m_loop) {
    return;
  }

  g_main_loop_run(m_loop);

  g_object_unref(m_server);
  g_object_unref(m_loop);
  m_server = NULL;
  m_loop = NULL;
}

int g_video_send_count = 0;
class VideoDataSender : public DataSender {
public:
  void OnSendingData(SoupWebsocketConnection* connection) override {
   int videosz = 1024 * 10;
    if (++g_video_send_count % 10 == 0) {
     videosz = 1024 * 400;
    }
    std::vector<uint8_t> frame_data;
   frame_data.resize(videosz);

    // 发送大的数据包
    const size_t chunkSize = 1024 * 8;  // 设置数据块的大小
    const char* binaryData =
      (const char*)frame_data.data();  // 假设这里是您要发送的大型二进制数据
    size_t binaryDataSize =
      frame_data.size();  // 假设这里是您要发送的大型二进制数据的大小

  // 将大型二进制数据拆分成多个较小的数据块并逐一发送
    for (size_t i = 0; i < binaryDataSize; i += chunkSize) {
      size_t chunkSizeActual = std::min(chunkSize, binaryDataSize - i);
      const char* chunk = binaryData + i;

      // 发送数据块
      SoupWebsocketState state = soup_websocket_connection_get_state(connection);
      if (SOUP_WEBSOCKET_STATE_OPEN != state) {
        break;
      }
      soup_websocket_connection_send_binary(connection, chunk, chunkSizeActual);
    }

    SoupWebsocketState state = soup_websocket_connection_get_state(connection);
    if (SOUP_WEBSOCKET_STATE_OPEN != state) {
      return;
    }
    std::string video_str("video end");
    soup_websocket_connection_send_text(connection, video_str.c_str());

    //printf("XR-Server Video data sended \n");
  }
};

class AudioDataSender : public DataSender {
public:
  void OnSendingData(SoupWebsocketConnection* connection) override {
    std::vector<uint8_t> data;
    data.resize(200);
    for (int i = 0; i < 200; i++) {
      data[i] = i;
    }
    SoupWebsocketState state = soup_websocket_connection_get_state(connection);
    if (SOUP_WEBSOCKET_STATE_OPEN != state) {
      return;
    }
    soup_websocket_connection_send_binary(connection, data.data(), data.size());

    //printf("XR-Server Audio data sended \n");
  }
};

CustomSoupServer g_video_server(VIDEO_PORT, "Video", "/video");
CustomSoupServer g_audio_server(AUDIO_PORT, "Audio", "/audio");

std::shared_ptr<VideoDataSender> g_video_sender;
std::shared_ptr<AudioDataSender> g_audio_sender;

void start_soup_server() {
  g_video_sender = std::make_shared<VideoDataSender>();
  g_audio_sender = std::make_shared<AudioDataSender>();
  std::thread video_thread([]() {
    g_video_server.Start();
    g_video_server.SetSender(g_video_sender);
   });
  std::thread audio_thread([]() {
    g_audio_server.Start();
    g_audio_server.SetSender(g_audio_sender);
   });
  while (true)
  {
    g_usleep(1000);
  }
}
