/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Growing byte buffer and JSON string output. */
#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void buf_put(buf_t *b, const void *data, size_t len) {
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1)
            cap *= 2;
        uint8_t *g = realloc(b->data, cap);
        if (!g)
            return; /* the output is cut, never corrupted */
        b->data = g;
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
}

void buf_printf(buf_t *b, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        buf_put(b, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

void buf_free(buf_t *b) {
    free(b->data);
    memset(b, 0, sizeof(*b));
}

/* Length of the valid UTF-8 sequence at p, 0 when it is not one. */
static int utf8_len(const unsigned char *p) {
    unsigned c = p[0];
    int n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    for (int i = 1; i < n; i++)
        if ((p[i] & 0xc0) != 0x80)
            return 0;
    if ((c == 0xe0 && p[1] < 0xa0) || (c == 0xed && p[1] >= 0xa0) || (c == 0xf0 && p[1] < 0x90) ||
        (c == 0xf4 && p[1] >= 0x90))
        return 0; /* overlong, surrogate or past U+10FFFF */
    return n;
}

void buf_json_str(buf_t *b, const char *s) {
    const unsigned char *p = (const unsigned char *)s;
    BUF_LIT(b, "\"");
    while (*p) {
        if (*p == '"' || *p == '\\') {
            char e[2] = {'\\', (char)*p++};
            buf_put(b, e, 2);
        } else if (*p < 0x20) {
            char e[8];
            snprintf(e, sizeof(e), "\\u%04x", *p++);
            buf_put(b, e, 6);
        } else if (*p < 0x80) {
            buf_put(b, p++, 1);
        } else {
            int n = utf8_len(p);
            if (n) {
                buf_put(b, p, (size_t)n);
                p += n;
            } else {
                BUF_LIT(b, "\xef\xbf\xbd");
                p++;
            }
        }
    }
    BUF_LIT(b, "\"");
}
