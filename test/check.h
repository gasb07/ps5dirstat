/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Check macro shared by the host tests. */
#pragma once

#include <stdio.h>

static int g_failed, g_checks;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        g_checks++;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            g_failed++;                                                                                                \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                   \
        }                                                                                                              \
    } while (0)

/* Prints the summary; exit code for main. */
static inline int check_done(const char *name) {
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failed);
    return g_failed != 0;
}
