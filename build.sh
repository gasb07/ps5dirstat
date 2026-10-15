#!/bin/bash
# Builds build/ps5-dirstat.elf with the PS5 Payload SDK (https://github.com/ps5-payload-dev/sdk).
# Set PS5_PAYLOAD_SDK when the SDK is not in /opt/ps5-payload-sdk.
set -e
cd "$(dirname "$0")"
B=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/bin
mkdir -p build
# Embedded data: panel pages and scripts, Media tab icon.
{
    (cd web && xxd -i -n index_html index.html)
    (cd web && xxd -i -n legacy_html legacy.html)
    (cd web && xxd -i -n app_js app.js)
    (cd web && xxd -i -n treemap_js treemap.js)
} > build/page_data.h
(cd assets && xxd -i -n icon0_png icon0.png) > build/icon_data.h
[ -f build/sqlite3.o ] || "$B/prospero-clang" -O2 -w -c third_party/sqlite/sqlite3.c -o build/sqlite3.o \
    -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0
"$B/prospero-clang" -O2 -Wall -Wextra -Ibuild -Ithird_party/sqlite -o build/ps5-dirstat.elf \
    src/main.c src/buf.c src/tree.c src/scan.c src/mounts.c src/snap.c src/state.c src/web.c \
    src/db.c src/fw.c src/logts.c build/sqlite3.o \
    -lSceUserService -lSceAppInstUtil -lSceSystemService -lkernel_sys
ls -la build/ps5-dirstat.elf
