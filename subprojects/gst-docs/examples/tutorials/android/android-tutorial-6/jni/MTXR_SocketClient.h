//
// Created by dayong.li on 2023/7/13.
//

#ifndef ANDROID_MTXR_SOCKETCLIENT_H
#define ANDROID_MTXR_SOCKETCLIENT_H

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <libsoup/soup.h>

class SocketMessageListener {
public:
  virtual void
  OnMessage(SoupWebsocketConnection *conn, SoupWebsocketDataType type,
            gchar *data, gsize size) = 0;
};

class ClientMessageObject {
public:
  virtual SoupWebsocketDataType GetType() = 0;

  virtual std::string ToString() = 0;

  virtual std::unique_ptr< std::vector<unsigned char> > ToBinary() = 0;
};

class XRSocketClient {
public:
  XRSocketClient(const char *name)
      : m_name(name) {
  }

  virtual ~XRSocketClient() {
  }

  //for connection logging
  virtual int GetConnectionID() {
    return 0;
  }

public:
  //连接到server并接收消息
  void Start(const char *server_ip,
             std::shared_ptr<SocketMessageListener> processor);

  void Quit();

  //发送消息到server
  void SendDataAsync(std::shared_ptr<ClientMessageObject> msg) {
    {
      std::lock_guard<std::mutex> auto_lock(m_msg_lock);
      m_current_message = msg;
    }
    m_msg_cv.notify_one();
  }

protected:
  void RunLoop();

  static gboolean AsyncQuitFn(gpointer user_data);
  void OnQuitting();

  static gboolean AsyncConnectFn(gpointer user_data);
  void ConnectServer();

  static gboolean ServerConnectedCallback(SoupSession *session, GAsyncResult *res, void *user_data);
  bool OnServerConnected(SoupSession *session, GAsyncResult *res);

  static void
  ServerClosedCallback(SoupWebsocketConnection *conn, void *user_data);
  void OnServerClosed(SoupWebsocketConnection *connection);

  static void ServerMessageCallback(SoupWebsocketConnection *conn,
                                    SoupWebsocketDataType type,
                                    GBytes *message, void *user_data);
  void OnServerMessage(SoupWebsocketConnection *connection,
                       SoupWebsocketDataType type, GBytes *message);

  static gboolean SendDataAsyncFn(void *user_data);
  void OnSendingData();

protected:
  std::string m_name;
  std::string m_server_ip;

  bool m_receive_loop_running = true;
  std::shared_ptr<std::thread> m_receive_thread;

  bool m_sending_loop_running = false;
  std::shared_ptr<std::thread> m_sending_thread;
  std::shared_ptr<ClientMessageObject> m_current_message;
  std::mutex m_msg_lock;
  std::condition_variable m_msg_cv;

  std::mutex m_resource_lock;

  GMainContext *m_context = NULL;
  GMainLoop *m_receive_loop = NULL;
  SoupSession *m_soup_session = NULL;
  GCancellable *m_cancellable = NULL;
  SoupMessage *m_soup_message = NULL;
  SoupLogger *m_logger = NULL;

  SoupWebsocketConnection *m_connection = NULL;

  std::weak_ptr<SocketMessageListener> m_data_processor;
};


#endif //ANDROID_MTXR_SOCKETCLIENT_H
