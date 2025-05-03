#include <gst/gst.h>

/* 콜백에 전달할 수 있도록 모든 정보를 포함하는 구조체 */
typedef struct _CustomData {
    GstElement* pipeline;
    GstElement* source;
    GstElement* convert;
    GstElement* resample;
    GstElement* sink;
} CustomData;

/* pad-added 시그널 핸들러 */
static void pad_added_handler(GstElement* src, GstPad* pad, CustomData* data);

int main(int argc, char* argv[]) {
    CustomData data;
    GstBus* bus;
    GstMessage* msg;
    GstStateChangeReturn ret;
    gboolean terminate = FALSE;

    /* GStreamer 초기화 */
    gst_init(&argc, &argv);

    /* 엘리먼트 생성 */
    data.source = gst_element_factory_make("uridecodebin", "source");
    data.convert = gst_element_factory_make("audioconvert", "convert");
    data.resample = gst_element_factory_make("audioresample", "resample");
    data.sink = gst_element_factory_make("autoaudiosink", "sink");

    /* 빈 파이프라인 생성 */
    data.pipeline = gst_pipeline_new("test-pipeline");

    if (!data.pipeline || !data.source || !data.convert || !data.resample || !data.sink) {
        g_printerr("Cannot create all elements.\n");
        return -1;
    }

    /* 파이프라인 빌드. 이 시점에서는 소스를 연결하지 않습니다. 나중에 연결합니다. */
    gst_bin_add_many(GST_BIN(data.pipeline), data.source, data.convert, data.resample, data.sink, NULL);
    if (!gst_element_link_many(data.convert, data.resample, data.sink, NULL)) {
        g_printerr("Cannot link elements.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    /* 재생할 URI 설정 */
    g_object_set(data.source, "uri", "https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm", NULL);

    /* pad-added 시그널에 연결 */
    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    /* 재생 시작 */
    ret = gst_element_set_state(data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Cannot set pipeline to playing state.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    /* 버스 감시 */
    bus = gst_element_get_bus(data.pipeline);
    do {
        msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
            GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

        /* 메시지 파싱 */
        if (msg != NULL) {
            GError* err;
            gchar* debug_info;

            switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR:
                gst_message_parse_error(msg, &err, &debug_info);
                g_printerr("Error occurred in element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
                g_printerr("Debugging info: %s\n", debug_info ? debug_info : "none");
                g_clear_error(&err);
                g_free(debug_info);
                terminate = TRUE;
                break;
            case GST_MESSAGE_EOS:
                g_print("End of stream reached.\n");
                terminate = TRUE;
                break;
            case GST_MESSAGE_STATE_CHANGED:
                /* 파이프라인의 상태 변경 메시지에만 관심이 있습니다. */
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data.pipeline)) {
                    GstState old_state, new_state, pending_state;
                    gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                    g_print("Pipeline state changed from %s to %s:\n",
                        gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
                }
                break;
            default:
                /* 여기에는 도달하지 않아야 합니다. */
                g_printerr("Unexpected message received.\n");
                break;
            }
            gst_message_unref(msg);
        }
    } while (!terminate);

    /* 리소스 해제 */
    gst_object_unref(bus);
    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    gst_object_unref(data.pipeline);
    return 0;
}

/* 이 함수는 pad-added 시그널에 의해 호출됩니다. */
static void pad_added_handler(GstElement* src, GstPad* new_pad, CustomData* data) {
    GstPad* sink_pad = gst_element_get_static_pad(data->convert, "sink");
    GstPadLinkReturn ret;
    GstCaps* new_pad_caps = NULL;
    GstStructure* new_pad_struct = NULL;
    const gchar* new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* 컨버터가 이미 연결되어 있다면 여기서 할 일은 없습니다. */
    if (gst_pad_is_linked(sink_pad)) {
        g_print("Already linked. Ignoring.\n");
        goto exit;
    }

    /* 새로운 패드의 유형 확인 */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (!g_str_has_prefix(new_pad_type, "audio/x-raw")) {
        g_print("Type is '%s', but not raw audio. Ignoring.\n", new_pad_type);
        goto exit;
    }

    /* 연결 시도 */
    ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print("Type is '%s', but linking failed.\n", new_pad_type);
    }
    else {
        g_print("Link successful (type '%s').\n", new_pad_type);
    }

exit:
    /* 새로운 패드의 capabilities를 얻었다면 해제합니다. */
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);

    /* 싱크 패드 언레퍼런스 */
    gst_object_unref(sink_pad);
}