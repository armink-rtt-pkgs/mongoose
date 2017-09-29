/*
 * File      : mg_port.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2017, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017-08-19     armink       first version
 */

#include <rtthread.h>
#include <finsh.h>
#include <string.h>
#include <lwip/dhcp.h>
#include <lwip/init.h>
#include <lwip/netif.h>

#include "mongoose.h"

static rt_mutex_t mg_locker = NULL;

int gettimeofday(struct timeval *tp, void *ignore)
{

    time_t time;
    rt_device_t device;

    device = rt_device_find("rtc");
    if (device != RT_NULL)
    {
        rt_device_control(device, RT_DEVICE_CTRL_RTC_GET_TIME, &time);
        if (tp != RT_NULL)
        {
            tp->tv_sec = time;
        }
    }

    return 0;
}

/**
 * Mongoose 库移植功能初始化
 */
void mongoose_port_init() {
    if (!mg_locker) {
        mg_locker = rt_mutex_create("mg_lock", RT_IPC_FLAG_FIFO);
    }
}

void mgos_lock(void) {
    rt_mutex_take(mg_locker, RT_WAITING_FOREVER);
}

void mgos_unlock(void) {
    rt_mutex_release(mg_locker);
}

static void http_server_ev_handler(struct mg_connection *nc, int ev, void *ev_data) {
    if (ev == MG_EV_POLL)
        return;
    rt_kprintf("ev %d\r\n", ev);
    switch (ev) {
    case MG_EV_ACCEPT: {
        char addr[32];
        mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr),
        MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
        rt_kprintf("%p: Connection from %s\r\n", nc, addr);
        break;
    }
    case MG_EV_HTTP_REQUEST: {
        struct http_message *hm = (struct http_message *) ev_data;
        char addr[32];
        mg_sock_addr_to_str(&nc->sa, addr, sizeof(addr), MG_SOCK_STRINGIFY_IP | MG_SOCK_STRINGIFY_PORT);
        rt_kprintf("%p: %.*s %.*s\r\n", nc, (int) hm->method.len, hm->method.p, (int) hm->uri.len, hm->uri.p);
        mg_send_response_line(nc, 200, "Content-Type: text/html\r\n"
                "Connection: close");
        mg_printf(nc, "\r\n<h1>Hello, %s!</h1>\r\n"
                "You asked for %.*s\r\n", addr, (int) hm->uri.len, hm->uri.p);
        nc->flags |= MG_F_SEND_AND_CLOSE;
        break;
    }
    case MG_EV_RECV : {
        struct mbuf *io = &nc->recv_mbuf;
        rt_kprintf("recv data: %.*s\n", io->len, io->buf);
        break;
    }
    case MG_EV_CLOSE: {
        rt_kprintf("%p: Connection closed\r\n", nc);
        break;
    }
    }
}

static void http_client_ev_handler(struct mg_connection *c, int ev, void *p) {
    struct http_message *hm = (struct http_message *) p;
    switch (ev) {
    case MG_EV_HTTP_REPLY: {
        c->flags |= MG_F_CLOSE_IMMEDIATELY;
        rt_kprintf("%.*s\n", hm->message.len, hm->message.p);
        break;
    }
    case MG_EV_CLOSE: {
        rt_kprintf("session close\n");
        break;
    }
    }
}

/*
 * This is a callback invoked by Mongoose to signal that a poll is needed soon.
 * Since we're in a tight polling loop anyway (see below), we don't need to do
 * anything.
 */
void mg_lwip_mgr_schedule_poll(struct mg_mgr *mgr) {
}

static void thread_entry_mg_poll(void* parameter) {
    struct mg_mgr *mgr = (struct mg_mgr *) parameter;
    while (1) {
        mg_mgr_poll(mgr, 0);
        rt_thread_delay(rt_tick_from_millisecond(10));
    }
}

void cs_log_print_prefix(const char *func) {
    rt_kprintf("[mg:%s]", func);
}

