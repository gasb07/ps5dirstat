/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Growing byte buffer and JSON string output. */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data; /* always NUL terminated after a put */
    size_t len, cap;
} buf_t;

void buf_put(buf_t *b, const void *data, size_t len);
void buf_printf(buf_t *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_free(buf_t *b);
/* s as a JSON string with its quotes. Control characters are escaped and bytes that are
 * not valid UTF-8 become U+FFFD, so any file name gives valid JSON. */
void buf_json_str(buf_t *b, const char *s);

/* A string literal without its NUL. */
#define BUF_LIT(b, s) buf_put((b), (s), sizeof(s) - 1)
