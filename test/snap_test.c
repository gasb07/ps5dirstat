/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Snapshot round trip, and every damaged file rejected without hanging. */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "check.h"
#include "snap.h"

static char g_file[1100];

/* Header (48 bytes), then nodes, then pool. */
#define HEAD 48

static void sample(tree_t *t) {
    memset(t, 0, sizeof(*t));
    tree_add(t, TREE_NONE, "/", NODE_DIR, 0, 0);
    uint32_t a = tree_add(t, 0, "a", NODE_DIR, 0, 0);
    tree_add(t, a, "x", NODE_FILE, 10, 0);
    tree_add(t, a, "y", NODE_FILE, 30, 0);
    tree_add(t, 0, "b", NODE_FILE, 5, 13);
    tree_finish(t);
    t->time = 1760000000;
    t->duration_ms = 1234;
}

/* Writes the sample, applies a change to the bytes at off, and checks it is rejected. */
static int rejected_after(off_t off, const void *bytes, size_t n) {
    tree_t t, r = {0};
    sample(&t);
    snap_write(g_file, &t);
    tree_free(&t);
    int fd = open(g_file, O_WRONLY);
    if (fd < 0 || pwrite(fd, bytes, n, off) != (ssize_t)n)
        return 0;
    close(fd);
    int bad = snap_read(g_file, &r) != 0 && r.nodes == NULL;
    tree_free(&r);
    return bad;
}

int main(void) {
    const char *tmp = getenv("TEST_TMP");
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/snap.XXXXXX", tmp ? tmp : "/tmp");
    if (!mkdtemp(dir))
        return 1;
    snprintf(g_file, sizeof(g_file), "%s/last.bin", dir);

    tree_t t, r = {0};
    sample(&t);
    CHECK(snap_write(g_file, &t) == 0);
    char tmpf[1200];
    snprintf(tmpf, sizeof(tmpf), "%s.tmp", g_file);
    CHECK(access(tmpf, F_OK) != 0); /* renamed into place */
    CHECK(snap_read(g_file, &r) == 0);
    CHECK(r.n == t.n && r.pool_len == t.pool_len);
    CHECK(!memcmp(r.nodes, t.nodes, sizeof(node_t) * t.n) && !memcmp(r.pool, t.pool, t.pool_len));
    CHECK(r.time == 1760000000 && r.duration_ms == 1234 && r.errors == 1);
    tree_free(&r);

    struct stat st;
    stat(g_file, &st);
    CHECK(truncate(g_file, st.st_size - 1) == 0);
    CHECK(snap_read(g_file, &r) != 0);

    CHECK(rejected_after(0, "X", 1)); /* magic */
    uint32_t v = 99;
    CHECK(rejected_after(8, &v, 4)); /* version */
    uint32_t big = 5;
    CHECK(rejected_after(HEAD + sizeof(node_t) * 2 + offsetof(node_t, parent), &big, 4)); /* parent after child */
    uint32_t self = 1;
    CHECK(rejected_after(HEAD + sizeof(node_t) * 1 + offsetof(node_t, next), &self, 4)); /* sibling loop */
    uint32_t far = 1u << 30;
    CHECK(rejected_after(HEAD + sizeof(node_t) * 3 + offsetof(node_t, name), &far, 4)); /* name past pool */
    char nonul = 'z';
    CHECK(rejected_after((off_t)(HEAD + sizeof(node_t) * t.n + t.pool_len - 1), &nonul, 1)); /* pool end */

    char missing[1200];
    snprintf(missing, sizeof(missing), "%s/none.bin", dir);
    CHECK(snap_read(missing, &r) != 0);
    snprintf(missing, sizeof(missing), "%s/no/such/dir.bin", dir);
    CHECK(snap_write(missing, &t) != 0);
    tree_free(&t);
    return check_done("snap");
}
