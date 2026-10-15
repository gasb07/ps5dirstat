#!/bin/bash
# Builds and runs the host tests (no PS5 or SDK needed): bash test/run.sh
#   CC=clang     compiler (default: cc)
#   SANITIZE=1   with AddressSanitizer and UndefinedBehaviorSanitizer
set -e
cd "$(dirname "$0")/.."
B=build/test
mkdir -p "$B"
CC=${CC:-cc}
FLAGS="-O1 -g -Wall -Wextra -I$B -Isrc -Itest"
if [ "${SANITIZE:-0}" = 1 ]; then
    FLAGS="$FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
    export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1
fi
TEST_TMP=$(mktemp -d)
export TEST_TMP
trap 'chmod -R u+rwx "$TEST_TMP" 2>/dev/null; rm -rf "$TEST_TMP"' EXIT

echo "== buffers and JSON strings"
# shellcheck disable=SC2086
$CC $FLAGS -o "$B/buf_test" test/buf_test.c src/buf.c
"$B/buf_test"

echo "== tree"
# shellcheck disable=SC2086
$CC $FLAGS -o "$B/tree_test" test/tree_test.c src/tree.c src/buf.c
"$B/tree_test"
