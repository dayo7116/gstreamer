#include "AudioPlayer.h"

#include <memory>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <vector>
#include <mutex>
#include <thread>

//for websockt
#include <libsoup/soup.h>

#if ON_WINDOWS
#pragma comment(lib, "gstbase-1.0.lib")
#pragma comment(lib, "gstaudio-1.0.lib")
#endif

#if ON_WINDOWS
#else
#include <android/log.h>
#include <jni.h>
#include <android/asset_manager_jni.h>
#include <android/asset_manager.h>
static AAssetManager* s_nativeasset = nullptr;
static JNIEnv* s_env = nullptr;
static jobject s_jobj;
static jmethodID s_mid;

extern "C" JNIEXPORT
void JNICALL Java_org_freedesktop_gstreamer_tutorials_tutorial_16_Tutorial6_setNativeAssetManager(JNIEnv * env, jobject instance, jobject assetManager) {
  //s_env = env;
  s_nativeasset = AAssetManager_fromJava(env, assetManager);
}

std::vector<char> readFileFromAssets(const char* filename) {
  AAsset* pathAsset = AAssetManager_open(s_nativeasset, filename, AASSET_MODE_UNKNOWN);
  off_t assetLength = AAsset_getLength(pathAsset);
  unsigned char* fileData = (unsigned char*)AAsset_getBuffer(pathAsset);
  std::vector<char> buffer(assetLength);
  memcpy(buffer.data(), fileData, assetLength);
  AAsset_close(pathAsset);
  return buffer;
}
#endif

namespace PLAY {


#define BITS_PER_BYTE 8


#define CHUNK_SIZE 1024         /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 48000       /* Samples per second we are sending */

  /* Structure to contain all our information, so we can pass it to callbacks */
  struct CustomData
  {
    GstElement* pipeline, * app_source, * audio_convert,
      * audio_resample, * audio_sink;

    guint64 num_samples = 0;          /* Number of samples generated so far (for timestamp generation) */
    gfloat a = 0.0, b = 0.0, c = 0.0, d = 0.0;            /* For waveform generation */

    guint sourceid = 0;               /* To control the GSource */

    GMainLoop* main_loop;         /* GLib's Main Loop */
    std::vector< std::shared_ptr<AudioBlock> > audio_datas;
    int audio_frame_index = 0;
  };

  static std::mutex g_lock;
  int g_count = 0;

  static bool g_connected = false;
  /* This method is called by the idle GSource in the mainloop, to feed CHUNK_SIZE bytes into appsrc.
   * The idle handler is added to the mainloop when appsrc requests us to start sending data (need-data signal)
   * and is removed when appsrc has enough data (enough-data signal).
   */
  static gboolean
    push_data(CustomData* data)
  {
    GstBuffer* buffer;
    GstFlowReturn ret;
    int i;
    GstMapInfo map;
    gint16* raw;
    gint num_samples = CHUNK_SIZE / 2;    /* Because each sample is 16 bits */
    gfloat freq;

    std::shared_ptr<AudioBlock> audio_frame;
    {
      std::lock_guard<std::mutex> auto_lock(g_lock);
      if (data->audio_datas.empty()) {
        return TRUE;
      }
      audio_frame = data->audio_datas.front();
      data->audio_datas.erase(data->audio_datas.begin());
    }
    g_count++;

    /*if (g_count % 3 == 0 || g_count % 4 == 0) {
      return TRUE;
    }*/

    if (!audio_frame) {
      return TRUE;
    }
    /* Create a new empty buffer */
    int buf_size = audio_frame->size;

    buffer = gst_buffer_new_and_alloc(buf_size);

    /* Set its timestamp and duration */
    //GST_BUFFER_TIMESTAMP(buffer) =
    //  gst_util_uint64_scale(16, GST_SECOND, SAMPLE_RATE);
    //GST_BUFFER_DURATION(buffer) =
    //  gst_util_uint64_scale(16, GST_SECOND, SAMPLE_RATE);

    /* Generate some psychodelic waveforms */
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    raw = (gint16*)map.data;

    memcpy(raw, audio_frame->data, buf_size);

    gst_buffer_unmap(buffer, &map);
    data->num_samples += num_samples;

    /* Push the buffer into the appsrc */
    g_signal_emit_by_name(data->app_source, "push-buffer", buffer, &ret);

    /* Free the buffer now that we are done with it */
    gst_buffer_unref(buffer);

    /*data->audio_frame_index++;
    data->audio_frame_index = data->audio_frame_index % data->audio_datas.size();*/

    if (ret != GST_FLOW_OK) {
      /* We got some error, stop sending data */
      return FALSE;
    }

    return TRUE;
  }

