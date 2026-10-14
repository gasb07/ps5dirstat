# PS5 DirStat design

Date: 2026-10-14

## Goal

A payload for a jailbroken PS5 that scans the file system and shows disk usage in a web panel,
like WinDirStat on Windows: a directory tree sorted by size, with a size bar on each row, and a
treemap where each rectangle is a file or directory and its area is its size.

Success criteria:

1. A full scan of the console runs without visible impact on a running game (no FPS drop on the
   overlay, no stutter).
2. The panel works on a PC or phone browser and on the PS5 Media tab, on current and old firmwares.
3. The last scan is shown right after the payload loads, without scanning again.

Out of scope: deleting, moving or opening files. The panel is read-only.

## Architecture

Same layout and build as ps4mount: C payload built with `prospero-clang` from the PS5 Payload SDK,
pages embedded with `xxd -i`, own HTTP server.

| File | Purpose |
|---|---|
| `src/main.c` | Boot, single instance (lock on TCP port 9033), Media tab shortcut, web loop |
| `src/scan.c/h` | Scan thread that builds the tree |
| `src/tree.c/h` | Node array, string pool, children sorted by size, JSON for a node |
| `src/snap.c/h` | Writes and reads the snapshot `/data/ps5-dirstat/last.bin` |
| `src/web.c/h` | HTTP server on port 8771, based on ps4mount `web.c` |
| `web/index.html` | Panel for current browsers (PC, phone, recent firmwares) |
| `web/legacy.html` | Panel for the browser of old firmwares |
| `assets/icon0.png` | Media tab icon |
| `build.sh` | Builds `build/ps5-dirstat.elf` |

### Tree storage

The tree lives in memory as one array of fixed-size nodes plus one string pool for names.
Each node holds: parent index, first child index, next sibling index, name offset in the pool,
type (file, directory, error), total size in bytes, recursive file count and recursive directory
count. About 32 bytes per node plus the name, so 1 million entries take about 60 to 80 MB.

The snapshot is the header, the node array and the string pool written as is. Loading is one
`read`, with no parsing.

## Scan rules

- **D3.** The scan thread runs at the lowest priority and sleeps 1 ms every 256 entries.
- **D4.** The scan starts at `/`. It skips `/dev` and every mount whose type is `devfs`, `nullfs`,
  `procfs`, `fdescfs` or `tmpfs` (mount list from `getfsstat`). `nullfs` is skipped because the
  sandbox mounts under `/mnt/sandbox` repeat `/user` and `/system`. `tmpfs` is RAM, not disk.
- **D5.** A file with `st_nlink > 1` is counted once, tracked by a set of `(st_dev, st_ino)`.
- **D6.** The size shown is `st_size` (logical size), the WinDirStat default.
- **D7.** While a new scan runs, the panel keeps serving the previous tree. The new tree replaces it
  only when the scan ends, and is then written to disk. A cancelled scan keeps the previous tree.
- **D8.** A directory that cannot be opened becomes an error node with size 0. The scan goes on.
- **D17.** A path deeper than 256 levels or longer than `PATH_MAX` becomes an error node and is not
  followed. Symlinks are never followed.

Directory totals (size, file count, directory count) are summed when the DFS leaves a directory.

## HTTP API

| Route | Result |
|---|---|
| `GET /` | `index.html` |
| `GET /legacy` | `legacy.html` |
| `GET /api/status` | `state` (`idle`, `scanning`, `loading`), `gen`, snapshot date, totals (bytes, files, directories, errors), progress while scanning (entries read, current path) |
| `POST /api/scan` | Starts a scan. 409 if one is running |
| `POST /api/scan/cancel` | Cancels the running scan |
| `GET /api/node?id=N&gen=G&limit=200` | The node (name, full path, size, files, directories) and its children sorted by size, largest first. Children past `limit` are merged into one "N others" item with their summed size |
| `GET /api/map?id=N&gen=G&depth=3` | Subtree for the treemap: up to 3 levels and 5000 nodes. A child under 0.1% of its parent's size is merged into "others" |

- **D9.** A node `id` is its index in the array. Each new tree increments `gen`. A request with an
  old `gen` gets 409 and the panel reloads from the root.
- **D11.** The panel polls `/api/status` every second only while a scan runs.
- **D12.** At boot the payload loads `last.bin` if it exists. Without it the panel shows
  "No scan yet" and waits for the user to start one. The payload never starts a scan by itself.
- The Media tab shortcut opens `/` or `/legacy` by firmware version, with the same rule as ps4mount.

## Panel

Layout as in WinDirStat: tree on top, treemap below. Stacked on a phone, with a draggable divider on
a PC. Dark colors from the ps4mount panel.

1. **Top bar.** Scan / Cancel button, state (last scan date, or entries read and current path while
   scanning), totals.
2. **Tree.** Each row: expand arrow, name, bar proportional to the parent's size, readable size
   (KB, MB, GB), percent of parent, item count. Files and directories are mixed and sorted by size,
   largest first. Children are fetched from `/api/node` when a directory is expanded.
3. **Treemap.** Squarified layout drawn on a 2D canvas, 3 levels from the selected directory. Each
   first-level directory gets one color, its children shades of it. Click selects the item in the
   tree. Double click (X on the controller) enters a directory. A breadcrumb goes back up. On a PC,
   hovering shows path and size.
- **D13.** Each tree row is focusable, so the PS5 controller moves through it. On the PS5 the
  treemap is used with the browser cursor and the breadcrumb.
- **D14.** Both pages have the same structure. `legacy.html` is ES5 with `XMLHttpRequest` only, no
  CSS variables, no grid, no `<dialog>`. The squarified function is the same code in both pages.

## Errors

- **D15.** The node array and string pool grow with `realloc` up to 256 MB in total. Past that, or on
  allocation failure, the scan stops with "not enough memory: N entries read", the partial tree is
  dropped and the previous tree stays.
- **D16.** `last.bin` is accepted only if magic, version, declared sizes against file size and all
  indexes are valid. Otherwise it is ignored and the panel shows "No scan yet". It is written to
  `last.bin.tmp` and renamed, so a crash while writing does not damage the previous snapshot.
- **D18.** PS5 notifications when a scan starts and when it ends (total size and duration). Log as in
  ps4mount.

## Tests

Host tests run by `test/run.sh`, as in ps4mount:

1. `tree_test.c`: builds a tree in a temporary directory with files, hardlinks, symlinks, an
   unreadable directory and deep nesting. Checks sizes, counts, hardlink dedup (D5), symlinks not
   followed, error nodes (D8, D17).
2. `snap_test.c`: write, read and compare; truncated and corrupted files are rejected (D16).
3. `http_test.c`: routes, 409 on old `gen` (D9), "N others" merge, `map` limits.
4. Treemap: Node test of the squarified function: no overlapping cells, cell area proportional to
   size.

On the console: a full scan while a game runs, watching FPS on the overlay, and both pages opened
from the Media tab.
