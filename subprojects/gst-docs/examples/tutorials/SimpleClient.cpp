#include "SimpleClient.h"

#include <libsoup/soup.h>
#include <mutex>
#include <thread>

SoupWebsocketConnection* g_connection = NULL;

std::thread* g_thread = NULL;
GMainLoop* g_loop = NULL;

gboolean ServerConnectedCallback(SoupSession* session, GAsyncResult* res,
                                 void* user_data) {
  GError* error = NULL;
  g_connection = soup_session_websocket_connect_finish(session, res, &error);

  soup_websocket_connection_set_keepalive_interval(g_connection, 1);

  g_thread = new std::thread([]() {
    std::mutex g_lock;
    std::condition_variable g_cv;

    std::unique_lock<std::mutex> lock(g_lock);
    g_cv.wait_for(lock, std::chrono::milliseconds(10000), [] { return false; });

    if (g_loop) {
      g_main_loop_quit(g_loop);
    }
  });
  return FALSE;
}

std::thread* g_soup_thread = NULL;

void test_simple_client() {
  g_soup_thread = new std::thread([]() {
    GMainLoop* loop = g_main_loop_new(NULL, FALSE);
    g_loop = loop;

    const char* https_aliases[] = {"wss", NULL};

    SoupSession* session = soup_session_new_with_options(
        SOUP_SESSION_SSL_STRICT, TRUE, SOUP_SESSION_HTTPS_ALIASES,
        https_aliases, NULL);
    SoupMessage* message =
        soup_message_new(SOUP_METHOD_GET, "ws://192.168.6.31:8088/video");

    soup_session_websocket_connect_async(
        session, message, NULL, NULL, NULL,
        (GAsyncReadyCallback)ServerConnectedCallback, NULL);

    g_main_loop_run(loop);

    if (g_connection) {
      soup_websocket_connection_close(g_connection, SOUP_WEBSOCKET_CLOSE_NORMAL,
                                      "client close");
    }

    g_object_unref(message);
    g_object_unref(session);
    g_main_loop_unref(loop);
  });

  while (true) {
    g_usleep(1000 * 100);
  }
  
}
