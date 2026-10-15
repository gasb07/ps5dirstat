/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Walks a directory tree into a tree_t. */
#include "scan.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* (dev, ino) of files with more than one link, so each is counted once. */
typedef struct {
    uint64_t dev, ino;
} link_key_t;

typedef struct {
    link_key_t *s;
    size_t n, cap; /* cap is a power of two */
} links_t;

static link_key_t *slot(link_key_t *s, size_t cap, uint64_t dev, uint64_t ino) {
    size_t i = (size_t)((dev * 0x9e3779b97f4a7c15ull) ^ (ino * 0xc2b2ae3d27d4eb4full)) & (cap - 1);
    while ((s[i].dev || s[i].ino) && (s[i].dev != dev || s[i].ino != ino))
        i = (i + 1) & (cap - 1);
    return &s[i];
}

/* 1 when seen before, 0 when recorded now, -1 out of memory. */
static int links_seen(links_t *l, uint64_t dev, uint64_t ino) {
    if ((l->n + 1) * 2 > l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 1024;
        link_key_t *s = calloc(cap, sizeof(*s));
        if (!s)
            return -1;
        for (size_t i = 0; i < l->cap; i++)
            if (l->s[i].dev || l->s[i].ino)
                *slot(s, cap, l->s[i].dev, l->s[i].ino) = l->s[i];
        free(l->s);
        l->s = s;
        l->cap = cap;
    }
    link_key_t *k = slot(l->s, l->cap, dev, ino);
    if (k->dev == dev && k->ino == ino && (dev || ino))
        return 1;
    k->dev = dev;
    k->ino = ino;
    l->n++;
    return 0;
}

typedef struct {
    const scan_opt_t *o;
    tree_t *t;
    links_t links;
    char path[PATH_MAX];
    uint64_t entries;
    int status;
} ctx_t;

static int skipped(const ctx_t *c) {
    for (int i = 0; i < c->o->nskip; i++)
        if (!strcmp(c->o->skip[i], c->path))
            return 1;
    return 0;
}

static void tick(ctx_t *c) {
    if (++c->entries % SCAN_TICK)
        return;
    if (c->o->progress)
        c->o->progress(c->o->ctx, c->entries, c->path);
    if (c->o->cancel && *c->o->cancel)
        c->status = SCAN_CANCELLED;
    if (c->o->pause_us)
        usleep(c->o->pause_us);
}

/* Reads directory id, whose path (len bytes) is in c->path, then goes into its
 * subdirectories. The directory is closed before going down: open descriptors never
 * grow with the depth. */
static void walk(ctx_t *c, uint32_t id, size_t len, int depth) {
    int fd = open(c->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *d = fd >= 0 ? fdopendir(fd) : NULL;
    if (!d) {
        c->t->nodes[id].err = errno ? (uint32_t)errno : EIO;
        if (fd >= 0)
            close(fd);
        return;
    }
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(d);
        if (!e) {
            if (errno)
                c->t->nodes[id].err = (uint32_t)errno;
            break;
        }
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        struct stat st;
        uint32_t type = NODE_FILE, err = 0;
        uint64_t size = 0;
        if (fstatat(dirfd(d), e->d_name, &st, AT_SYMLINK_NOFOLLOW))
            err = (uint32_t)errno; /* gone since readdir, or no search permission */
        else if (S_ISDIR(st.st_mode))
            type = NODE_DIR;
        else if (S_ISREG(st.st_mode)) {
            int seen = st.st_nlink > 1 ? links_seen(&c->links, (uint64_t)st.st_dev, (uint64_t)st.st_ino) : 0;
            if (seen < 0) {
                c->status = SCAN_NOMEM;
                break;
            }
            size = seen ? 0 : (uint64_t)st.st_size;
        } else if (S_ISLNK(st.st_mode))
            size = (uint64_t)st.st_size;
        if (tree_add(c->t, id, e->d_name, type, size, err) == TREE_NONE) {
            c->status = SCAN_NOMEM;
            break;
        }
        tick(c);
        if (c->status != SCAN_OK)
            break;
    }
    closedir(d);
    for (uint32_t ch = c->t->nodes[id].child; ch != TREE_NONE && c->status == SCAN_OK; ch = c->t->nodes[ch].next) {
        if (c->t->nodes[ch].type != NODE_DIR || c->t->nodes[ch].err)
            continue;
        const char *name = tree_name(c->t, ch);
        size_t nl = strlen(name), at = c->path[len - 1] == '/' ? len : len + 1;
        if (depth + 1 >= SCAN_MAX_DEPTH || at + nl + 1 > sizeof(c->path)) {
            c->t->nodes[ch].err = ENAMETOOLONG;
            continue;
        }
        c->path[len] = '/';
        memcpy(c->path + at, name, nl + 1);
        if (skipped(c))
            c->t->nodes[ch].type = NODE_SKIP;
        else
            walk(c, ch, at + nl, depth + 1); /* may move nodes and pool: nothing kept across it */
        c->path[len] = 0;
    }
}

int scan_run(const scan_opt_t *o, tree_t *t) {
    ctx_t c;
    memset(&c, 0, sizeof(c));
    c.o = o;
    c.t = t;
    struct timespec a, b;
    clock_gettime(CLOCK_MONOTONIC, &a);
    t->time = (int64_t)time(NULL);
    size_t len = strlen(o->root);
    if (!len || len >= sizeof(c.path) || tree_add(t, TREE_NONE, o->root, NODE_DIR, 0, 0) == TREE_NONE)
        return SCAN_NOMEM;
    memcpy(c.path, o->root, len + 1);
    walk(&c, 0, len, 0);
    free(c.links.s);
    if (c.status == SCAN_OK && tree_finish(t))
        c.status = SCAN_NOMEM;
    clock_gettime(CLOCK_MONOTONIC, &b);
    t->duration_ms = (uint64_t)((b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000);
    return c.status;
}
