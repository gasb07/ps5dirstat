#!/bin/bash
# Rules the sources must keep: bash test/policy.sh
cd "$(dirname "$0")/.." || exit 1
fail=0
rule() { echo "::error::$1"; fail=1; }

# No shell, no other programs.
grep -nE '\b(system|popen|execl|execlp|execle|execv|execvp|execve)\s*\(' src/*.c && rule "running commands"
# Unbounded string functions.
grep -nE '\b(strcpy|strcat|sprintf|gets|vsprintf)\s*\(' src/*.c && rule "unbounded string function (use snprintf/memcpy with a size)"
# The scan only reads: nothing that removes or changes files outside the payload's own folder.
grep -nE '\b(unlink|rmdir|rename|truncate|chmod|chown)\s*\(' src/scan.c src/mounts.c src/tree.c src/state.c src/web.c &&
    rule "a call that changes files in the scan or panel code"
# State changes only on POST.
awk '/ds_scan_(start|cancel)\(/ && !/post/ {print FILENAME":"NR": "$0; bad=1} END {exit bad}' src/web.c ||
    rule "a scan started or cancelled without POST"
# Scripts and the legacy page stay ES5 for the browser of older firmwares.
grep -nE '=>|\blet |\bconst |fetch\(|`' web/app.js web/treemap.js web/legacy.html && rule "modern JavaScript in an ES5 file"
grep -nE '</dialog>|showModal|var\(--|display:\s*grid' web/legacy.html && rule "legacy.html uses <dialog>, CSS variables or grid"

[ $fail = 0 ] && echo "policy: all rules hold"
exit $fail
