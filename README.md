# PS5 DirStat

See what is using the storage of a jailbroken PS5, like WinDirStat on Windows.

PS5 DirStat scans the console file system in the background, at low priority, and shows the
result in a web panel: a folder tree sorted by size, with a bar on each row, and a treemap where
every rectangle is a folder or file and its area is its size. It only reads: nothing is deleted
or moved.

## Requirements

- A jailbroken PS5 with an ELF loader listening on port **9021**.

## Use

1. Send `ps5-dirstat.elf` to the ELF loader.
2. Open **PS5 DirStat** in the **Media** tab, or `http://<PS5 IP>:8771` on a PC or phone on the
   same network.
3. Press **Scan**. A notification shows when the scan starts and when it ends.

The last scan is kept in `/data/ps5-dirstat/last.bin` and shown again the next time the payload
loads. Press **Scan** to update it.

## Panel

- **Tree**: click a row to select it, click it again (or press X) to open the folder. Arrow keys
  also work.
- **Treemap**: click a rectangle to find it in the tree, double click a folder to show it alone,
  and use the path above the map to go back up. **Show in map** opens the selected folder.
- Older firmwares open a simpler version of the same panel automatically (`/legacy`).

## What is not counted

- `/dev`, and mounts that hold no data of their own or repeat other folders (`devfs`, `nullfs`,
  `procfs`, `fdescfs`, `tmpfs`). They are listed as "not scanned".
- Files with several hard links are counted once. Symbolic links are not followed.
- Folders the payload cannot read are listed as "not readable".

## Logs

`/data/ps5-dirstat/log.txt`, including every mount and its type at the start of each scan.

## Build

Requires the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) in `/opt/ps5-payload-sdk`
(or set `PS5_PAYLOAD_SDK`).

    bash build.sh        # build/ps5-dirstat.elf
    bash test/run.sh     # host tests (no console needed)

## License

GPL-3.0-or-later.
