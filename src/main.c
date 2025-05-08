#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#define DEFAULT_RTSP_URI "http://cctvsec.ktict.co.kr/138//JTYQpiZnGi4tnbFrn9n6pIiSJcySItxTBwQWVCrVLclBVzg4Fkof3+g7F4ae9hmVxX5rvfUcP+jTHNPljaZSBMkjpQnnxVKaUQo+7ilJFQ="

typedef struct _CustomData {
    GstElement* pipeline;
    GstElement* uri_decode_bin;
    GstElement* video_tee;
    GstElement* video_queue_display;
    GstElement* video_convert_display;
    GstElement* video_sink_display;
    GstElement* video_queue_record;
    GstElement* video_valve;
    GstElement* video_convert_record;
    GstElement* video_encoder;
    GstElement* muxer;
    GstElement* file_sink;

    GMainLoop* loop;
    gboolean recording;
} CustomData;

// 함수 선언
static void pad_added_handler(GstElement* src, GstPad* new_pad, CustomData* data);
static gboolean bus_call(GstBus* bus, GstMessage* msg, CustomData* data);
static void start_recording(CustomData* data);
static void stop_recording(CustomData* data);
static gboolean handle_keyboard(GIOChannel* source, GIOCondition condition, CustomData* data);


int clipper_main(int argc, char* argv[]) {
    CustomData data;
    GstBus* bus;
    GIOChannel* io_stdin;
    const gchar* hls_uri = DEFAULT_RTSP_URI;

    // 초기화
    gst_init(&argc, &argv);
    memset(&data, 0, sizeof(data));
    data.recording = FALSE;

    // --- 1. 엘리먼트 생성 ---
    data.pipeline = gst_pipeline_new("hls-stream-clipper-pipeline");
    if (!data.pipeline) {
        g_printerr("Pipeline element could not be created.\n");
        return -1;
    }

    // uridecodebin 생성
    data.uri_decode_bin = gst_element_factory_make("uridecodebin", "uri-source-decoder");
    if (!data.uri_decode_bin) {
        g_printerr("uridecodebin element could not be created. Check core GStreamer installation.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    // URI 설정
    g_object_set(G_OBJECT(data.uri_decode_bin), "uri", hls_uri, NULL);

    data.video_tee = gst_element_factory_make("tee", "video_tee");

    // 재생 엘리먼트 생성
    data.video_queue_display = gst_element_factory_make("queue", "video_queue_display");
    data.video_convert_display = gst_element_factory_make("videoconvert", "video_convert_display");
    data.video_sink_display = gst_element_factory_make("autovideosink", "video_sink_display");

    // 녹화 엘리먼트 생성
    data.video_queue_record = gst_element_factory_make("queue", "video_queue_record");
    data.video_valve = gst_element_factory_make("valve", "video_valve");
    data.video_convert_record = gst_element_factory_make("videoconvert", "video_convert_record");
    data.video_encoder = gst_element_factory_make("x264enc", "video_encoder"); // x264enc는 -ugly 플러그인 필요 가능성 있음
    data.muxer = gst_element_factory_make("mp4mux", "muxer");
    data.file_sink = gst_element_factory_make("filesink", "file_sink");

    // 모든 필수 엘리먼트 생성 확인
    if (!data.video_tee || !data.video_queue_display || !data.video_convert_display || !data.video_sink_display ||
        !data.video_queue_record || !data.video_valve || !data.video_convert_record || !data.video_encoder ||
        !data.muxer || !data.file_sink) {
        g_printerr("Not all processing elements could be created. Check GStreamer plugin installations (e.g., -base, -good, -ugly).\n");
        gst_object_unref(data.pipeline); // uri_decode_bin도 unref됨
        return -1;
    }

    // 엘리먼트 속성 설정
    g_object_set(G_OBJECT(data.video_valve), "drop", TRUE, NULL);
    g_object_set(G_OBJECT(data.file_sink), "location", "result.mp4", NULL);

    // --- 2. 파이프라인에 엘리먼트 추가 ---
    gst_bin_add_many(GST_BIN(data.pipeline),
        data.uri_decode_bin, data.video_tee,
        data.video_queue_display, data.video_convert_display, data.video_sink_display,
        data.video_queue_record, data.video_valve, data.video_convert_record, data.video_encoder,
        data.muxer, data.file_sink,
        NULL);

    // --- 3. 엘리먼트 연결 ---
    // uridecodebin의 pad-added 시그널 연결 (동적 연결 처리)
    g_signal_connect(data.uri_decode_bin, "pad-added", G_CALLBACK(pad_added_handler), &data);

    // Tee 패드 요청 및 정적 연결 (Tee -> 큐)
    GstPadTemplate* tee_src_pad_template;
    GstPad* tee_video_pad1, * tee_video_pad2;
    GstPad* queue_display_sink_pad, * queue_record_sink_pad;

    tee_src_pad_template = gst_element_class_get_pad_template(GST_ELEMENT_GET_CLASS(data.video_tee), "src_%u");
    if (!tee_src_pad_template) {
        g_printerr("Unable to get Tee src pad template.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    // Tee -> 재생 큐 연결
    tee_video_pad1 = gst_element_request_pad(data.video_tee, tee_src_pad_template, NULL, NULL);
    g_print("Obtained request pad %s for display branch.\n", gst_pad_get_name(tee_video_pad1));
    queue_display_sink_pad = gst_element_get_static_pad(data.video_queue_display, "sink");
    if (!tee_video_pad1 || !queue_display_sink_pad ||
        gst_pad_link(tee_video_pad1, queue_display_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Video Tee to display queue could not be linked.\n");
        if (tee_video_pad1) gst_object_unref(tee_video_pad1);
        if (queue_display_sink_pad) gst_object_unref(queue_display_sink_pad);
        gst_object_unref(data.pipeline);
        return -1;
    }
    gst_object_unref(queue_display_sink_pad);

    // Tee -> 녹화 큐 연결
    tee_video_pad2 = gst_element_request_pad(data.video_tee, tee_src_pad_template, NULL, NULL);
    g_print("Obtained request pad %s for record branch.\n", gst_pad_get_name(tee_video_pad2));
    queue_record_sink_pad = gst_element_get_static_pad(data.video_queue_record, "sink");
    if (!tee_video_pad2 || !queue_record_sink_pad ||
        gst_pad_link(tee_video_pad2, queue_record_sink_pad) != GST_PAD_LINK_OK) {
        g_printerr("Video Tee to record queue could not be linked.\n");
        gst_object_unref(tee_video_pad1); // 이전에 성공한 pad도 해제해야 함
        if (tee_video_pad2) gst_object_unref(tee_video_pad2);
        if (queue_record_sink_pad) gst_object_unref(queue_record_sink_pad);
        gst_object_unref(data.pipeline);
        return -1;
    }
    gst_object_unref(queue_record_sink_pad);

    // 요청했던 Tee 패드 해제 (연결 후에는 필요 없음)
    gst_object_unref(tee_video_pad1);
    gst_object_unref(tee_video_pad2);

    // 나머지 정적 연결
    // 비디오 재생 브랜치
    if (!gst_element_link_many(data.video_queue_display, data.video_convert_display, data.video_sink_display, NULL)) {
        g_printerr("Video display elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    // 비디오 녹화 브랜치 (muxer까지)
    if (!gst_element_link_many(data.video_queue_record, data.video_valve, data.video_convert_record, data.video_encoder, data.muxer, NULL)) {
        g_printerr("Video recording elements (up to muxer) could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    // Muxer -> File Sink 연결
    // mp4mux는 보통 video/audio 패드를 동적으로 요청해야 함. 여기서는 간단한 link 시도.
    // 실제로는 encoder의 src 패드와 muxer의 video_%u 요청 패드를 연결해야 할 수 있음.
    if (!gst_element_link(data.muxer, data.file_sink)) {
        g_printerr("Muxer to filesink could not be linked. Might need specific pad linking for mp4mux.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }

    // --- 4. 메인 루프 및 버스 설정 ---
    data.loop = g_main_loop_new(NULL, FALSE);
    bus = gst_pipeline_get_bus(GST_PIPELINE(data.pipeline));
    gst_bus_add_watch(bus, (GstBusFunc)bus_call, &data);
    gst_object_unref(bus);

    // 표준 입력 처리 설정
    io_stdin = g_io_channel_unix_new(fileno(stdin));
    if (!io_stdin) {
        g_printerr("Could not create GIOChannel for stdin.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    // G_IO_HUP (hang-up) 조건도 감시하여 채널이 닫혔을 때 처리
    g_io_add_watch(io_stdin, G_IO_IN | G_IO_HUP, (GIOFunc)handle_keyboard, &data);


    // --- 5. 파이프라인 시작 ---
    g_print("Setting pipeline to PLAYING...\n");
    g_print("Using URI source: %s\n", hls_uri);
    g_print("Press 'r' to start/stop recording, 'q' to quit.\n");
    if (gst_element_set_state(data.pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        g_printerr("Unable to set the pipeline to the playing state.\n");
        gst_object_unref(data.pipeline);
        g_io_channel_unref(io_stdin);
        return -1;
    }


    // --- 6. 메인 루프 실행 ---
    g_print("Running...\n");
    g_main_loop_run(data.loop);

    // --- 7. 정리 ---
    g_print("Stopping pipeline...\n");
    // 입력 채널 감시 제거
    g_io_channel_shutdown(io_stdin, TRUE, NULL); // Ensure channel is closed before unref
    g_io_channel_unref(io_stdin);

    gst_element_set_state(data.pipeline, GST_STATE_NULL);
    g_print("Cleaning up...\n");
    g_main_loop_unref(data.loop);
    gst_object_unref(data.pipeline); // 파이프라인 해제 (포함된 엘리먼트들도 해제됨)

    return 0;
}

// pad_added_handler 수정: uridecodebin에서 나오는 raw 패드를 Tee에 연결
static void pad_added_handler(GstElement* src, GstPad* new_pad, CustomData* data) {
    GstPad* tee_sink_pad = NULL;
    GstPadLinkReturn ret;
    GstCaps* new_pad_caps = NULL;
    GstStructure* new_pad_struct = NULL;
    const gchar* new_pad_type = NULL;

    // src가 uridecodebin인지 확인 (디버깅 목적)
    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    // 패드 캡 가져오기 (uridecodebin은 이미 디코딩된 raw 포맷 제공)
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    if (!new_pad_caps) {
        new_pad_caps = gst_pad_query_caps(new_pad, NULL);
    }
    if (!new_pad_caps) {
        g_printerr("Could not get caps for new pad %s.\n", GST_PAD_NAME(new_pad));
        goto exit;
    }
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);

    // 비디오 패드 처리 (raw video)
    if (g_str_has_prefix(new_pad_type, "video/x-raw")) {
        // video_tee의 싱크 패드 가져오기
        tee_sink_pad = gst_element_get_static_pad(data->video_tee, "sink");
        if (!tee_sink_pad) {
            g_printerr("Could not get sink pad from video_tee.\n");
            goto exit;
        }
        // 이미 연결되어 있는지 확인
        if (gst_pad_is_linked(tee_sink_pad)) {
            g_print("Video Tee sink pad already linked. Ignoring new pad '%s'.\n", GST_PAD_NAME(new_pad));
            goto exit;
        }
        // 연결 시도
        ret = gst_pad_link(new_pad, tee_sink_pad);
        if (GST_PAD_LINK_FAILED(ret)) {
            g_printerr("Link failed for raw video pad: %s\n", gst_pad_link_get_name(ret));
        }
        else {
            g_print("Link succeeded for raw video pad (type '%s').\n", new_pad_type);
        }
    }
    else {
        g_print("Ignoring pad with type '%s'.\n", new_pad_type);
    }

exit:
    // 자원 해제
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);
    if (tee_sink_pad != NULL)
        gst_object_unref(tee_sink_pad);
}


// bus_call 구현 (이전과 동일)
static gboolean bus_call(GstBus* bus, GstMessage* msg, CustomData* data) {
    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
        g_print("End-of-stream\n");
        g_main_loop_quit(data->loop);
        break;
    case GST_MESSAGE_ERROR: {
        gchar* debug = NULL;
        GError* error = NULL;
        gst_message_parse_error(msg, &error, &debug);
        g_printerr("ERROR from element %s: %s\n", GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), error->message);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");
        g_error_free(error);
        g_free(debug);
        g_main_loop_quit(data->loop);
        break;
    }
    case GST_MESSAGE_WARNING: {
        gchar* debug = NULL;
        GError* error = NULL;
        gst_message_parse_warning(msg, &error, &debug);
        g_printerr("WARNING from element %s: %s\n", GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)), error->message);
        g_printerr("Debugging info: %s\n", (debug) ? debug : "none");
        g_error_free(error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_IS_PIPELINE(GST_MESSAGE_SRC(msg))) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            g_print("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name(old_state), gst_element_state_get_name(new_state));
        }
        break;
    }
    default:
        break;
    }
    return TRUE;
}

// start_recording 구현 (이전과 동일)
static void start_recording(CustomData* data) {
    if (!data->recording) {
        g_print("Starting recording...\n");
        g_object_set(G_OBJECT(data->video_valve), "drop", FALSE, NULL);
        // 오디오 Valve 제어 (필요시)
        data->recording = TRUE;
    }
}

// stop_recording 구현 (이전과 동일)
static void stop_recording(CustomData* data) {
    if (data->recording) {
        g_print("Stopping recording...\n");
        g_object_set(G_OBJECT(data->video_valve), "drop", TRUE, NULL);
        // 오디오 Valve 제어 (필요시)
        data->recording = FALSE;
    }
}

// handle_keyboard 구현 (이전과 동일, 입력 처리 강화)
static gboolean handle_keyboard(GIOChannel* source, GIOCondition condition, CustomData* data) {
    gchar* str = NULL;
    gsize len;
    GError* error = NULL;
    GIOStatus status;

    // 채널이 닫혔는지 확인 (예: Ctrl+D 입력)
    if (condition & G_IO_HUP) {
        g_print("Input channel closed (HUP). Quitting...\n");
        if (data->loop && g_main_loop_is_running(data->loop)) {
            g_main_loop_quit(data->loop);
        }
        return FALSE; // 소스 제거
    }

    // 입력 읽기
    status = g_io_channel_read_line(source, &str, &len, NULL, &error);

    if (status == G_IO_STATUS_NORMAL) {
        if (str) {
            // 개행 문자 제거
            str = g_strchomp(str);

            if (g_strcmp0(str, "r") == 0 || g_strcmp0(str, "R") == 0) {
                if (data->recording) stop_recording(data);
                else start_recording(data);
            }
            else if (g_strcmp0(str, "q") == 0 || g_strcmp0(str, "Q") == 0) {
                g_print("Quitting...\n");
                if (data->loop && g_main_loop_is_running(data->loop)) {
                    g_main_loop_quit(data->loop);
                }
                g_free(str);
                return FALSE; // 소스 제거
            }
            else if (strlen(str) > 0) { // 빈 입력 무시
                g_print("Unknown command: '%s'. Press 'r' to toggle recording, 'q' to quit.\n", str);
            }
            g_free(str);
        }
    }
    else if (status == G_IO_STATUS_ERROR) {
        g_printerr("Error reading input: %s\n", error->message);
        g_error_free(error);
        if (data->loop && g_main_loop_is_running(data->loop)) {
            g_main_loop_quit(data->loop);
        }
        return FALSE; // 소스 제거
    }
    else if (status == G_IO_STATUS_EOF) {
        g_print("Input channel closed (EOF). Quitting...\n");
        if (data->loop && g_main_loop_is_running(data->loop)) {
            g_main_loop_quit(data->loop);
        }
        return FALSE; // 소스 제거
    }

    // 소스 감시 유지
    return TRUE;
}

int main(int argc, char* argv[]) {
#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
    return gst_macos_main((GstMainFunc)clipper_main, argc, argv, NULL);
#else
    return clipper_main(argc, argv);
#endif
}