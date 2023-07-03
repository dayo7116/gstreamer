#include "play_ogg.h"
#include <gst/gst.h>

gboolean bus_callback(GstBus* bus, GstMessage* message, gpointer data) {
  switch (GST_MESSAGE_TYPE(message)) {
  case GST_MESSAGE_EOS:
    g_print("End of stream\n");
    g_main_loop_quit((GMainLoop*)data);
    break;
  case GST_MESSAGE_ERROR: {
    gchar* debug;
    GError* error;

    gst_message_parse_error(message, &error, &debug);
    g_free(debug);
    g_printerr("Error: %s\n", error->message);
    g_error_free(error);

    g_main_loop_quit((GMainLoop*)data);
    break;
  }
  default:
    break;
  }

  return TRUE;
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

  gst_pad_link(pad, sinkpad);

  gst_object_unref(sinkpad);
}

static void
error_cb(GstBus* bus, GstMessage* msg, void* loop)
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

  g_main_loop_quit((GMainLoop*)loop);
}

int play_ogg(int argc, char* argv[], const char* file_path) {

  //g_setenv("GST_DEBUG", "*:7", TRUE);

  gst_init(&argc, &argv);

  // 创建GMainLoop
  GMainLoop* loop = g_main_loop_new(NULL, FALSE);

  // 创建播放器pipeline
  GstElement* pipeline = gst_pipeline_new("audio-player");

  // 创建元素
  GstElement* source = gst_element_factory_make("filesrc", "file-source");
  GstElement* demuxer = gst_element_factory_make("oggdemux", "ogg-demuxer");
  GstElement* decoder = gst_element_factory_make("vorbisdec", "vorbis-decoder");
  GstElement* sink = gst_element_factory_make("autoaudiosink", "audio-sink");

  if (!pipeline || !source || !demuxer || !decoder || !sink) {
    g_printerr("Not all elements could be created.\n");
    return -1;
  }

  // 设置输入文件路径
  g_object_set(G_OBJECT(source), "location", file_path, NULL);

  // 将元素添加到pipeline
  gst_bin_add_many(GST_BIN(pipeline), source, demuxer, decoder, sink, NULL);

  // 链接元素
  gboolean ret1 = gst_element_link(source, demuxer);
  //gboolean ret2 = gst_element_link(demuxer, decoder);
  gboolean ret3 = gst_element_link_many(decoder, sink, NULL);

  g_signal_connect(demuxer, "pad-added", G_CALLBACK(on_pad_added), decoder);

  // 设置bus
  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
  gst_bus_add_watch(bus, bus_callback, loop);
  gst_object_unref(bus);
  g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb, NULL);

  GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

  g_main_loop_run(loop);

  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(GST_OBJECT(pipeline));
  g_main_loop_unref(loop);

  return 0;
}
