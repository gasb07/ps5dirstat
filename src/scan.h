/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Walks a directory tree into a tree_t. */
#pragma once

#include <stdint.h>

#include "tree.h"

#define SCAN_MAX_DEPTH 256
#define SCAN_TICK 256 /* entries between progress reports, cancel checks and pauses */

enum { SCAN_OK, SCAN_CANCELLED, SCAN_NOMEM };

typedef struct {
    const char *root;              /* "/" on the console */
    const char *const *skip;       /* directories shown but not entered */
    int nskip;
    volatile int *cancel;          /* set to stop; NULL for none */
    void (*progress)(void *ctx, uint64_t entries, const char *path);
    void *ctx;
    unsigned pause_us;             /* sleep every SCAN_TICK entries */
} scan_opt_t;

/* Fills t (empty, max_bytes may be set) and finishes it on SCAN_OK. Symlinks are never
 * followed; a file with several hard links is counted once. On any other result the
 * caller frees t. */
int scan_run(const scan_opt_t *o, tree_t *t);
