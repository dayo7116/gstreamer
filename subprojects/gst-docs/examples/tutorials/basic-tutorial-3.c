#include <gst/gst.h>
#ifdef HAVE_GTK
#include <gtk/gtk.h>
#endif

#include <gst/app/gstappsink.h>
#include <stdlib.h>
#include <gst/gstbuffer.h>

#define CAPS "video/x-raw,format=RGB,width=160,pixel-aspect-ratio=1/1"

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
    gboolean res = gst_structure_get_int(s, "width", &width);
    res |= gst_structure_get_int(s, "height", &height);
    if (!res) {
      g_print("could not get snapshot dimension\n");
      exit(-1);
    }

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
    gst_message_parse_error(msg, &err, &debug);
    g_print("Error: %s\n", err->message);
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

int
main(int argc, char* argv[])
{
  GstElement* pipeline, * sink;
  gint width, height;
  GstSample* sample;
  gchar* descr;
  GError* error = NULL;
  gint64 duration, position;
  GstStateChangeReturn ret;
  gboolean res;
  GstMapInfo map;

  gst_init(&argc, &argv);

  if (argc != 2) {
    g_print("usage: %s <uri>\n Writes snapshot.png in the current directory\n",
      argv[0]);
    //exit(-1);
  }

  /* create a new pipeline */
  descr =
    g_strdup_printf("uridecodebin uri=%s ! videoconvert ! videoscale ! "
      " appsink name=sink caps=\"" CAPS "\"", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm");
  pipeline = gst_parse_launch(descr, &error);
  
  if (error != NULL) {
    g_print("could not construct pipeline: %s\n", error->message);
    g_clear_error(&error);
    exit(-1);
  }

  /* get sink */
  sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");

  GstAppSinkCallbacks callbacks;
  // 设置回调函数，以处理从音频捕获元素捕获的音频数据
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.new_sample = on_new_sample_from_sink;
  gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, NULL, NULL);

  // 启动 GStreamer 管道
  ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);

  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_print("Unable to set pipeline to playing state.\n");
    gst_object_unref(pipeline);
    return -1;
  }

  // 获取 GStreamer 消息总线
  GstBus* bus = gst_element_get_bus(pipeline);
  GMainLoop* loop = g_main_loop_new(NULL, FALSE);
  // 开始 GStreamer 循环
  gst_bus_add_watch(bus, bus_call, loop);
  g_main_loop_run(loop);

  /* cleanup and exit */
  gst_element_set_state(pipeline, GST_STATE_NULL);
  gst_object_unref(sink);
  gst_object_unref(pipeline);

  exit(0);
}