  /* This signal callback triggers when appsrc needs data. Here, we add an idle handler
   * to the mainloop to start pushing data into the appsrc */
  static void
    start_feed(GstElement* source, guint size, CustomData* data)
  {
    if (data->sourceid == 0) {
      g_print("Start feeding\n");
      data->sourceid = g_idle_add((GSourceFunc)push_data, data);
    }
  }

  /* This callback triggers when appsrc has enough data and we can stop sending.
   * We remove the idle handler from the mainloop */
  static void
    stop_feed(GstElement* source, CustomData* data)
  {
    if (data->sourceid != 0) {
      g_print("Stop feeding\n");
      g_source_remove(data->sourceid);
      data->sourceid = 0;
    }
  }

  /* The appsink has received a buffer */
  static GstFlowReturn
    new_sample(GstElement* sink, CustomData* data)
  {
    GstSample* sample;

    /* Retrieve the buffer */
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (sample) {
      /* The only thing we do in this example is print a * to indicate a received buffer */
      g_print("*");
      gst_sample_unref(sample);
      return GST_FLOW_OK;
    }

    return GST_FLOW_ERROR;
  }

  /* This function is called when an error message is posted on the bus */
  static void
    error_cb(GstBus* bus, GstMessage* msg, CustomData* data)
  {
    GError* err;
    gchar* debug_info;

    /* Print error details on the screen */
    gst_message_parse_error(msg, &err, &debug_info);
    g_printerr("Error received from element %s: %s\n",
      GST_OBJECT_NAME(msg->src), err->message);
    g_printerr("Debugging information: %s\n", debug_info ? debug_info : "none");
    g_clear_error(&err);
    g_free(debug_info);

    g_main_loop_quit(data->main_loop);
  }

  static void
    on_pad_added(GstElement* element,
      GstPad* pad,
      gpointer    data)
  {
    GstPad* sinkpad;
    GstElement* decoder = (GstElement*)data;

    /* We can now link this pad with the vorbis-decoder sink pad */
    g_print("Dynamic pad created, linking demuxer/decoder\n");

    sinkpad = gst_element_get_static_pad(decoder, "sink");

    GstPadLinkReturn ret = gst_pad_link(pad, sinkpad);
    if (GST_PAD_LINK_OK != ret) {
      g_print("Link Error\n");
    }

    gst_object_unref(sinkpad);

    g_connected = TRUE;
  }

  class AudioPlayer {
  public:
    AudioPlayer() {}
    ~AudioPlayer() {
      if (m_loop_thread) {
        m_loop_thread->join();
        delete m_loop_thread;
        m_loop_thread = NULL;
      }
    }

    void StartPlay() {

#if PLAY_FILE

#if ON_WINDOWS
        const char* file_path = "D:/test_audio_cpp.opus";
#else
        const char* file_path = "/storage/emulated/0/Android/data/org.freedesktop.gstreamer.tutorials.tutorial_2/files/test_audio.opus";
#endif

        FILE* file = fopen(file_path, "rb");
        int block_count = 0;
        if (file) {
            while (true) {

                int sz = 0;
                int read_count = sizeof(int);
                std::vector<char> datas;
                int result = fread(&sz, read_count, 1, file);
                if (sz == 0) {
                    fclose(file);
                    break;
                }
                datas.resize(sz);
                result = fread(datas.data(), 1, sz, file);

                std::shared_ptr<AudioBlock> audio_frame = std::shared_ptr<AudioBlock>(new AudioBlock());
                audio_frame->data = new unsigned char[sz];
                audio_frame->size = sz;
                memcpy(audio_frame->data, datas.data(), sz);

                block_count++;
                if (block_count > 2)
                {
                    add_audio_frame(audio_frame.get());
                }

            }
        }

#endif



      m_loop_thread = new std::thread([this] {
        this->StartAndLoop();
        });
    }

