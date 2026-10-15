/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Scanned tree: one node array plus one string pool, and its JSON for the panel. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#define TREE_NONE 0xffffffffu
#define TREE_MAX_BYTES ((size_t)256 << 20)

enum { NODE_FILE, NODE_DIR, NODE_SKIP }; /* NODE_SKIP: a directory that is not entered */

typedef struct {
    uint32_t parent, child, next; /* node indexes, TREE_NONE when absent */
    uint32_t name;                /* offset in the pool */
    uint64_t size;                /* bytes; for a directory, everything below it */
    uint32_t files, dirs;         /* below a directory, all levels */
    uint32_t type;                /* NODE_* */
    uint32_t err;                 /* errno when it could not be read, 0 when it could */
} node_t;

typedef struct {
    node_t *nodes;
    uint32_t n, cap;
    char *pool;
    size_t pool_len, pool_cap;
    size_t max_bytes;     /* cap for nodes + pool; 0 means TREE_MAX_BYTES */
    int64_t time;         /* scan start, unix seconds */
    uint64_t duration_ms; /* scan length */
    uint32_t errors;      /* nodes with err != 0, set by tree_finish */
} tree_t;

/* Adds a node under parent; the first node is the root (parent TREE_NONE). A child always
 * gets a higher index than its parent. Returns the index, or TREE_NONE when the parent is
 * invalid or the memory cap is reached. */
uint32_t tree_add(tree_t *t, uint32_t parent, const char *name, uint32_t type, uint64_t size, uint32_t err);
/* Sums sizes and counts into the directories, sorts children by size (largest first) and
 * counts errors. 0, or -1 when out of memory. */
int tree_finish(tree_t *t);
void tree_free(tree_t *t);
const char *tree_name(const tree_t *t, uint32_t id);
/* Appends the full path of id ("/user/app/x"). */
void tree_path(const tree_t *t, uint32_t id, buf_t *out);
/* JSON for /api/node: the node, its path and ancestors, up to limit children, the rest
 * summed in "others". */
void tree_node_json(const tree_t *t, uint32_t id, uint32_t limit, buf_t *out);
/* JSON for /api/map: up to depth levels below id and max_nodes nodes, picked level by
 * level. Children under 1/1000 of their parent, or past the budget, are merged into one
 * "others" cell. 0, or -1 when out of memory. */
int tree_map_json(const tree_t *t, uint32_t id, int depth, uint32_t max_nodes, buf_t *out);
