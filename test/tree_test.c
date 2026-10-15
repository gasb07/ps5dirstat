/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Tree sums, order, memory cap and the JSON of /api/node and /api/map. */
#include <string.h>

#include "check.h"
#include "tree.h"

static const char *json(buf_t *b) { return b->data ? (const char *)b->data : ""; }

/* /r: a/{x 10, y 30}, b 5, c/ (empty) */
static void sample(tree_t *t) {
    memset(t, 0, sizeof(*t));
    uint32_t r = tree_add(t, TREE_NONE, "/r", NODE_DIR, 0, 0);
    uint32_t a = tree_add(t, r, "a", NODE_DIR, 0, 0);
    tree_add(t, a, "x", NODE_FILE, 10, 0);
    tree_add(t, a, "y", NODE_FILE, 30, 0);
    tree_add(t, r, "b", NODE_FILE, 5, 0);
    tree_add(t, r, "c", NODE_DIR, 0, 0);
}

int main(void) {
    tree_t t;
    sample(&t);
    CHECK(t.n == 6);
    CHECK(tree_add(&t, TREE_NONE, "again", NODE_DIR, 0, 0) == TREE_NONE); /* one root */
    CHECK(tree_add(&t, 99, "orphan", NODE_FILE, 0, 0) == TREE_NONE);
    CHECK(tree_finish(&t) == 0);
    CHECK(t.nodes[0].size == 45 && t.nodes[0].files == 3 && t.nodes[0].dirs == 2);
    CHECK(t.nodes[1].size == 40 && t.nodes[1].files == 2 && t.nodes[1].dirs == 0);
    /* children by size: a (1), b (4), c (5); in a: y (3), x (2) */
    CHECK(t.nodes[0].child == 1 && t.nodes[1].next == 4 && t.nodes[4].next == 5 && t.nodes[5].next == TREE_NONE);
    CHECK(t.nodes[1].child == 3 && t.nodes[3].next == 2);

    buf_t b = {0};
    tree_path(&t, 3, &b);
    CHECK(!strcmp(json(&b), "/r/a/y"));
    buf_free(&b);

    tree_node_json(&t, 0, 200, &b);
    const char *s = json(&b);
    CHECK(strstr(s, "\"id\":0,\"name\":\"/r\",\"size\":45,\"files\":3,\"dirs\":2,\"type\":\"dir\",\"err\":0") == s + 1);
    CHECK(strstr(s, "\"path\":\"/r\""));
    CHECK(strstr(s, "\"ancestors\":[]"));
    const char *pa = strstr(s, "\"name\":\"a\""), *pb = strstr(s, "\"name\":\"b\""), *pc = strstr(s, "\"name\":\"c\"");
    CHECK(pa && pb && pc && pa < pb && pb < pc);
    CHECK(strstr(s, "\"others\":{\"count\":0,\"size\":0}"));
    buf_free(&b);

    tree_node_json(&t, 0, 1, &b);
    CHECK(strstr(json(&b), "\"others\":{\"count\":2,\"size\":5}"));
    buf_free(&b);

    tree_node_json(&t, 3, 0, &b);
    CHECK(strstr(json(&b), "\"ancestors\":[{\"id\":0,\"name\":\"/r\"},{\"id\":1,\"name\":\"a\"}]"));
    CHECK(strstr(json(&b), "\"children\":[]"));
    buf_free(&b);

    CHECK(tree_map_json(&t, 0, 1, 5000, &b) == 0);
    CHECK(strstr(json(&b), "\"n\":\"a\"") && strstr(json(&b), "\"n\":\"b\"") && !strstr(json(&b), "\"n\":\"x\""));
    CHECK(!strstr(json(&b), "\"n\":\"c\"")); /* size 0: no cell */
    buf_free(&b);
    CHECK(tree_map_json(&t, 0, 2, 5000, &b) == 0);
    CHECK(strstr(json(&b), "\"n\":\"x\""));
    buf_free(&b);
    tree_free(&t);

    /* a child under 1/1000 of its parent goes to "others" */
    memset(&t, 0, sizeof(t));
    tree_add(&t, TREE_NONE, "/", NODE_DIR, 0, 0);
    tree_add(&t, 0, "big", NODE_FILE, 1000000, 0);
    tree_add(&t, 0, "tiny", NODE_FILE, 10, 0);
    tree_finish(&t);
    tree_map_json(&t, 0, 3, 5000, &b);
    CHECK(strstr(json(&b), "\"s\":10,\"k\":1,\"o\":1") && !strstr(json(&b), "tiny"));
    buf_free(&b);
    tree_path(&t, 1, &b);
    CHECK(!strcmp(json(&b), "/big")); /* no double slash under "/" */
    buf_free(&b);
    tree_free(&t);

    /* node budget of the map */
    memset(&t, 0, sizeof(t));
    tree_add(&t, TREE_NONE, "/", NODE_DIR, 0, 0);
    for (int i = 0; i < 10; i++)
        tree_add(&t, 0, "f", NODE_FILE, 100, 0);
    tree_finish(&t);
    tree_map_json(&t, 0, 3, 4, &b);
    CHECK(strstr(json(&b), "\"s\":700,\"k\":7,\"o\":1"));
    buf_free(&b);
    tree_free(&t);

    /* memory cap: adds fail cleanly */
    memset(&t, 0, sizeof(t));
    t.max_bytes = 4096;
    uint32_t last = tree_add(&t, TREE_NONE, "/", NODE_DIR, 0, 0);
    int added = 0;
    while (last != TREE_NONE && added < 100000) {
        last = tree_add(&t, 0, "some-file-name", NODE_FILE, 1, 0);
        added++;
    }
    CHECK(last == TREE_NONE && added < 100000);
    CHECK((size_t)t.cap * sizeof(node_t) + t.pool_cap <= 4096);
    tree_free(&t);

    /* error count */
    memset(&t, 0, sizeof(t));
    tree_add(&t, TREE_NONE, "/", NODE_DIR, 0, 0);
    tree_add(&t, 0, "bad", NODE_DIR, 0, 13);
    tree_finish(&t);
    CHECK(t.errors == 1);
    tree_free(&t);
    return check_done("tree");
}
