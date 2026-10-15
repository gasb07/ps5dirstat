/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Panel server over a real socket, with a real scan of a temporary directory. */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "state.h"
#include "web.h"

static char g_resp[1 << 20];

static const char *ask(const char *req) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv))
        return "";
    if (write(sv[0], req, strlen(req)) < 0)
        perror("write");
    shutdown(sv[0], SHUT_WR);
    web_handle(sv[1]);
    size_t n = 0;
    ssize_t r;
    while (n < sizeof(g_resp) - 1 && (r = read(sv[0], g_resp + n, sizeof(g_resp) - 1 - n)) > 0)
        n += (size_t)r;
    g_resp[n] = 0;
    close(sv[0]);
    return g_resp;
}

static const char *get(const char *path) {
    char req[600];
    snprintf(req, sizeof(req), "GET %s HTTP/1.1\r\nHost: x\r\n\r\n", path);
    return ask(req);
}

static const char *post(const char *path) {
    char req[600];
    snprintf(req, sizeof(req), "POST %s HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n", path);
    return ask(req);
}

static int code(const char *resp, const char *c) {
    char want[32];
    snprintf(want, sizeof(want), "HTTP/1.1 %s", c);
    return !strncmp(resp, want, strlen(want));
}

int main(void) {
    const char *tmp = getenv("TEST_TMP");
    char root[1024], p[1200], snap[1200];
    snprintf(root, sizeof(root), "%s/http.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp(root))
        return 1;
    /* 600 files: the scan passes two pauses, so requests meet it running */
    for (int i = 0; i < 600; i++) {
        snprintf(p, sizeof(p), "%s/f%03d", root, i);
        int fd = open(p, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) {
            if (ftruncate(fd, i + 1))
                perror("ftruncate");
            close(fd);
        }
    }
    snprintf(snap, sizeof(snap), "%s.bin", root);

    ds_config_t cfg = {0};
    cfg.root = root;
    cfg.snap_path = snap;
    cfg.pause_us = 100000;
    ds_init(&cfg);

    CHECK(code(get("/"), "200") && strstr(g_resp, "<title>PS5 DirStat</title>"));
    CHECK(code(get("/legacy"), "200") && strstr(g_resp, "older firmwares"));
    CHECK(code(get("/app.js"), "200") && strstr(g_resp, "javascript"));
    CHECK(code(get("/treemap.js"), "200") && strstr(g_resp, "DSTreemap"));
    CHECK(code(get("/api/status"), "200") && strstr(g_resp, "\"state\":\"idle\"") && strstr(g_resp, "\"has_tree\":false"));
    CHECK(code(get("/api/node?id=0&gen=1"), "503"));
    CHECK(code(get("/api/scan"), "404")); /* state changes only on POST */
    CHECK(code(get("/nope"), "404"));
    CHECK(code(ask("GARBAGE\r\n\r\n"), "400"));

    CHECK(code(post("/api/scan"), "200"));
    CHECK(code(post("/api/scan"), "409"));
    CHECK(code(get("/api/status"), "200") && strstr(g_resp, "\"state\":\"scanning\""));
    ds_wait();
    CHECK(code(get("/api/status"), "200") && strstr(g_resp, "\"gen\":2") && strstr(g_resp, "\"files\":600"));
    struct stat st;
    CHECK(stat(snap, &st) == 0);

    CHECK(code(get("/api/node?id=0&gen=2&limit=3"), "200"));
    CHECK(strstr(g_resp, "\"name\":\"f599\",\"size\":600") && strstr(g_resp, "\"others\":{\"count\":597"));
    CHECK(code(get("/api/node?id=0&gen=1"), "409"));
    CHECK(code(get("/api/node?id=99999&gen=2"), "404"));
    CHECK(code(get("/api/node?id=-1&gen=2"), "404"));
    CHECK(code(get("/api/map?id=0&gen=2&depth=9"), "200") && strstr(g_resp, "\"c\":["));

    CHECK(code(post("/api/scan"), "200"));
    CHECK(code(post("/api/scan/cancel"), "200"));
    ds_wait();
    CHECK(code(get("/api/status"), "200") && strstr(g_resp, "\"gen\":2") && strstr(g_resp, "cancelled"));
    CHECK(code(get("/api/node?id=0&gen=2&limit=1"), "200")); /* previous tree still served */
    return check_done("http");
}
