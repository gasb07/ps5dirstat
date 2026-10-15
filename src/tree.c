/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Scanned tree: one node array plus one string pool, and its JSON for the panel. */
#include "tree.h"

#include <stdlib.h>
#include <string.h>

#define MAX_CHAIN 512 /* deeper than the scan ever goes (256) */

static size_t limit_of(const tree_t *t) { return t->max_bytes ? t->max_bytes : TREE_MAX_BYTES; }

/* Bytes left for one array when the other keeps its size. */
static size_t room(const tree_t *t, size_t other) { return other < limit_of(t) ? limit_of(t) - other : 0; }

uint32_t tree_add(tree_t *t, uint32_t parent, const char *name, uint32_t type, uint64_t size, uint32_t err) {
    size_t len = strlen(name) + 1;
    if ((parent == TREE_NONE) != (t->n == 0) || (parent != TREE_NONE && parent >= t->n))
        return TREE_NONE;
    if (t->n == t->cap) {
        size_t max = room(t, t->pool_cap) / sizeof(node_t), cap = t->cap ? (size_t)t->cap * 2 : 4096;
        if (cap > max)
            cap = max;
        if (cap > 0xfffffff0u)
            cap = 0xfffffff0u;
        if (cap <= t->n)
            return TREE_NONE;
        node_t *g = realloc(t->nodes, cap * sizeof(node_t));
        if (!g)
            return TREE_NONE;
        t->nodes = g;
        t->cap = (uint32_t)cap;
    }
    if (t->pool_len + len > t->pool_cap) {
        size_t max = room(t, (size_t)t->cap * sizeof(node_t)), cap = t->pool_cap ? t->pool_cap * 2 : 65536;
        while (cap < t->pool_len + len)
            cap *= 2;
        if (cap > max)
            cap = max;
        if (cap < t->pool_len + len)
            return TREE_NONE;
        char *g = realloc(t->pool, cap);
        if (!g)
            return TREE_NONE;
        t->pool = g;
        t->pool_cap = cap;
    }
    uint32_t id = t->n++;
    node_t *nd = &t->nodes[id];
    memset(nd, 0, sizeof(*nd));
    nd->parent = parent;
    nd->child = TREE_NONE;
    nd->next = TREE_NONE;
    nd->name = (uint32_t)t->pool_len;
    nd->type = type;
    nd->size = size;
    nd->err = err;
    memcpy(t->pool + t->pool_len, name, len);
    t->pool_len += len;
    if (parent != TREE_NONE) {
        nd->next = t->nodes[parent].child;
        t->nodes[parent].child = id;
    }
    return id;
}

typedef struct {
    uint64_t size;
    uint32_t id;
} ent_t;

static int by_size(const void *a, const void *b) {
    const ent_t *x = a, *y = b;
    if (x->size != y->size)
        return x->size < y->size ? 1 : -1;
    return x->id < y->id ? -1 : x->id > y->id;
}

int tree_finish(tree_t *t) {
    t->errors = 0;
    /* children have higher indexes than their parents: one backward pass sums everything */
    for (uint32_t i = t->n; i-- > 0;) {
        node_t *c = &t->nodes[i];
        if (c->err)
            t->errors++;
        if (c->parent == TREE_NONE)
            continue;
        node_t *p = &t->nodes[c->parent];
        p->size += c->size;
        p->files += c->files + (c->type == NODE_FILE);
        p->dirs += c->dirs + (c->type != NODE_FILE);
    }
    ent_t *v = NULL;
    size_t cap = 0;
    for (uint32_t i = 0; i < t->n; i++) {
        size_t k = 0;
        for (uint32_t c = t->nodes[i].child; c != TREE_NONE; c = t->nodes[c].next) {
            if (k == cap) {
                size_t nc = cap ? cap * 2 : 256;
                ent_t *g = realloc(v, nc * sizeof(*g));
                if (!g) {
                    free(v);
                    return -1;
                }
                v = g;
                cap = nc;
            }
            v[k].size = t->nodes[c].size;
            v[k].id = c;
            k++;
        }
        if (k < 2)
            continue;
        qsort(v, k, sizeof(*v), by_size);
        t->nodes[i].child = v[0].id;
        for (size_t j = 0; j + 1 < k; j++)
            t->nodes[v[j].id].next = v[j + 1].id;
        t->nodes[v[k - 1].id].next = TREE_NONE;
    }
    free(v);
    return 0;
}

void tree_free(tree_t *t) {
    free(t->nodes);
    free(t->pool);
    memset(t, 0, sizeof(*t));
}

const char *tree_name(const tree_t *t, uint32_t id) { return t->pool + t->nodes[id].name; }

void tree_path(const tree_t *t, uint32_t id, buf_t *out) {
    uint32_t chain[MAX_CHAIN];
    int n = 0;
    for (uint32_t i = id; i != TREE_NONE && n < MAX_CHAIN; i = t->nodes[i].parent)
        chain[n++] = i;
    size_t start = out->len;
    for (int k = n - 1; k >= 0; k--) {
        const char *s = tree_name(t, chain[k]);
        if (k < n - 1 && out->len > start && out->data[out->len - 1] != '/')
            BUF_LIT(out, "/");
        buf_put(out, s, strlen(s));
    }
}

