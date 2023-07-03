#include "AudioPlayer.h"

#include <memory>
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <vector>
#include <mutex>

#pragma comment(lib, "gstbase-1.0.lib")
#pragma comment(lib, "gstaudio-1.0.lib")

namespace PLAY {

#define BITS_PER_BYTE 8


#define CHUNK_SIZE 1024         /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 48000       /* Samples per second we are sending */

    /* Structure to contain all our information, so we can pass it to callbacks */
    typedef struct _CustomData
    {
        GstElement* pipeline, * app_source, * audio_convert,
            * audio_resample, * audio_sink;

        guint64 num_samples = 0;          /* Number of samples generated so far (for timestamp generation) */
        gfloat a = 0.0, b = 0.0, c = 0.0, d = 0.0;            /* For waveform generation */

        guint sourceid = 0;               /* To control the GSource */

        GMainLoop* main_loop;         /* GLib's Main Loop */
        std::vector< std::shared_ptr<AudioBlock> > audio_datas;
        int audio_frame_index = 0;
    } CustomData;

    static std::mutex g_lock;

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

        if (data->audio_datas.empty()) {
            g_main_loop_quit(data->main_loop);
            return FALSE;
        }
        std::lock_guard<std::mutex> auto_lock(g_lock);
        std::shared_ptr<AudioBlock> audio_frame = data->audio_datas.front();

        if (!audio_frame) {
            return FALSE;
        }
        /* Create a new empty buffer */
        int buf_size = audio_frame->size;

        buffer = gst_buffer_new_and_alloc(buf_size);

        /* Set its timestamp and duration */
        GST_BUFFER_TIMESTAMP(buffer) =
            gst_util_uint64_scale(16, GST_SECOND, SAMPLE_RATE);
        GST_BUFFER_DURATION(buffer) =
            gst_util_uint64_scale(16, GST_SECOND, SAMPLE_RATE);

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

        data->audio_datas.erase(data->audio_datas.begin());
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


    class AudioPlayer::Impl {
    public:
        Impl() {}
        ~Impl() {
            if (m_loop_thread) {
                m_loop_thread->join();
                delete m_loop_thread;
                m_loop_thread = NULL;
            }
        }

        void StartPlay() {
            m_loop_thread = new std::thread([this] {
                this->StartAndLoop();
            });
        }

        void AddOneAudioFrame(const std::shared_ptr<AudioBlock>& audio_frame) {
            std::lock_guard<std::mutex> auto_lock(g_lock);
            m_data.audio_datas.push_back(audio_frame);
            printf("Add One Audio Frame, total:%d \n", m_data.audio_datas.size());
        }

        void StartAndLoop() {
            GstAudioInfo info;
            GstCaps* audio_caps;
            GstBus* bus;

            /* Initialize cumstom m_data structure */
            m_data.b = 1;                   /* For waveform generation */
            m_data.d = 1;

            /* Initialize GStreamer */
            gst_init(NULL, NULL);

            /* Create the elements */
            m_data.app_source = gst_element_factory_make("appsrc", "audio_source");

            GstElement* demuxer = gst_element_factory_make("oggdemux", "ogg-demuxer");
            GstElement* decoder = gst_element_factory_make("vorbisdec", "vorbis-decoder");

            m_data.audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");

            /* Create the empty pipeline */
            m_data.pipeline = gst_pipeline_new("test-pipeline");

            if (!m_data.pipeline || !m_data.app_source || !demuxer || !decoder || !m_data.audio_sink) {
                g_printerr("Not all elements could be created.\n");
                return;
            }

            /* Configure appsrc */
            /*gst_audio_info_set_format(&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 2, NULL);
            audio_caps = gst_audio_info_to_caps(&info);
            g_object_set(m_data.app_source, "caps", audio_caps, "format", GST_FORMAT_TIME,
                NULL);*/
            g_signal_connect(m_data.app_source, "need-data", G_CALLBACK(start_feed),
                &m_data);
            g_signal_connect(m_data.app_source, "enough-data", G_CALLBACK(stop_feed),
                &m_data);

            //gst_caps_unref(audio_caps);

            /* Link all elements that can be automatically linked because they have "Always" pads */
            gst_bin_add_many(GST_BIN(m_data.pipeline), m_data.app_source, demuxer, decoder,
                m_data.audio_sink, NULL);
            /*if (gst_element_link_many(m_data.app_source, demuxer, decoder, m_data.audio_sink, NULL) != TRUE) {
                g_printerr("Elements could not be linked.\n");
                gst_object_unref(m_data.pipeline);
                return;
            }*/

            gboolean link_demuxer = gst_element_link(m_data.app_source, demuxer);
            gboolean link_decoder = gst_element_link(decoder, m_data.audio_sink);


            /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
            bus = gst_element_get_bus(m_data.pipeline);
            gst_bus_add_signal_watch(bus);
            g_signal_connect(G_OBJECT(bus), "message::error", (GCallback)error_cb,
                &m_data);
            gst_object_unref(bus);

            /* Start playing the pipeline */
            gst_element_set_state(m_data.pipeline, GST_STATE_PLAYING);

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

    AudioPlayer::AudioPlayer() {
        m_impl = std::shared_ptr<AudioPlayer::Impl>(new AudioPlayer::Impl());
    }
    AudioPlayer::~AudioPlayer() {

    }

    void AudioPlayer::StartPlay() {
        m_impl->StartPlay();
    }
    void AudioPlayer::AddOneAudioFrame(const std::shared_ptr<AudioBlock>& audio_frame) {
        m_impl->AddOneAudioFrame(audio_frame);
    }
}
