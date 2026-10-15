/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * PS5 DirStat daemon: serves the disk usage panel and runs scans on request. */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "db.h"
#include "fw.h"
#include "icon_data.h"
#include "logts.h"
#include "mounts.h"
#include "state.h"
#include "tree.h"
#include "web.h"

#define VERSION "PS5 DirStat 0.1.0"
#define LOCK_PORT 9033
#define STATE_DIR "/data/ps5-dirstat"

typedef struct { char useless[45]; char message[3075]; } notify_request_t;
int sceUserServiceInitialize(void *);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void *);
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notify(const char *fmt, ...) {
    notify_request_t req;
    memset(&req, 0, sizeof(req));
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(req.message, sizeof(req.message), fmt, ap);
    va_end(ap);
    printf("[notify] %s\n", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void scan_note(const char *msg) { notify("%s", msg); }

static void mkdirs(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    mkdir(tmp, mode);
}

static int write_file(const char *path, const void *data, size_t len, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0)
        return -1;
    ssize_t w = write(fd, data, len);
    close(fd);
    return w == (ssize_t)len ? 0 : -1;
}

static int same_file(const char *path, const void *data, size_t size) {
    struct stat st;
    if (stat(path, &st) || (size_t)st.st_size != size)
        return 0;
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    uint8_t *tmp = malloc(size);
    int same = tmp && read(fd, tmp, size) == (ssize_t)size && !memcmp(tmp, data, size);
    free(tmp);
    close(fd);
    return same;
}

/* --- firmware: how the Media tab shortcut is registered (same rule as PS4Mount) --- */

typedef int (*title_dir_fn)(const char *title_id, const char *dir, void *reserved);
static title_dir_fn g_title_dir;
static int g_reg_mode = REG_ALL;
static char g_fw_text[16] = "unknown";

static void check_firmware(void) {
    uint32_t fw = kernel_get_fw_version();
    unsigned major, minor;
    fw_decode(fw, &major, &minor);
    fw_text(fw, g_fw_text, sizeof(g_fw_text));
    /* looked up at runtime: linking it would stop the payload from loading where it is gone */
    uint32_t h = 0;
    if (!kernel_dynlib_handle(getpid(), "libSceAppInstUtil.sprx", &h))
        g_title_dir = (title_dir_fn)kernel_dynlib_dlsym(getpid(), h, "sceAppInstUtilAppInstallTitleDir");
    g_reg_mode = fw_reg_mode(major, g_title_dir != NULL);
    printf("[main] firmware %s (0x%08x), registration mode %d\n", g_fw_text, fw, g_reg_mode);
}

static int register_call(const char *tid) {
    if (g_reg_mode == REG_TITLEDIR) {
        int rc = g_title_dir(tid, "/user/app/", NULL);
        return rc == (int)0x80990002 ? 0 : rc; /* already known */
    }
    return sceAppInstUtilAppInstallAll(NULL);
}

/* --- Media tab shortcut (category 65536 + deeplinkUri) --- */

#define PANEL_ID "DSTT00001"
static const char k_panel_param[] =
    "{\n"
    "    \"titleId\": \"" PANEL_ID "\",\n"
    "    \"applicationCategoryType\": 65536,\n"
    "    \"deeplinkUri\": \"http://127.0.0.1:8771/\",\n"
    "    \"localizedParameters\": {\n"
    "        \"defaultLanguage\": \"en-US\",\n"
    "        \"en-US\": {\n"
    "            \"titleName\": \"PS5 DirStat\"\n"
    "        }\n"
    "    }\n"
    "}\n";

/* Installs the shortcut when it is missing or out of date. */
static void install_panel_shortcut(void) {
    const char *param = "/user/app/" PANEL_ID "/sce_sys/param.json", *icon = "/user/app/" PANEL_ID "/sce_sys/icon0.png";
    int present = db_is_registered(PANEL_ID);
    int same_param = same_file(param, k_panel_param, sizeof(k_panel_param) - 1);
    int same_icon = same_file(icon, icon0_png, sizeof(icon0_png));
    printf("[panel] present=%d param=%d icon=%d\n", present, same_param, same_icon);
    if (present && same_param && same_icon)
        return;
    mkdirs("/user/app/" PANEL_ID "/sce_sys", 0755);
    if (write_file(param, k_panel_param, sizeof(k_panel_param) - 1, 0644) ||
        write_file(icon, icon0_png, sizeof(icon0_png), 0644)) {
        printf("[panel] shortcut files: %s\n", strerror(errno));
        return;
    }
    int rc = register_call(PANEL_ID);
    printf("[panel] shortcut install rc=0x%08x\n", rc);
    if (!rc)
        notify("PS5 DirStat added to the Media tab");
}

/* --- single instance and listening sockets --- */

static int single_instance(void) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(LOCK_PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 1)) {
        if (s >= 0)
            close(s);
        return -1;
    }
    return s;
}

/* Rest mode closes the sockets of payloads: a socket that stopped listening is opened again. */
static int still_listening(int s) {
    int on = 0;
    socklen_t n = sizeof(on);
    return s >= 0 && !getsockopt(s, SOL_SOCKET, SO_ACCEPTCONN, &on, &n) && on;
}

int main(void) {
    int lock = single_instance();
    if (lock < 0) {
        printf(VERSION " already running\n");
        return 0;
    }
    sceUserServiceInitialize(0);
    sceAppInstUtilInitialize();
    mkdirs(STATE_DIR, 0777);
    if (!log_start(STATE_DIR "/log.txt"))
        atexit(log_stop);
    setvbuf(stdout, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    printf("[main] " VERSION " starting\n");
    check_firmware();
    if (g_reg_mode != REG_NONE)
        install_panel_shortcut();

    ds_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.root = "/";
    cfg.snap_path = STATE_DIR "/last.bin";
    cfg.pause_us = 1000;
    cfg.max_bytes = TREE_MAX_BYTES / 2; /* the previous tree stays loaded during a scan: two trees fit the cap */
    cfg.skip_list = mounts_skip_list;
    cfg.skip_free = mounts_free;
    cfg.notify = scan_note;
    ds_init(&cfg);

    int web = web_listen();
    notify(web >= 0 ? VERSION " running. Panel in the Media tab or http://<PS5 IP>:8771"
                    : VERSION " running (panel unavailable)");
    for (;;) {
        if (!still_listening(lock)) {
            close(lock);
            if ((lock = single_instance()) < 0) {
                notify(VERSION " stopped: another PS5 DirStat is running");
                return 0;
            }
        }
        if (!still_listening(web)) {
            if (web >= 0)
                close(web);
            web = web_listen();
            printf("[main] web panel %s\n", web >= 0 ? "listening again" : "unavailable");
        }
        if (web < 0) {
            sleep(1);
            continue;
        }
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(web, &fs);
        struct timeval tv = {1, 0};
        if (select(web + 1, &fs, NULL, NULL, &tv) > 0)
            web_serve(web);
    }
}
