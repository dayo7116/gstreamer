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

  g_setenv("GST_DEBUG", "*:5", TRUE);

  gst_init(&argc, &argv);

  if (argc != 2) {
    g_print("usage: %s <uri>\n Writes snapshot.png in the current directory\n",
      argv[0]);
    //exit(-1);
  }
  GstElement* pipeline, * source, * convert, * resample, * encoder, * sink;
  GstCaps* caps;

  pipeline = gst_pipeline_new("audio-pipeline");

  source = gst_element_factory_make("wasapi2src", "audio-source");
  g_object_set(G_OBJECT(source), "device", "{E6327CAD-DCEC-4949-AE8A-991E976A79D2}", "loopback", true, NULL);

  convert = gst_element_factory_make("audioconvert", "audio-convert");

  resample = gst_element_factory_make("audioresample", "audio-resample");

  encoder = gst_element_factory_make("wavenc", "audio-encoder");

  sink = gst_element_factory_make("filesink", "file-sink");
  g_object_set(G_OBJECT(sink), "location", "D:\\test_audio.wav", NULL);

  gst_bin_add_many(GST_BIN(pipeline), source, convert, resample, encoder, sink, NULL);

  gst_element_link_many(source, convert, resample, encoder, sink, NULL);

  caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE", "rate", G_TYPE_INT, 48000, "channels", G_TYPE_INT, 2, NULL);
  g_object_set(G_OBJECT(source), "caps", caps, NULL);
  gst_caps_unref(caps);

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