    void AddOneAudioFrame(const std::shared_ptr<AudioBlock>& audio_frame) {
      std::lock_guard<std::mutex> auto_lock(g_lock);
      m_data.audio_datas.push_back(audio_frame);
      //printf("Add One Audio Frame, total:%d \n", m_data.audio_datas.size());
    }

    void StartAndLoop() {
      GstAudioInfo info;
      GstCaps* audio_caps;
      GstBus* bus;

      /* Initialize cumstom m_data structure */
      m_data.b = 1;                   /* For waveform generation */
      m_data.d = 1;
      g_setenv("GST_DEBUG", "*:7", TRUE);
      /* Initialize GStreamer */
      gst_init(NULL, NULL);

      /* Create the elements */
      m_data.app_source = gst_element_factory_make("appsrc", "audio_source");
     /* GstElement* demuxer = gst_element_factory_make("mpegaudioparse", "ogg-demuxer");
      GstElement* decoder = gst_element_factory_make("mpg123audiodec", "vorbis-decoder");*/

      GstElement* demuxer = gst_element_factory_make("opusparse", "ogg-demuxer");
      GstElement* decoder = gst_element_factory_make("opusdec", "vorbis-decoder");

      GstElement* convert = gst_element_factory_make("audioconvert", "audio-convert");
      m_data.audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");

      /* Create the empty pipeline */
      m_data.pipeline = gst_pipeline_new("test-pipeline");

      if (!m_data.pipeline || !m_data.app_source || !demuxer || !decoder || !m_data.audio_sink) {
        g_printerr("Not all elements could be created.\n");
        return;
      }

       g_signal_connect(m_data.app_source, "need-data", G_CALLBACK(start_feed),
                                   &m_data);
       g_signal_connect(m_data.app_source, "enough-data", G_CALLBACK(stop_feed),
                                   &m_data);

                  /* Link all elements that can be automatically linked because they have "Always" pads */
      gst_bin_add_many(GST_BIN(m_data.pipeline), m_data.app_source, demuxer, decoder, convert,
        m_data.audio_sink, NULL);

      gboolean link_many = gst_element_link_many(m_data.app_source, demuxer, decoder, convert, m_data.audio_sink, NULL);

      /*gboolean link_demuxer = gst_element_link(m_data.app_source, demuxer);
      gboolean link_decoder = gst_element_link_many(decoder, convert, m_data.audio_sink, NULL);

      g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), decoder);*/

      /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
      bus = gst_element_get_bus(m_data.pipeline);
      gst_bus_add_signal_watch(bus);
      g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb,
        &m_data);
      gst_object_unref(bus);

      /* Start playing the pipeline */
      GstStateChangeReturn ret = gst_element_set_state(m_data.pipeline, GST_STATE_PLAYING);

      /* Create a GLib Main Loop and set it to run */
      m_data.main_loop = g_main_loop_new(NULL, FALSE);
      g_main_loop_run(m_data.main_loop);

      /* Release the request pads from the Tee, and unref them */

      /* Free resources */
      gst_element_set_state(m_data.pipeline, GST_STATE_NULL);
      gst_object_unref(m_data.pipeline);
    }

  protected:
    std::thread* m_loop_thread = NULL;
    CustomData m_data;
  };
}

namespace XRClient {
    class SocketMessageListener {
    public:
        virtual void OnMessage(SoupWebsocketConnection * conn, SoupWebsocketDataType type, gchar *data, gsize size) = 0;
    };

    class XRSocketClient {
    public:
        XRSocketClient(const char* name)
        : m_name(name) {

        }
        virtual ~XRSocketClient() {
            Quit();
        }

    public:
        //连接到server并接收消息
        void Start(const char* server_ip, std::shared_ptr<SocketMessageListener> processor);
        void Quit();

    protected:
        void RunLoop();

        static gboolean AsyncConnectFun(gpointer user_data);
        void ConnectServer();

        static gboolean ServerConnectedCallback(SoupSession * session, GAsyncResult * res, void *user_data);
        bool OnServerConnected(SoupSession * session, GAsyncResult * res) ;

        static void ServerClosedCallback(SoupWebsocketConnection* conn, void *user_data);
        void OnServerClosed(SoupWebsocketConnection* connection);

        static void ServerMessageCallback(SoupWebsocketConnection * conn, SoupWebsocketDataType type, GBytes * message,void *user_data);
        void OnServerMessage(SoupWebsocketConnection * connection, SoupWebsocketDataType type, GBytes * message);

