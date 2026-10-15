/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Web panel server, one request per connection. */
#include "web.h"

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "buf.h"
#include "page_data.h"
#include "state.h"

#define JSON "application/json; charset=utf-8"
#define JS "application/javascript; charset=utf-8"
#define HTML "text/html; charset=utf-8"

int web_listen(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(WEB_PORT);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (s < 0)
        return -1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 8)) {
        close(s);
        return -1;
    }
    return s;
}

/* Writes until done or until deadline (each write waits at most 1 s, SO_SNDTIMEO). */
static void send_all(int fd, const void *p, size_t n, time_t deadline) {
    const char *c = p;
    while (n && time(NULL) < deadline) {
        ssize_t w = write(fd, c, n);
        if (w <= 0)
            return;
        c += w;
        n -= (size_t)w;
    }
}

static const char *status_text(int code) {
    switch (code) {
    case 200: return "200 OK";
    case 404: return "404 Not Found";
    case 409: return "409 Conflict";
    case 503: return "503 Service Unavailable";
    default: return "400 Bad Request";
    }
}

static void reply(int fd, int code, const char *type, const void *body, size_t len) {
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     status_text(code), type, len);
    time_t deadline = time(NULL) + WEB_SEND_TIMEOUT;
    send_all(fd, head, (size_t)n, deadline);
    send_all(fd, body, len, deadline);
}

static void reply_buf(int fd, int code, buf_t *b) {
    reply(fd, code, JSON, b->data ? (const char *)b->data : "", b->len);
    buf_free(b);
}

static void reply_msg(int fd, int code, const char *msg) {
    buf_t b = {0};
    buf_printf(&b, "{\"ok\":%s,\"message\":", code == 200 ? "true" : "false");
    buf_json_str(&b, msg);
    BUF_LIT(&b, "}");
    reply_buf(fd, code, &b);
}

/* Number after key= in the query of path; def when missing or not a number. */
static unsigned long qnum(const char *path, const char *key, unsigned long def) {
    const char *p = strchr(path, '?');
    size_t kl = strlen(key);
    while (p) {
        p++;
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            if (p[kl + 1] < '0' || p[kl + 1] > '9')
                return def;
            return strtoul(p + kl + 1, NULL, 10);
        }
        p = strchr(p, '&');
    }
    return def;
}

static unsigned long clamp(unsigned long v, unsigned long lo, unsigned long hi) { return v < lo ? lo : v > hi ? hi : v; }

static const char *error_text(int code) {
    return code == 409 ? "The scan changed: reload" : code == 404 ? "No such entry" : "No scan yet";
}

void web_serve(int listen_fd) {
    int fd = accept(listen_fd, NULL, NULL);
    if (fd >= 0)
        web_handle(fd);
}

void web_handle(int fd) {
    struct timeval tv = {5, 0}, out = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    /* a client that stops reading (a suspended browser) must not hold the only server thread */
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &out, sizeof(out));
    char req[4096], method[8] = "", path[512] = "", route[512];
    ssize_t n = read(fd, req, sizeof(req) - 1);
    if (n <= 0) {
        close(fd);
        return;
    }
    req[n] = 0;
    if (sscanf(req, "%7s %511s", method, path) != 2) {
        reply_msg(fd, 400, "Bad request");
        close(fd);
        return;
    }
    size_t rl = strcspn(path, "?");
    memcpy(route, path, rl);
    route[rl] = 0;
    int get = !strcmp(method, "GET"), post = !strcmp(method, "POST");
    /* a missing, invalid or too large id never matches a node */
    unsigned long id = qnum(path, "id", 0xffffffffUL), gen = qnum(path, "gen", 0);
    if (id > 0xffffffffUL)
        id = 0xffffffffUL;

    if (get && !strcmp(route, "/"))
        reply(fd, 200, HTML, index_html, sizeof(index_html));
    else if (get && !strcmp(route, "/legacy"))
        reply(fd, 200, HTML, legacy_html, sizeof(legacy_html));
    else if (get && !strcmp(route, "/app.js"))
        reply(fd, 200, JS, app_js, sizeof(app_js));
    else if (get && !strcmp(route, "/treemap.js"))
        reply(fd, 200, JS, treemap_js, sizeof(treemap_js));
    else if (get && !strcmp(route, "/api/status")) {
        buf_t b = {0};
        ds_status_json(&b);
        reply_buf(fd, 200, &b);
    } else if (post && !strcmp(route, "/api/scan")) {
        int busy = ds_scan_start(); /* post */
        reply_msg(fd, busy ? 409 : 200, busy ? "A scan is already running" : "Scan started");
    } else if (post && !strcmp(route, "/api/scan/cancel")) {
        ds_scan_cancel(); /* post */
        reply_msg(fd, 200, "Cancelling the scan");
    } else if (get && !strcmp(route, "/api/node")) {
        buf_t b = {0};
        int code = ds_node_json((uint32_t)id, (uint32_t)gen, (uint32_t)clamp(qnum(path, "limit", 200), 0, 1000), &b);
        if (code == 200)
            reply_buf(fd, 200, &b);
        else
            reply_msg(fd, code, error_text(code));
    } else if (get && !strcmp(route, "/api/map")) {
        buf_t b = {0};
        int code = ds_map_json((uint32_t)id, (uint32_t)gen, (int)clamp(qnum(path, "depth", 3), 1, 6), &b);
        if (code == 200)
            reply_buf(fd, 200, &b);
        else
            reply_msg(fd, code, error_text(code));
    } else {
        reply(fd, 404, "text/plain", "not found\n", 10);
    }
    close(fd);
}
