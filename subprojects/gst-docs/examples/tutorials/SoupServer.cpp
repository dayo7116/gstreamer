#include "SoupServer.h"

#include <gst/gst.h>
#include <gobject\gsignal.h>

#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
#include <libsoup/soup.h>


#include <thread>
#include <mutex>

#define DEFAULT_PORT 8088


SoupWebsocketConnection* g_connection = NULL;

std::shared_ptr<std::thread> g_send_thread;
bool g_sending_loop = false;
std::mutex g_thread_lock;

void soup_websocket_closed_cb(SoupWebsocketConnection* connection,
  gpointer user_data) {
  printf("XR-Server connection:%p closed \n", connection);
  if (g_connection) {
    g_object_unref(G_OBJECT(g_connection));
    g_connection = NULL;
  }

  std::lock_guard<std::mutex> auto_lock(g_thread_lock);
  g_sending_loop = false;
  if (nullptr != g_send_thread) {
    g_send_thread->join();
    g_send_thread = nullptr;
  }
}

void soup_websocket_message_cb(G_GNUC_UNUSED SoupWebsocketConnection* connection,
  SoupWebsocketDataType data_type, GBytes* message, gpointer user_data) {

  printf("XR-Server connection:%p get msg \n", connection);

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

void soup_websocket_handler(SoupServer* server, SoupWebsocketConnection* connection, const char* path, SoupClientContext* client, gpointer user_data) {

  printf("XR-Server server:%p get connection:%p, path:%s, client:%p \n", server, connection, path, client);

  g_signal_connect(G_OBJECT(connection), "closed",
    G_CALLBACK(soup_websocket_closed_cb), user_data);


  g_connection = connection;
  g_object_ref(G_OBJECT(g_connection));

  g_signal_connect(G_OBJECT(connection), "message",
    G_CALLBACK(soup_websocket_message_cb), user_data);

  //start send thread
  std::lock_guard<std::mutex> auto_lock(g_thread_lock);
  g_sending_loop = true;
  if (nullptr == g_send_thread) {
    g_send_thread = std::make_shared<std::thread>([]() {
      while (g_sending_loop) {
        g_usleep(1000);
      }
    });
  }
  
}


void start_soup_server() {
  SoupServer* server;
  guint port;

  // Create SoupServer instance
  server = soup_server_new(SOUP_SERVER_SERVER_HEADER, "Demo Server", NULL);

  // Set up the request handler
  soup_server_add_websocket_handler(server, "/test", NULL, NULL,
    soup_websocket_handler, NULL, NULL);

  // Set the port to listen on
  port = DEFAULT_PORT;
  soup_server_listen_all(server, port, (SoupServerListenOptions)0/*SOUP_SERVER_LISTEN_IPV4_ONLY*/, NULL);

  // Run the main loop
  g_main_loop_run(g_main_loop_new(NULL, FALSE));

  // Clean up
  g_object_unref(server);
}
