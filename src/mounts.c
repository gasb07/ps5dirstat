/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Mount points the scan does not enter. */
#include "mounts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/ucred.h>
#endif

static const char *const k_special[] = {"devfs", "nullfs", "procfs", "fdescfs", "tmpfs"};

int mounts_special(const char *fstype) {
    for (size_t i = 0; i < sizeof(k_special) / sizeof(k_special[0]); i++)
        if (!strcmp(fstype, k_special[i]))
            return 1;
    return 0;
}

int mounts_skip_list(char ***out) {
    int n = 0, cap = 1;
    char **l = malloc(sizeof(*l));
    if (!l || !(l[0] = strdup("/dev"))) {
        free(l);
        *out = NULL;
        return 0;
    }
    n = 1;
#ifdef __FreeBSD__
    int m = getfsstat(NULL, 0, MNT_NOWAIT);
    struct statfs *fs = m > 0 ? calloc((size_t)m, sizeof(*fs)) : NULL;
    if (fs && (m = getfsstat(fs, (long)((size_t)m * sizeof(*fs)), MNT_NOWAIT)) > 0) {
        char **g = realloc(l, sizeof(*l) * (size_t)(m + 1));
        if (g) {
            l = g;
            cap = m + 1;
            for (int i = 0; i < m; i++) {
                int skip = mounts_special(fs[i].f_fstypename);
                printf("[mounts] %s on %s (%s)%s\n", fs[i].f_mntfromname, fs[i].f_mntonname, fs[i].f_fstypename,
                       skip ? " skipped" : "");
                if (skip && n < cap && (l[n] = strdup(fs[i].f_mntonname)))
                    n++;
            }
        }
    }
    free(fs);
#endif
    (void)cap;
    *out = l;
    return n;
}

void mounts_free(char **list, int n) {
    for (int i = 0; i < n; i++)
        free(list[i]);
    free(list);
}