void cs_log_printf(const char *fmt, ...) {
    static char log_buf[RT_CONSOLEBUF_SIZE];
    va_list args;

    /* args point to the first variable parameter */
    va_start(args, fmt);
    /* must use vprintf to print */
    rt_vsprintf(log_buf, fmt, args);
    rt_kprintf("%s\n", log_buf);
    va_end(args);
}

static void mg(uint8_t argc, char **argv) {

enum {
    CMD_HTTP_INDEX,
};

    size_t i = 0;
    static struct mg_mgr *mgr = NULL;
    static rt_thread_t test_thread = NULL;
    static struct mg_connection *conn = NULL;

    const char* help_info[] = {
            [CMD_HTTP_INDEX]    = "mg http <s:server|c:client <url>>       - start http `server` or http `client url` test",
    };

    if (argc < 2) {
        rt_kprintf("Usage:\n");
        for (i = 0; i < sizeof(help_info) / sizeof(char*); i++) {
            rt_kprintf("%s\n", help_info[i]);
        }
        rt_kprintf("\n");
    } else {
        const char *operator = argv[1];

        if (!strcmp(operator, "init")) {
        } else {
            /* create locker when first running */
            if (!mg_locker) {
                mg_locker = rt_mutex_create("mg_lock", RT_IPC_FLAG_FIFO);
            }
            /* delete last mongoose manager poll thread */
            if (test_thread) {
                rt_thread_delete(test_thread);
                /* wait 100 ticks for thread deleted finish */
                rt_thread_delay(100);
                test_thread = NULL;
            }

            /* create and initialize mongoose manager */
            {
                /* delete last mongoose manager */
                if (mgr) {
                    mg_mgr_free(mgr);
                    rt_free(mgr);
                    mgr = NULL;
                }
                mgr = rt_calloc(sizeof(struct mg_mgr), 1);
                if (mgr) {
                    mg_mgr_init(mgr, NULL);
                } else {
                    rt_kprintf("Warning: NO memory.\n");
                    return;
                }
            }

            if (!strcmp(operator, "http")) {
                if (argc < 3) {
                    rt_kprintf("Usage: %s.\n", help_info[CMD_HTTP_INDEX]);
                    return;
                } else if (!strcmp(argv[2], "s")) {
                    /* http server test */
                    const char *err;
                    struct mg_bind_opts opts = { NULL };

                    opts.error_string = &err;
                    conn = mg_bind_opt(mgr, ":80", http_server_ev_handler, opts);
                    if (conn == NULL) {
                        rt_kprintf("Failed to create listener: %s\r\n", err);
                        return;
                    }
                    mg_set_protocol_http_websocket(conn);
                    rt_kprintf("Server address: http://%s:%d/\r\n", inet_ntoa(conn->sa.sin.sin_addr), htons(conn->sa.sin.sin_port));
                } else if (!strcmp(argv[2], "c") && argc > 3) {
                    /* http client test */
                    mg_connect_http(mgr, http_client_ev_handler, argv[3], NULL, NULL);
                    rt_kprintf("Http client request url: %s\n", argv[3]);
                } else {
                    rt_kprintf("Usage: %s.\n", help_info[CMD_HTTP_INDEX]);
                    return;
                }
            } else if (!strcmp(operator, "ws")) {
            } else if (!strcmp(operator, "coap")) {
            } else if (!strcmp(operator, "mqtt")) {
            } else {
                rt_kprintf("Usage:\n");
                for (i = 0; i < sizeof(help_info) / sizeof(char*); i++) {
                    rt_kprintf("%s\n", help_info[i]);
                }
                rt_kprintf("\n");
            }

            test_thread = rt_thread_create("mg_test_poll", thread_entry_mg_poll, mgr, 2048, 8, 10);
            if (test_thread != NULL) {
                rt_thread_startup(test_thread);
            } else {
                rt_kprintf("Warning: NO memory.\n");
            }
        }
    }
}
MSH_CMD_EXPORT(mg, Mongoose Test);