    protected:
        std::string m_name;
        std::shared_ptr<std::thread> m_loop_thread;
        pthread_t m_thread_handle = 0;

        GMainLoop *m_main_loop = NULL;
        SoupSession * m_soup_session = NULL;
        SoupMessage *m_soup_message = NULL;

        SoupWebsocketConnection *m_connection = NULL;
        std::string m_server_ip;

        std::weak_ptr<SocketMessageListener> m_data_processor;
    };


    void XRSocketClient::Start(const char* server_ip, std::shared_ptr<SocketMessageListener> processor) {
        if (nullptr != m_loop_thread) {
            return;
        }

        if (server_ip) {
            m_server_ip = server_ip;
        }
        m_data_processor = processor;
        m_loop_thread = std::make_shared<std::thread>([this]() {
            pthread_setname_np(m_thread_handle, "ClientXRLoop");
            m_thread_handle = 0;
            RunLoop();
        });
        m_thread_handle = m_loop_thread->native_handle();
        m_loop_thread->detach();
    }

    void XRSocketClient::RunLoop() {
        GMainContext *context = g_main_context_new ();

        g_main_context_invoke(context, (GSourceFunc) AsyncConnectFun, this);
        g_main_context_push_thread_default (context);

        GMainLoop *loop = g_main_loop_new(context, FALSE);
        m_main_loop = loop;

        g_main_loop_run (loop);

        g_main_context_pop_thread_default (context);

        g_main_loop_unref(loop);
        g_main_context_unref(context);
    }

    void XRSocketClient::Quit() {
        if (m_main_loop) {
            g_main_loop_quit(m_main_loop);
            m_main_loop = NULL;
        }
        if (m_loop_thread) {
            m_loop_thread = nullptr;
        }

        if (m_connection) {
            if (SOUP_WEBSOCKET_STATE_OPEN == soup_websocket_connection_get_state (m_connection)) {
                soup_websocket_connection_close (m_connection, 1000, "");
            }
            g_object_unref (m_connection);
            m_connection = NULL;
        }

        if (m_soup_session) {
            g_object_unref(m_soup_session);
            m_soup_session = NULL;
        }
        if (m_soup_message) {
            g_object_unref(m_soup_message);
            m_soup_message = NULL;
        }
    }


    gboolean XRSocketClient::AsyncConnectFun(gpointer user_data) {
        XRSocketClient* client = (XRSocketClient*)user_data;
        if (client) {
            client->ConnectServer();
        }
        return G_SOURCE_REMOVE;
    }
    void XRSocketClient::ConnectServer() {
        const char *https_aliases[] = { "wss", NULL };
        m_soup_session = soup_session_new_with_options (
                SOUP_SESSION_SSL_STRICT, TRUE,
                SOUP_SESSION_HTTPS_ALIASES, https_aliases, NULL);

        gchar *server_url = g_strdup(m_server_ip.c_str());
        m_soup_message = soup_message_new (SOUP_METHOD_GET, server_url);
        g_free (server_url);

        // Once connected, we will register
        soup_session_websocket_connect_async (m_soup_session, m_soup_message, NULL, NULL, NULL,
                                              (GAsyncReadyCallback) ServerConnectedCallback, this);

    }

    gboolean XRSocketClient::ServerConnectedCallback(SoupSession * session, GAsyncResult * res, void *user_data) {
        XRSocketClient* client = (XRSocketClient*)user_data;
        if (client) {
            return client->OnServerConnected(session, res);
        }
        return FALSE;
    }
    bool XRSocketClient::OnServerConnected(SoupSession * session, GAsyncResult * res) {
        GError *error = NULL;

        // 创建连接对像
        m_connection = soup_session_websocket_connect_finish (session, res, &error);
        __android_log_print (ANDROID_LOG_ERROR, "XRSocket",
                             "%s gets connection %p connected", m_name.c_str(), m_connection);
        if (error) {
            Quit();
            g_error_free (error);
            return false;
        }

        soup_websocket_connection_set_max_incoming_payload_size(m_connection, 16 * 1024 * 1024);
        // 心跳时间
        soup_websocket_connection_set_keepalive_interval(m_connection, 1);

        g_signal_connect (m_connection, "message", G_CALLBACK (ServerMessageCallback), this);
        g_signal_connect (m_connection, "closed", G_CALLBACK (ServerClosedCallback), this);
        return true;
    }

