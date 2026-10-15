/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The last scan on disk. */
#include "snap.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SNAP_MAGIC "DIRSTAT"
#define SNAP_VERSION 1

typedef struct {
    char magic[8];
    uint32_t version, node_size;
    uint64_t nodes, pool;
    int64_t time;
    uint64_t duration_ms;
} snap_head_t;

_Static_assert(sizeof(snap_head_t) == 48, "snapshot header layout");

static int write_all(int fd, const void *p, size_t n) {
    const char *c = p;
    while (n) {
        ssize_t w = write(fd, c, n);
        if (w <= 0)
            return -1;
        c += w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *p, size_t n) {
    char *c = p;
    while (n) {
        ssize_t r = read(fd, c, n);
        if (r <= 0)
            return -1;
        c += r;
        n -= (size_t)r;
    }
    return 0;
}

int snap_write(const char *path, const tree_t *t) {
    char tmp[1024];
    if ((size_t)snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= sizeof(tmp))
        return -1;
    snap_head_t h;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, SNAP_MAGIC, sizeof(SNAP_MAGIC));
    h.version = SNAP_VERSION;
    h.node_size = sizeof(node_t);
    h.nodes = t->n;
    h.pool = t->pool_len;
    h.time = t->time;
    h.duration_ms = t->duration_ms;
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    int bad = write_all(fd, &h, sizeof(h)) || write_all(fd, t->nodes, sizeof(node_t) * t->n) ||
              write_all(fd, t->pool, t->pool_len);
    bad |= fsync(fd) != 0;
    bad |= close(fd) != 0;
    if (bad || rename(tmp, path)) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* Every index in range, parents before children, each node linked once under its own
 * parent (which also rules out loops), names inside the pool. */
static int valid(const tree_t *t) {
    if (!t->pool_len || t->pool[t->pool_len - 1] || t->nodes[0].parent != TREE_NONE)
        return 0;
    uint8_t *seen = calloc(t->n, 1);
    if (!seen)
        return 0;
    int ok = 1;
    for (uint32_t i = 0; ok && i < t->n; i++) {
        const node_t *nd = &t->nodes[i];
        if (nd->name >= t->pool_len || nd->type > NODE_SKIP || (i && nd->parent >= i) ||
            (nd->child != TREE_NONE && nd->child >= t->n) || (nd->next != TREE_NONE && nd->next >= t->n))
            ok = 0;
        for (uint32_t c = nd->child; ok && c != TREE_NONE; c = t->nodes[c].next) {
            if (c >= t->n || seen[c] || t->nodes[c].parent != i)
                ok = 0;
            else
                seen[c] = 1;
        }
    }
    for (uint32_t i = 1; ok && i < t->n; i++)
        ok = seen[i];
    free(seen);
    return ok;
}

int snap_read(const char *path, tree_t *t) {
    memset(t, 0, sizeof(*t));
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    snap_head_t h;
    tree_t r;
    memset(&r, 0, sizeof(r));
    if (fstat(fd, &st) || read_all(fd, &h, sizeof(h)) || memcmp(h.magic, SNAP_MAGIC, sizeof(SNAP_MAGIC)) ||
        h.version != SNAP_VERSION || h.node_size != sizeof(node_t) || !h.nodes || h.nodes > TREE_MAX_BYTES / sizeof(node_t) ||
        !h.pool || h.pool > TREE_MAX_BYTES || (uint64_t)st.st_size != sizeof(h) + h.nodes * sizeof(node_t) + h.pool)
        goto bad;
    r.nodes = malloc(h.nodes * sizeof(node_t));
    r.pool = malloc(h.pool);
    if (!r.nodes || !r.pool || read_all(fd, r.nodes, h.nodes * sizeof(node_t)) || read_all(fd, r.pool, h.pool))
        goto bad;
    r.n = r.cap = (uint32_t)h.nodes;
    r.pool_len = r.pool_cap = h.pool;
    r.time = h.time;
    r.duration_ms = h.duration_ms;
    if (!valid(&r))
        goto bad;
    for (uint32_t i = 0; i < r.n; i++)
        r.errors += r.nodes[i].err != 0;
    close(fd);
    *t = r;
    return 0;
bad:
    close(fd);
    tree_free(&r);
    return -1;
}
