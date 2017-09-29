// Copyright (c) 2015 Cesanta Software Limited
// All rights reserved

#define LOG_TAG    "mg.demo.cc4"

#include <elog.h>
#include <finsh.h>
#include "mongoose.h"

struct device_settings {
    char setting1[100];
    char setting2[100];
};

static const char *s_http_port = "8000";
static struct mg_serve_http_opts s_http_server_opts;
static struct device_settings s_settings = { "value1", "value2" };

static void handle_save(struct mg_connection *nc, struct http_message *hm) {
    // Get form variables and store settings values
    mg_get_http_var(&hm->body, "setting1", s_settings.setting1, sizeof(s_settings.setting1));
    mg_get_http_var(&hm->body, "setting2", s_settings.setting2, sizeof(s_settings.setting2));

    // Send response
    mg_http_send_redirect(nc, 302, mg_mk_str("/"), mg_mk_str(NULL));
}

static void handle_get_cpu_usage(struct mg_connection *nc) {
    // Generate random value, as an example of changing CPU usage
    // Getting real CPU usage depends on the OS.
    extern void cpu_usage_get(rt_uint8_t *major, rt_uint8_t *minor);
    float cpu_usage;
    uint8_t cpu_usage_major, cpu_usage_minor;

    cpu_usage_get(&cpu_usage_major, &cpu_usage_minor);
    cpu_usage = (float) cpu_usage_major + ((float) cpu_usage_minor) / 100.0f;

    // Use chunked encoding in order to avoid calculating Content-Length
    mg_printf(nc, "%s", "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");

    // Output JSON object which holds CPU usage data
    mg_printf_http_chunk(nc, "{ \"result\": %f }", cpu_usage);

    // Send empty chunk, the end of response
    mg_send_http_chunk(nc, "", 0);
}

static void handle_ssi_call(struct mg_connection *nc, const char *param) {
    if (strcmp(param, "setting1") == 0) {
        mg_printf_html_escape(nc, "%s", s_settings.setting1);
    } else if (strcmp(param, "setting2") == 0) {
        mg_printf_html_escape(nc, "%s", s_settings.setting2);
    }
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
    struct http_message *hm = (struct http_message *) ev_data;

    switch (ev) {
    case MG_EV_HTTP_REQUEST:
        if (mg_vcmp(&hm->uri, "/save") == 0) {
            handle_save(nc, hm);
        } else if (mg_vcmp(&hm->uri, "/get_cpu_usage") == 0) {
            handle_get_cpu_usage(nc);
        } else {
            mg_serve_http(nc, hm, s_http_server_opts);  // Serve static content
        }
        break;
    case MG_EV_SSI_CALL:
        handle_ssi_call(nc, ev_data);
        break;
    default:
        break;
    }
}

static void push_data_to_all_websocket_connections(struct mg_mgr *m) {
    struct mg_connection *c;
    rt_uint32_t total_mem, usage_memory, max_used_mem;

    rt_memory_info(&total_mem, &usage_memory, &max_used_mem);

    for (c = mg_next(m, NULL); c != NULL; c = mg_next(m, c)) {
        if (c->flags & MG_F_IS_WEBSOCKET) {
            mg_printf_websocket_frame(c, WEBSOCKET_OP_TEXT, "%d", usage_memory);
        }
    }
}

static void connected_device_entry(void *param) {
    struct mg_mgr mgr;
    struct mg_connection *nc;
    cs_stat_t st;

    mg_mgr_init(&mgr, NULL);
    nc = mg_bind(&mgr, s_http_port, ev_handler);
    if (nc == NULL) {
        log_e("Cannot bind to %s", s_http_port);
        exit(1);
    }

    // Set up HTTP server parameters
    mg_set_protocol_http_websocket(nc);
    s_http_server_opts.document_root = "web_root";  // Set up web root directory

    if (mg_stat(s_http_server_opts.document_root, &st) != 0) {
        log_e("%s", "Cannot find web_root directory, exiting...");
        exit(1);
    }

    log_i("Starting web server on port %s", s_http_port);
    for (;;) {
        static time_t last_time;
        time_t now = time(NULL);
        mg_mgr_poll(&mgr, 1000);
        if (now - last_time > 0) {
            push_data_to_all_websocket_connections(&mgr);
            last_time = now;
        }
        rt_thread_delay(rt_tick_from_millisecond(10));
    }
//    mg_mgr_free(&mgr);
}

static void mg_demo_cc4(uint8_t argc, char **argv) {
    /* 初始化完成 */
    static bool init_ok = false;

    if(init_ok) {
        rt_kprintf("already start Mongoose Demo: connected device 4\n");
        return;
    }

    rt_thread_t thread = rt_thread_create("mg_demo_cc4", connected_device_entry, NULL, 4096, 18, 25);
    if (thread) {
        rt_thread_startup(thread);
        /* 初始化完成 */
        init_ok = true;
    }
}
MSH_CMD_EXPORT(mg_demo_cc4, Mongoose Demo connected device 4);
