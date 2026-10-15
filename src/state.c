/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The current tree, the scan thread and the panel's view of both. */
#include "state.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "scan.h"
#include "snap.h"
#include "tree.h"

#define MAP_MAX_NODES 5000

static ds_config_t g_cfg;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static tree_t g_tree;      /* the tree the panel shows; replaced only by the scan thread */
static int g_have;         /* g_tree holds a tree */
static int g_scanning;
static int g_joinable;     /* main thread only */
static volatile int g_cancel;
static uint32_t g_gen; /* changes with every new tree; starts from the clock, see ds_init */
static uint64_t g_entries;
static char g_path[512], g_msg[200];
static pthread_t g_th;

static void say(const char *msg) {
    printf("[scan] %s\n", msg);
    if (g_cfg.notify)
        g_cfg.notify(msg);
}

static void size_text(uint64_t n, char *out, size_t cap) {
    static const char *const u[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)n;
    int i = 0;
    while (v >= 1024 && i < 4) {
        v /= 1024;
        i++;
    }
    snprintf(out, cap, i ? "%.1f %s" : "%.0f %s", v, u[i]);
}

void ds_init(const ds_config_t *cfg) {
    g_cfg = *cfg;
    /* not 1: a tab left open across a payload restart must get 409, never another tree's ids */
    g_gen = (uint32_t)time(NULL);
    tree_t t;
    if (cfg->snap_path && !snap_read(cfg->snap_path, &t)) {
        g_tree = t;
        g_have = 1;
        printf("[snap] loaded %u entries from %s\n", t.n, cfg->snap_path);
    } else if (cfg->snap_path) {
        printf("[snap] no valid snapshot at %s\n", cfg->snap_path);
    }
}

static void progress(void *ctx, uint64_t entries, const char *path) {
    (void)ctx;
    pthread_mutex_lock(&g_mu);
    g_entries = entries;
    snprintf(g_path, sizeof(g_path), "%s", path);
    pthread_mutex_unlock(&g_mu);
}

static void *scan_thread(void *arg) {
    (void)arg;
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = sched_get_priority_min(SCHED_OTHER);
    int prc = pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);
    printf("[scan] thread priority %d (rc %d)\n", sp.sched_priority, prc);

    char **skip = NULL;
    int nskip = g_cfg.skip_list ? g_cfg.skip_list(&skip) : 0;
    tree_t t;
    memset(&t, 0, sizeof(t));
    t.max_bytes = g_cfg.max_bytes;
    scan_opt_t o;
    memset(&o, 0, sizeof(o));
    o.root = g_cfg.root;
    o.skip = (const char *const *)skip;
    o.nskip = nskip;
    o.cancel = &g_cancel;
    o.progress = progress;
    o.pause_us = g_cfg.pause_us;
    int rc = scan_run(&o, &t);
    if (g_cfg.skip_free)
        g_cfg.skip_free(skip, nskip);

    char msg[200], size[32];
    if (rc == SCAN_OK) {
        tree_t old;
        pthread_mutex_lock(&g_mu);
        old = g_tree;
        g_tree = t;
        g_have = 1;
        g_gen++;
        pthread_mutex_unlock(&g_mu);
        if (old.nodes)
            tree_free(&old);
        /* no other thread changes g_tree while g_scanning is set */
        if (g_cfg.snap_path && snap_write(g_cfg.snap_path, &g_tree))
            printf("[snap] could not write %s\n", g_cfg.snap_path);
        size_text(g_tree.nodes[0].size, size, sizeof(size));
        snprintf(msg, sizeof(msg), "Scan done: %s in %u files, %llu s", size, g_tree.nodes[0].files,
                 (unsigned long long)(g_tree.duration_ms / 1000));
    } else {
        uint64_t read_n = t.n;
        tree_free(&t);
        if (rc == SCAN_CANCELLED)
            snprintf(msg, sizeof(msg), "Scan cancelled");
        else
            snprintf(msg, sizeof(msg), "Scan stopped: not enough memory (%llu entries read)", (unsigned long long)read_n);
    }
    pthread_mutex_lock(&g_mu);
    g_scanning = 0;
    snprintf(g_msg, sizeof(g_msg), "%s", msg);
    pthread_mutex_unlock(&g_mu);
    say(msg);
    return NULL;
}

int ds_scan_start(void) {
    pthread_mutex_lock(&g_mu);
    if (g_scanning) {
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    g_scanning = 1;
    g_cancel = 0;
    g_entries = 0;
    g_path[0] = 0;
    g_msg[0] = 0;
    pthread_mutex_unlock(&g_mu);
    ds_wait(); /* the previous thread has ended: release it */
    say("Scan started");
    if (pthread_create(&g_th, NULL, scan_thread, NULL)) {
        pthread_mutex_lock(&g_mu);
        g_scanning = 0;
        snprintf(g_msg, sizeof(g_msg), "Could not start the scan");
        pthread_mutex_unlock(&g_mu);
        return -1;
    }
    g_joinable = 1;
    return 0;
}

void ds_scan_cancel(void) { g_cancel = 1; }

void ds_wait(void) {
    if (g_joinable) {
        pthread_join(g_th, NULL);
        g_joinable = 0;
    }
}

void ds_status_json(buf_t *out) {
    pthread_mutex_lock(&g_mu);
    buf_printf(out, "{\"state\":\"%s\",\"gen\":%u,\"has_tree\":%s", g_scanning ? "scanning" : "idle", g_gen,
               g_have ? "true" : "false");
    if (g_have) {
        const node_t *r = &g_tree.nodes[0];
        buf_printf(out, ",\"time\":%lld,\"duration_ms\":%llu,\"size\":%llu,\"files\":%u,\"dirs\":%u,\"errors\":%u",
                   (long long)g_tree.time, (unsigned long long)g_tree.duration_ms, (unsigned long long)r->size, r->files,
                   r->dirs, g_tree.errors);
    }
    buf_printf(out, ",\"entries\":%llu,\"path\":", (unsigned long long)g_entries);
    buf_json_str(out, g_path);
    BUF_LIT(out, ",\"message\":");
    buf_json_str(out, g_msg);
    BUF_LIT(out, "}");
    pthread_mutex_unlock(&g_mu);
}

static int check_id(uint32_t id, uint32_t gen) {
    if (!g_have)
        return 503;
    if (gen != g_gen)
        return 409;
    return id < g_tree.n ? 200 : 404;
}

int ds_node_json(uint32_t id, uint32_t gen, uint32_t limit, buf_t *out) {
    pthread_mutex_lock(&g_mu);
    int code = check_id(id, gen);
    if (code == 200)
        tree_node_json(&g_tree, id, limit, out);
    pthread_mutex_unlock(&g_mu);
    return code;
}

int ds_map_json(uint32_t id, uint32_t gen, int depth, buf_t *out) {
    pthread_mutex_lock(&g_mu);
    int code = check_id(id, gen);
    if (code == 200 && tree_map_json(&g_tree, id, depth, MAP_MAX_NODES, out))
        code = 503;
    pthread_mutex_unlock(&g_mu);
    return code;
}