    void XRSocketClient::ServerClosedCallback(SoupWebsocketConnection* conn, void *user_data) {
        XRSocketClient* client = (XRSocketClient*)user_data;
        if (client) {
            client->OnServerClosed(conn);
        }
    }
    void XRSocketClient::OnServerClosed(SoupWebsocketConnection* connection) {
        SoupWebsocketState state = soup_websocket_connection_get_state(connection);
        __android_log_print (ANDROID_LOG_ERROR, "XRSocket",
                             "%s gets connection %p closed", m_name.c_str(), m_connection);
        // 如果连接已经被关闭，就尝试重新建立连接
        if (state == SOUP_WEBSOCKET_STATE_CLOSED) {
            Quit();
            //TODO: @dayong 添加重连策略
        }
    }

    void XRSocketClient::ServerMessageCallback(SoupWebsocketConnection * conn, SoupWebsocketDataType type, GBytes * message,void *user_data) {
        XRSocketClient* client = (XRSocketClient*)user_data;
        if (client) {
            client->OnServerMessage(conn, type, message);
        }
    }
    void XRSocketClient::OnServerMessage(SoupWebsocketConnection * connection, SoupWebsocketDataType type, GBytes * message) {
        std::shared_ptr<SocketMessageListener> processor = m_data_processor.lock();
        if (processor) {
            gsize size;
            gchar *data = (gchar*)g_bytes_unref_to_data (message, &size);
            processor->OnMessage(connection, type, data, size);
            g_free (data);
        }
    }
}

XRClient::XRSocketClient g_video_client("video");
XRClient::XRSocketClient g_audio_client("audio");

class VideoMessageListener : public XRClient::SocketMessageListener {
public:
    VideoMessageListener(){}
    virtual ~VideoMessageListener(){}

    void OnMessage(SoupWebsocketConnection * conn, SoupWebsocketDataType type, gchar *data, gsize size) override {
        __android_log_print (ANDROID_LOG_ERROR, "XRSocket",
                             "Video gets message data, type:%d, count:%d", type, size);
        switch (type) {
            case SOUP_WEBSOCKET_DATA_BINARY:
                break;

            case SOUP_WEBSOCKET_DATA_TEXT:
                /* Convert to NULL-terminated string */
                gchar *text = g_strndup (data, size);
                g_free (text);
                break;
        }
    }
};

class AudioMessageListener : public XRClient::SocketMessageListener {
public:
    AudioMessageListener(){}
    virtual ~AudioMessageListener(){}

    void OnMessage(SoupWebsocketConnection * conn, SoupWebsocketDataType type, gchar *data, gsize size) override {
        __android_log_print (ANDROID_LOG_ERROR, "XRSocket",
                             "Audio gets message data, type:%d, count:%d", type, size);
        switch (type) {
            case SOUP_WEBSOCKET_DATA_BINARY:
                break;

            case SOUP_WEBSOCKET_DATA_TEXT:
                /* Convert to NULL-terminated string */
                gchar *text = g_strndup (data, size);
                g_free (text);
                break;
        }
    }
};

std::shared_ptr<VideoMessageListener> g_video_msg_listener;
std::shared_ptr<AudioMessageListener> g_audio_msg_listener;

void start_video_client(const char* server_ip) {
    if (!g_video_msg_listener) {
        g_video_msg_listener = std::make_shared<VideoMessageListener>();
    }
    g_video_client.Start(server_ip, g_video_msg_listener);
}
void stop_video_client() {
    g_video_client.Quit();
}

void start_audio_client(const char* server_ip) {
    if (!g_audio_msg_listener) {
        g_audio_msg_listener = std::make_shared<AudioMessageListener>();
    }
    g_audio_client.Start(server_ip, g_audio_msg_listener);
}
void stop_audio_client() {
    g_audio_client.Quit();
}

PLAY::AudioPlayer g_player;
void start_play() {
  g_player.StartPlay();
}

void add_audio_frame(AudioBlock* block) {
  std::shared_ptr<AudioBlock> audio_frame = std::shared_ptr<AudioBlock>(new AudioBlock());
  audio_frame->data = new unsigned char[block->size];
  audio_frame->size = block->size;
  memcpy(audio_frame->data, block->data, block->size);

  g_player.AddOneAudioFrame(audio_frame);
}
