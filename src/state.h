/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The current tree, the scan thread and the panel's view of both. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

typedef struct {
    const char *root;      /* "/" on the console */
    const char *snap_path; /* NULL: nothing kept on disk */
    unsigned pause_us;     /* sleep every 256 entries */
    size_t max_bytes;      /* tree cap, 0 for the default */
    int (*skip_list)(char ***out); /* directories not entered, read at each scan start; NULL for none */
    void (*skip_free)(char **list, int n);
    void (*notify)(const char *msg); /* scan started / ended; NULL for none */
} ds_config_t;

/* Keeps cfg and loads the snapshot when there is a valid one. */
void ds_init(const ds_config_t *cfg);
/* 0 when a scan was started, -1 when one is running. */
int ds_scan_start(void);
void ds_scan_cancel(void);
/* Returns after the running scan, if any, has ended. */
void ds_wait(void);
void ds_status_json(buf_t *out);
/* HTTP status: 200 with the JSON in out, 503 without a tree, 409 for another gen, 404 for an unknown id. */
int ds_node_json(uint32_t id, uint32_t gen, uint32_t limit, buf_t *out);
int ds_map_json(uint32_t id, uint32_t gen, int depth, buf_t *out);
