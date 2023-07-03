#include "AudioCapture.h"

#include <gst/gst.h>
#ifdef HAVE_GTK
#include <gtk/gtk.h>
#endif

#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <gst/gstbuffer.h>

#include <string>

#include "AudioPlayer.h"

#pragma comment(lib, "gstapp-1.0.lib")

static void eos_cb(GstElement* sink, gpointer data) {
  g_print("eos_cb \n");
}

PLAY::AudioPlayer* g_player;

FILE* g_file = NULL;
int g_total_count = 0;
// 回调函数，将音频数据填充到缓冲区中
static GstFlowReturn on_new_sample_from_sink(GstElement* sink, gpointer data) {
  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

  if (sample) {
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    guint size = gst_buffer_get_size(buffer);

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {

      if (g_file) {
        fwrite(map.data, sizeof(guint8), map.size, g_file);

        std::shared_ptr<PLAY::AudioBlock> audio_frame = std::shared_ptr<PLAY::AudioBlock>(new PLAY::AudioBlock());
        audio_frame->data = new unsigned char[size];
        audio_frame->size = size;
        memcpy(audio_frame->data, map.data, map.size);
        g_player->AddOneAudioFrame(audio_frame);

        g_total_count += size;
      }

      gst_buffer_unmap(buffer, &map);
    }

    if (g_total_count > 100 * 1024) {
      fclose(g_file);
      g_file = NULL;
      g_total_count = 0;

      g_player->StartPlay();
    }
    gst_sample_unref(sample);
  }

  return GST_FLOW_OK;
}

// 用于处理收到的 GStreamer 消息的回调函数
static gboolean bus_call(GstBus* bus, GstMessage* msg, gpointer data) {
  GMainLoop* loop = (GMainLoop*)data;

  switch (GST_MESSAGE_TYPE(msg)) {

  case GST_MESSAGE_EOS:
    g_print("End of stream\n");
    g_main_loop_quit(loop);
    break;

  case GST_MESSAGE_ERROR: {
    gchar* debug;
    GError* err;
    G_FILE_ERROR;
    G_FILE_ERROR_NOENT;

    gst_message_parse_error(msg, &err, &debug);
    g_print("Error: %s, domain:%s, code:%d\n", err->message, g_quark_to_string(err->domain), err->code);
    g_print("Debug: %s\n", debug);
    g_error_free(err);
    g_free(debug);
    g_main_loop_quit(loop);
    break;
  }

  default:
    break;
  }

  return TRUE;
}

class GStructAutoRelease {
public:
  GStructAutoRelease(GstStructure* ptr)
  : m_ptr(ptr){}
  virtual ~GStructAutoRelease() {
    if (m_ptr) {
      gst_structure_free(m_ptr);
    }
  }

protected:
  GstStructure* m_ptr;
};

std::string get_default_device() {
  std::string default_device_id;

  GstDeviceMonitor* monitor = gst_device_monitor_new();
  gst_device_monitor_add_filter(monitor, "Audio/Source", NULL);

  // Start monitoring
  gst_device_monitor_start(monitor);

  // Get all devices
  GList* devices = gst_device_monitor_get_devices(monitor);
  for (GList* iterator = devices; iterator != NULL; iterator = iterator->next) {
    GstDevice* device = GST_DEVICE(iterator->data);

    GstStructure* properties = gst_device_get_properties(device);
    if (!properties) {
      continue;
    }
    GStructAutoRelease auto_relase(properties);
    gboolean is_default = FALSE;
    if (!gst_structure_has_field(properties, "device.default") || G_TYPE_BOOLEAN != gst_structure_get_field_type(properties, "device.default")) {
      continue;
    }
    gst_structure_get_boolean(properties, "device.default", &is_default);
    if (!is_default) {
      continue;
    }

    if (!gst_structure_has_field(properties, "device.api") || G_TYPE_STRING != gst_structure_get_field_type(properties, "device.api")) {
      continue;
    }
    const gchar* device_api = gst_structure_get_string(properties, "device.api");
    if (!device_api) {
      continue;
    }

    if (0 != g_ascii_strcasecmp(device_api, "wasapi2")) {
      continue;
    }

    if (!gst_structure_has_field(properties, "device.id") || G_TYPE_STRING != gst_structure_get_field_type(properties, "device.id")) {
      continue;
    }

    const gchar* id = gst_structure_get_string(properties, "device.id");
    if (id) {
      default_device_id = id;
      break;
    }
  }

  // Clean up
  gst_device_monitor_stop(monitor);
  gst_object_unref(monitor);
  g_list_free(devices);

  return default_device_id;
}

int test_audio_capture(int argc, char* argv[])
{
  GstSample* sample;
  gchar* descr;
  GError* error = NULL;
  gint64 duration, position;
  GstStateChangeReturn ret;
  gboolean res;
  GstMapInfo map;

  g_setenv("GST_DEBUG", "*:1", TRUE);

  gst_init(&argc, &argv);

  if (argc != 2) {
    g_print("usage: %s <uri>\n Writes snapshot.png in the current directory\n",
      argv[0]);
    //exit(-1);
  }

  g_player = new PLAY::AudioPlayer();

  GstElement* pipeline, * source, * convert, * resample, * encoder, * muxer, * sink, * audio_queue;

  pipeline = gst_pipeline_new("audio-pipeline");

  source = gst_element_factory_make("wasapi2src", "audio-source");
  g_object_set(G_OBJECT(source), "device", get_default_device().c_str() , "loopback", true, NULL);

  convert = gst_element_factory_make("audioconvert", "audio-convert");

  resample = gst_element_factory_make("audioresample", "audio-resample");

  encoder = gst_element_factory_make("wavenc", "audio-encoder");

  encoder = gst_element_factory_make("vorbisenc", "audio-encoder");
  muxer = gst_element_factory_make("oggmux", "audio-muxer");

  //sink = gst_element_factory_make("filesink", "file-sink");
  //g_object_set(G_OBJECT(sink), "location", "D:\\test_audio.ogg", NULL);
  g_file = fopen("D:\\test_audio.ogg", "ab");
  sink = gst_element_factory_make("appsink", "appsink_audio");
  g_object_set(G_OBJECT(sink), "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect(sink, "new-sample", G_CALLBACK(on_new_sample_from_sink), NULL);
  g_signal_connect(sink, "eos", G_CALLBACK(eos_cb), NULL);

  gst_bin_add_many(GST_BIN(pipeline), source, encoder, muxer, sink, NULL);

  gst_element_link_many(source, encoder, muxer, sink, NULL);

  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

  GMainLoop* loop = g_main_loop_new(NULL, FALSE);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_add_signal_watch(bus);
  g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)bus_call, loop);
  gst_object_unref(bus);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Unable to set the pipeline to the playing state.\n");
    return -1;
  }

  g_main_loop_run(loop);

  /* cleanup and exit */
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(pipeline);

  exit(0);
}
