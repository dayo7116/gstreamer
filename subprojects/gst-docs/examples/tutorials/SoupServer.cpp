#include "SoupServer.h"

#include <gst/gst.h>
#include <gobject\gsignal.h>

#include <libsoup/soup.h>


#include <thread>
#include <mutex>
#include <string>

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

static int g_close_count = 0;

class CustomSoupServer : public CThread {
public:
  CustomSoupServer(int port, const char* server_name, const char* path)
    : m_port(port), m_name(server_name), m_path(path)
  {}

  bool Init() override;
  void Run() override;

  static void soup_websocket_closed_cb(SoupWebsocketConnection* connection, gpointer user_data) {
    CustomSoupServer* server = (CustomSoupServer*)user_data;
    if (server) {
      server->OnClientClosed(connection);
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
    CustomSoupServer* custom_server = (CustomSoupServer*)user_data;
    if (custom_server) {
      custom_server->OnClientConnected(server, connection, path, client);
    }
  }

protected:
  void OnClientConnected(SoupServer* server, SoupWebsocketConnection* connection,
    const char* path, SoupClientContext* client) {

    printf("XR-Server server:%s get connection:%p, path:%s, client:%p \n", m_name.c_str(), connection, path, client);

    g_signal_connect(G_OBJECT(connection), "closed",
      G_CALLBACK(soup_websocket_closed_cb), this);


    m_connection = connection;
    g_object_ref(G_OBJECT(m_connection));

    g_signal_connect(G_OBJECT(connection), "message",
      G_CALLBACK(soup_websocket_message_cb), this);
  }
  void OnMessage(G_GNUC_UNUSED SoupWebsocketConnection* connection,
    SoupWebsocketDataType data_type, GBytes* message) {

    printf("XR-Server %s connection:%p get msg \n", m_name.c_str(), connection);

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
  void OnClientClosed(SoupWebsocketConnection* connection) {
    printf("XR-Server %s connection:%p closed \n", m_name.c_str(), connection);
    g_close_count++;
    if (g_close_count % 2 == 0) {
      printf("\n\n\n\n");
    }
    if (m_connection) {
      g_object_unref(G_OBJECT(m_connection));
      m_connection = NULL;
    }
  }
  
protected:
  GMainLoop* m_loop = NULL;

  SoupServer* m_server = NULL;
  SoupWebsocketConnection* m_connection = NULL;

  int m_port = 0;
  std::string m_name;
  std::string m_path;
};

bool CustomSoupServer::Init() {
  int argc = 0;
  gst_init(&argc, NULL);

  m_loop = g_main_loop_new(NULL, false);

  m_server = soup_server_new(SOUP_SERVER_SERVER_HEADER, m_name.c_str(), NULL);

  // Set up the request handler
  soup_server_add_websocket_handler(m_server, m_path.c_str(), NULL, NULL,
    soup_websocket_handler, this, NULL);

  // Set the port to listen on
  soup_server_listen_all(m_server, m_port, SOUP_SERVER_LISTEN_IPV4_ONLY, NULL);

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

CustomSoupServer g_video_server(VIDEO_PORT, "Video", "/video");
CustomSoupServer g_audio_server(AUDIO_PORT, "Audio", "/audio");

void start_soup_server() {
  std::thread video_thread([]() {
    g_video_server.Start();
   });
  std::thread audio_thread([]() {
    g_audio_server.Start();
   });
  while (true)
  {
    g_usleep(1000);
  }
}
