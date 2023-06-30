#include "AudioCapture.h"

#include <gst/gst.h>
#ifdef HAVE_GTK
#include <gtk/gtk.h>
#endif

#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <gst/gstbuffer.h>

#define CAPS "video/x-raw,format=RGB,width=160,pixel-aspect-ratio=1/1"

#pragma comment(lib, "gstapp-1.0.lib")

static void eos_cb(GstElement* sink, gpointer data) {
  g_print("eos_cb \n");
}

// 回调函数，将音频数据填充到缓冲区中
static GstFlowReturn on_new_sample_from_sink(GstElement* sink, gpointer data) {
  GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

  if (sample) {
    GstBuffer* buffer;
    GstCaps* caps;
    GstStructure* s;

    gint width, height;
    GstMapInfo map;

    buffer = gst_sample_get_buffer(sample);
    guint size = gst_buffer_get_size(buffer);

    /* get the snapshot buffer format now. We set the caps on the appsink so
   * that it can only be an rgb buffer. The only thing we have not specified
   * on the caps is the height, which is dependant on the pixel-aspect-ratio
   * of the source material */
    caps = gst_sample_get_caps(sample);
    if (!caps) {
      g_print("could not get snapshot format\n");
      exit(-1);
    }
    s = gst_caps_get_structure(caps, 0);

    /* we need to get the final caps on the buffer to get the size */
    /*gboolean res = gst_structure_get_int(s, "width", &width);
    res |= gst_structure_get_int(s, "height", &height);
    if (!res) {
      g_print("could not get snapshot dimension\n");
      exit(-1);
    }*/

    /* create pixmap from buffer and save, gstreamer video buffers have a stride
     * that is rounded up to the nearest multiple of 4 */
    buffer = gst_sample_get_buffer(sample);
    /* Mapping a buffer can fail (non-readable) */
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
#ifdef HAVE_GTK
      pixbuf = gdk_pixbuf_new_from_data(map.data,
        GDK_COLORSPACE_RGB, FALSE, 8, width, height,
        GST_ROUND_UP_4(width * 3), NULL, NULL);

      /* save the pixbuf */
      gdk_pixbuf_save(pixbuf, "snapshot.png", "png", &error, NULL);
#endif
      gst_buffer_unmap(buffer, &map);
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

int test_audio_capture(int argc, char* argv[])
{
  GstSample* sample;
  gchar* descr;
  GError* error = NULL;
  gint64 duration, position;
  GstStateChangeReturn ret;
  gboolean res;
  GstMapInfo map;

  g_setenv("GST_DEBUG", "*:7", TRUE);

  gst_init(&argc, &argv);

  if (argc != 2) {
    g_print("usage: %s <uri>\n Writes snapshot.png in the current directory\n",
      argv[0]);
    //exit(-1);
  }
  GstElement* appsink_audio, * audio_queue, * pipeline, * audio_filter;
  GstCaps* caps;

  audio_filter = gst_element_factory_make("capsfilter", "audio_filter");
  //caps = gst_caps_from_string("audio/mpeg, stream-format=(string) raw");
  caps = gst_caps_new_any();
  g_object_set(G_OBJECT(audio_filter), "caps", caps, NULL);
  gst_caps_unref(caps);

  appsink_audio = gst_element_factory_make("appsink", "appsink_audio");

  g_object_set(G_OBJECT(appsink_audio), "emit-signals", TRUE, "sync", FALSE, NULL);
  g_signal_connect(appsink_audio, "new-sample", G_CALLBACK(on_new_sample_from_sink), NULL);
  g_signal_connect(appsink_audio, "eos", G_CALLBACK(eos_cb), NULL);

  audio_queue = gst_element_factory_make("queue", "audio_queue");
  pipeline = gst_pipeline_new("audio-capture-pipeline");

  GstElement* audiosrc, * audioconvert, * aac_enc, * audio_resample, * audio_src_filter, * aac_parse;
  aac_parse = gst_element_factory_make("aacparse", "aac_parse");
  audiosrc = gst_element_factory_make("audiotestsrc", "audiosrc");//wasapisrc
  //g_object_set(G_OBJECT(audiosrc), "device", "{0.0.1.00000000}.{59abac3a - 90f8 - 4ee1 - 9e4a - 4e34289043d4}", "low-latency", TRUE, "use-audioclient3", TRUE, NULL);

  audio_src_filter = gst_element_factory_make("capsfilter", "audio_src_filter");
  //caps = gst_caps_from_string("audio/x-raw, rate=(int) { 16000, 24000, 48000 }");
  caps = gst_caps_new_any();
  g_object_set(G_OBJECT(audio_src_filter), "caps", caps, NULL);
  gst_caps_unref(caps);

  audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
  aac_enc = gst_element_factory_make("avenc_aac", "aac_enc");
  audio_resample = gst_element_factory_make("audioresample", "audioresample");

  gst_bin_add_many(GST_BIN(pipeline), appsink_audio, audiosrc, audioconvert, aac_enc,
    audio_queue, audio_filter, audio_resample, audio_src_filter, aac_parse, NULL);
  if (!gst_element_link_many(audiosrc, audio_resample, audio_queue, audioconvert, audio_src_filter, aac_enc, aac_parse, audio_filter,
    appsink_audio,
    NULL)) {
    g_printerr("Audio elements could not be linked.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  GMainLoop* loop = g_main_loop_new(NULL, FALSE);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  GstBus* bus = gst_element_get_bus(pipeline);
  gst_bus_add_signal_watch(bus);
  g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)bus_call, loop);
  gst_object_unref(bus);

  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
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
