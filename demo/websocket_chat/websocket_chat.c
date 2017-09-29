/*
 * Copyright (c) 2014 Cesanta Software Limited
 * All rights reserved
 */

#define LOG_TAG    "mg.demo.wc"

#include <elog.h>
#include <finsh.h>

#include "mongoose.h"

static const char *s_http_port = "8001";

static int is_websocket(const struct mg_connection *nc) {
    return nc->flags & MG_F_IS_WEBSOCKET;
}

static void broadcast(struct mg_connection *nc, const struct mg_str msg) {
    struct mg_connection *c;
    char buf[500];
    char addr[32];
    mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);

    snprintf(buf, sizeof(buf), "%s %.*s", addr, (int) msg.len, msg.p);
    rt_kprintf("%s\n", buf); /* Local echo. */
    for (c = mg_next(nc->mgr, NULL); c != NULL; c = mg_next(nc->mgr, c)) {
        if (c == nc || (c->flags & MG_F_LISTENING) || !is_websocket(nc))
            continue; /* Don't send to the sender or listen connection. */
        mg_send_websocket_frame(c, WEBSOCKET_OP_TEXT, buf, strlen(buf));
    }
}

static void ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
    switch (ev) {
    case MG_EV_WEBSOCKET_HANDSHAKE_DONE: {
        /* New websocket connection. Tell everybody. */
        broadcast(nc, mg_mk_str("++ joined"));
        break;
    }
    case MG_EV_WEBSOCKET_FRAME: {
        struct websocket_message *wm = (struct websocket_message *) ev_data;
        /* New websocket message. Tell everybody. */
        struct mg_str d = { (char *) wm->data, wm->size };
        broadcast(nc, d);
        break;
    }
    case MG_EV_CLOSE: {
        /* Disconnect. Tell everybody. */
        if (is_websocket(nc)) {
            broadcast(nc, mg_mk_str("-- left"));
        }
        break;
    }
    }
}

static void websocket_entry(void *param) {
    struct mg_mgr mgr;
    struct mg_connection *nc;

    mg_mgr_init(&mgr, NULL);

    nc = mg_bind(&mgr, s_http_port, ev_handler);
    mg_set_protocol_http_websocket(nc);

    rt_kprintf("Started on port %s\n", s_http_port);
    while (true) {
        mg_mgr_poll(&mgr, 200);
        rt_thread_delay(rt_tick_from_millisecond(200));
    }

//    mg_mgr_free(&mgr);
}

static void mg_demo_wc(uint8_t argc, char **argv) {
    /* 初始化完成 */
    static bool init_ok = false;

    if (init_ok) {
        rt_kprintf("already start Mongoose Demo: websocket chat\n");
        return;
    }

    rt_thread_t thread = rt_thread_create("mg_demo_wc", websocket_entry, NULL, 4096, 19, 25);
    if (thread) {
        rt_thread_startup(thread);
        /* 初始化完成 */
        init_ok = true;
    }
}
MSH_CMD_EXPORT(mg_demo_wc, Mongoose Demo websocket chat);
