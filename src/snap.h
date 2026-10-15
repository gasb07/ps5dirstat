/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * The last scan on disk. */
#pragma once

#include "tree.h"

/* Writes t to path.tmp and renames it over path, so a crash never damages the previous
 * file. 0 on success. */
int snap_write(const char *path, const tree_t *t);
/* Reads path into t (empty before). The file is checked completely (sizes, indexes,
 * names, every node linked once under its parent) before it is used. 0 on success; -1
 * when missing or invalid, t left empty. */
int snap_read(const char *path, tree_t *t);
