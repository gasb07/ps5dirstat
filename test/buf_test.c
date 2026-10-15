/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 gasb07
 * Growing buffer and JSON strings: names from any drive must give valid JSON. */
#include <string.h>

#include "buf.h"
#include "check.h"

static int json_is(const char *in, const char *want) {
    buf_t b = {0};
    buf_json_str(&b, in);
    int ok = b.len == strlen(want) && !memcmp(b.data, want, b.len);
    if (!ok)
        printf("  got [%.*s] want [%s]\n", (int)b.len, b.data ? (const char *)b.data : "", want);
    buf_free(&b);
    return ok;
}

int main(void) {
    buf_t b = {0};
    BUF_LIT(&b, "ab");
    buf_printf(&b, "%d-%s", 42, "x");
    CHECK(b.len == 6 && !strcmp((const char *)b.data, "ab42-x"));
    for (int i = 0; i < 10000; i++)
        buf_put(&b, "z", 1);
    CHECK(b.len == 10006 && b.data[b.len] == 0);
    buf_free(&b);
    CHECK(b.data == NULL && b.len == 0);

    CHECK(json_is("plain", "\"plain\""));
    CHECK(json_is("q\"b\\", "\"q\\\"b\\\\\""));
    CHECK(json_is("\x01\x1f", "\"\\u0001\\u001f\""));
    CHECK(json_is("\xe2\x82\xac", "\"\xe2\x82\xac\""));             /* valid 3 bytes kept */
    CHECK(json_is("\xf0\x9f\x98\x80", "\"\xf0\x9f\x98\x80\""));     /* valid 4 bytes kept */
    CHECK(json_is("\xff", "\"\xef\xbf\xbd\""));                     /* invalid byte */
    CHECK(json_is("\xe2\x82", "\"\xef\xbf\xbd\xef\xbf\xbd\""));     /* cut sequence */
    CHECK(json_is("\xc0\xaf", "\"\xef\xbf\xbd\xef\xbf\xbd\""));     /* overlong */
    CHECK(json_is("\xed\xa0\x80", "\"\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd\"")); /* surrogate */
    CHECK(json_is("", "\"\""));
    return check_done("buf");
}
