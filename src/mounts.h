/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Mount points the scan does not enter. */
#pragma once

/* 1 for file system types without disk data of their own: devfs, procfs, fdescfs, tmpfs
 * (RAM) and nullfs (the sandbox mounts repeat /user and /system). */
int mounts_special(const char *fstype);
/* "/dev" plus the mount point of every special mount; each mount is logged with its type.
 * Returns the count and the list in *out (free with mounts_free). */
int mounts_skip_list(char ***out);
void mounts_free(char **list, int n);