static const char *type_name(uint32_t type) {
    return type == NODE_DIR ? "dir" : type == NODE_SKIP ? "skip" : "file";
}

static void node_fields(const tree_t *t, uint32_t id, buf_t *out) {
    const node_t *nd = &t->nodes[id];
    buf_printf(out, "\"id\":%u,\"name\":", id);
    buf_json_str(out, tree_name(t, id));
    buf_printf(out, ",\"size\":%llu,\"files\":%u,\"dirs\":%u,\"type\":\"%s\",\"err\":%u", (unsigned long long)nd->size,
               nd->files, nd->dirs, type_name(nd->type), nd->err);
}

void tree_node_json(const tree_t *t, uint32_t id, uint32_t limit, buf_t *out) {
    BUF_LIT(out, "{");
    node_fields(t, id, out);
    buf_t path = {0};
    tree_path(t, id, &path);
    BUF_LIT(out, ",\"path\":");
    buf_json_str(out, path.data ? (const char *)path.data : "");
    buf_free(&path);
    BUF_LIT(out, ",\"ancestors\":[");
    uint32_t chain[MAX_CHAIN];
    int n = 0;
    for (uint32_t i = t->nodes[id].parent; i != TREE_NONE && n < MAX_CHAIN; i = t->nodes[i].parent)
        chain[n++] = i;
    for (int k = n - 1; k >= 0; k--) {
        buf_printf(out, "%s{\"id\":%u,\"name\":", k == n - 1 ? "" : ",", chain[k]);
        buf_json_str(out, tree_name(t, chain[k]));
        BUF_LIT(out, "}");
    }
    BUF_LIT(out, "],\"children\":[");
    uint32_t shown = 0, more = 0;
    uint64_t more_size = 0;
    for (uint32_t c = t->nodes[id].child; c != TREE_NONE; c = t->nodes[c].next) {
        if (shown < limit) {
            if (shown++)
                BUF_LIT(out, ",");
            BUF_LIT(out, "{");
            node_fields(t, c, out);
            BUF_LIT(out, "}");
        } else {
            more++;
            more_size += t->nodes[c].size;
        }
    }
    buf_printf(out, "],\"others\":{\"count\":%u,\"size\":%llu}}", more, (unsigned long long)more_size);
}

#define MAP_IN 1   /* in the map */
#define MAP_OPEN 2 /* its children were considered */

/* "," between children, ",\"c\":[" before the first. */
static void comma_or_open(buf_t *out, int shown) {
    if (shown)
        BUF_LIT(out, ",");
    else
        BUF_LIT(out, ",\"c\":[");
}

static void map_emit(const tree_t *t, uint32_t id, const uint8_t *mark, buf_t *out) {
    const node_t *nd = &t->nodes[id];
    buf_printf(out, "{\"id\":%u,\"n\":", id);
    buf_json_str(out, tree_name(t, id));
    buf_printf(out, ",\"s\":%llu,\"t\":\"%s\"", (unsigned long long)nd->size, type_name(nd->type));
    if (mark[id] & MAP_OPEN) {
        int shown = 0;
        uint32_t k = 0;
        uint64_t rest = 0;
        for (uint32_t c = nd->child; c != TREE_NONE; c = t->nodes[c].next) {
            if (mark[c] & MAP_IN) {
                comma_or_open(out, shown++);
                map_emit(t, c, mark, out);
            } else if (t->nodes[c].size) {
                k++;
                rest += t->nodes[c].size;
            }
        }
        if (k) {
            comma_or_open(out, shown++);
            buf_printf(out, "{\"n\":\"\",\"s\":%llu,\"k\":%u,\"o\":1}", (unsigned long long)rest, k);
        }
        if (shown)
            BUF_LIT(out, "]");
    }
    BUF_LIT(out, "}");
}

int tree_map_json(const tree_t *t, uint32_t id, int depth, uint32_t max_nodes, buf_t *out) {
    uint8_t *mark = calloc(t->n, 1);
    uint32_t *q = malloc(sizeof(*q) * ((size_t)max_nodes + 1));
    uint8_t *lvl = malloc((size_t)max_nodes + 1);
    if (!mark || !q || !lvl) {
        free(mark);
        free(q);
        free(lvl);
        return -1;
    }
    /* level by level, so a large first branch cannot use the whole budget */
    uint32_t head = 0, tail = 0, count = 1;
    mark[id] = MAP_IN;
    q[tail] = id;
    lvl[tail++] = 0;
    while (head < tail) {
        uint32_t p = q[head];
        int l = lvl[head++];
        if (l >= depth || t->nodes[p].type != NODE_DIR)
            continue;
        mark[p] |= MAP_OPEN;
        uint64_t min = t->nodes[p].size / 1000;
        for (uint32_t c = t->nodes[p].child; c != TREE_NONE && count < max_nodes; c = t->nodes[c].next) {
            if (!t->nodes[c].size || t->nodes[c].size < min)
                break; /* sorted by size: the rest is smaller */
            mark[c] = MAP_IN;
            q[tail] = c;
            lvl[tail++] = (uint8_t)(l + 1);
            count++;
        }
    }
    map_emit(t, id, mark, out);
    free(mark);
    free(q);
    free(lvl);
    return 0;
}
