/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The scan on a real directory: sizes, hardlinks, symlinks, skipped paths, unreadable
 * entries, depth limit, cancel and memory cap. Run with: bash test/run.sh */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "mounts.h"
#include "scan.h"

static char g_root[1024];

static void at(char *out, size_t n, const char *rel) { snprintf(out, n, "%s/%s", g_root, rel); }

static void put(const char *rel, off_t size) {
    char p[4096];
    at(p, sizeof(p), rel);
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        if (ftruncate(fd, size))
            perror("ftruncate");
        close(fd);
    }
}

static void dir(const char *rel) {
    char p[4096];
    at(p, sizeof(p), rel);
    mkdir(p, 0755);
}

static uint32_t find(const tree_t *t, uint32_t parent, const char *name) {
    for (uint32_t c = t->nodes[parent].child; c != TREE_NONE; c = t->nodes[c].next)
        if (!strcmp(tree_name(t, c), name))
            return c;
    return TREE_NONE;
}

static uint64_t g_progress;
static void progress(void *ctx, uint64_t entries, const char *path) {
    (void)ctx;
    (void)path;
    g_progress = entries;
}

int main(void) {
    const char *tmp = getenv("TEST_TMP");
    snprintf(g_root, sizeof(g_root), "%s/scan.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp(g_root)) {
        perror("mkdtemp");
        return 1;
    }
    int root_user = geteuid() == 0; /* root reads anything: the permission cases are skipped */
    char p[4096], q[4096];
    put("big.bin", 100000);
    dir("sub");
    put("sub/a.txt", 300);
    at(p, sizeof(p), "sub/a.txt");
    at(q, sizeof(q), "sub/hard");
    CHECK(link(p, q) == 0);
    at(q, sizeof(q), "link");
    CHECK(symlink("big.bin", q) == 0); /* 7 bytes, never followed */
    dir("skipme");
    put("skipme/f", 50);
    dir("locked");
    put("locked/f", 10);
    dir("noexec");
    put("noexec/f", 20);
    at(p, sizeof(p), "locked");
    chmod(p, 0);
    at(p, sizeof(p), "noexec");
    chmod(p, 0444); /* names readable, entries not */
    char rel[2048] = "deep";
    dir(rel);
    for (int i = 0; i < 299; i++) {
        strncat(rel, "/d", sizeof(rel) - strlen(rel) - 1);
        dir(rel);
    }

    char skip_path[1100];
    at(skip_path, sizeof(skip_path), "skipme");
    const char *skip[] = {skip_path};
    tree_t t = {0};
    scan_opt_t o = {0};
    o.root = g_root;
    o.skip = skip;
    o.nskip = 1;
    o.progress = progress;
    CHECK(scan_run(&o, &t) == SCAN_OK);
    CHECK(t.nodes[0].size == 100000 + 300 + 7);
    CHECK(g_progress >= 256);
    uint32_t sub = find(&t, 0, "sub"), link_n = find(&t, 0, "link"), sk = find(&t, 0, "skipme");
    CHECK(sub != TREE_NONE && t.nodes[sub].size == 300 && t.nodes[sub].files == 2);
    CHECK(link_n != TREE_NONE && t.nodes[link_n].type == NODE_FILE && t.nodes[link_n].size == 7);
    CHECK(sk != TREE_NONE && t.nodes[sk].type == NODE_SKIP && t.nodes[sk].child == TREE_NONE);
    if (!root_user) {
        uint32_t lk = find(&t, 0, "locked"), nx = find(&t, 0, "noexec");
        CHECK(lk != TREE_NONE && t.nodes[lk].err == EACCES);
        uint32_t nf = nx == TREE_NONE ? TREE_NONE : find(&t, nx, "f");
        CHECK(nf != TREE_NONE && t.nodes[nf].err == EACCES && t.nodes[nf].size == 0);
        CHECK(t.errors >= 3);
    }
    /* depth: "deep" is level 1; the first node not entered is at level SCAN_MAX_DEPTH */
    uint32_t cur = find(&t, 0, "deep");
    int level = 1;
    while (cur != TREE_NONE && !t.nodes[cur].err) {
        cur = find(&t, cur, "d");
        level++;
    }
    CHECK(cur != TREE_NONE && t.nodes[cur].err == ENAMETOOLONG && level == SCAN_MAX_DEPTH);
    tree_free(&t);

    volatile int cancel = 1;
    o.cancel = &cancel;
    CHECK(scan_run(&o, &t) == SCAN_CANCELLED);
    tree_free(&t);
    o.cancel = NULL;

    t.max_bytes = 8192;
    CHECK(scan_run(&o, &t) == SCAN_NOMEM);
    tree_free(&t);

    CHECK(mounts_special("nullfs") && mounts_special("devfs") && mounts_special("tmpfs"));
    CHECK(!mounts_special("ufs") && !mounts_special("exfatfs"));
    char **list;
    int n = mounts_skip_list(&list);
    CHECK(n >= 1 && !strcmp(list[0], "/dev"));
    mounts_free(list, n);
    return check_done("scan");
}
