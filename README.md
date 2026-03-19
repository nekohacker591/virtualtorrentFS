# virtualtorrentFS

`virtualtorrentFS` is a Python prototype for mounting a `.torrent` as a **read-only Windows drive**.
It preloads the torrent metadata up front, exposes the torrent contents as files and folders, and only starts downloading file data when an application actually opens and reads a file.

## What this solves

For a very large torrent archive, such as a multi-terabyte music collection, this design lets you:

- See the whole directory tree immediately in Windows Explorer.
- Show the total size of the torrent as the size of the mounted drive.
- Keep the volume read-only so applications cannot modify the torrent view.
- Delay data transfer until a file is opened.
- Stream files on demand in players like VLC, assuming peers can supply the requested pieces fast enough.

## Current status

This repository now contains a **working architectural prototype**, not a polished production release.
It demonstrates the hard parts of the design in Python:

- Built-in torrent session management with `python-libtorrent`
- User-mode Windows filesystem mounting with `pywinfspy` / WinFsp
- Read-only file operations
- Metadata-first mount behavior
- On-open piece prioritization and sequential streaming
- Background prefetch queue and cache eviction

## Requirements

### Windows

This prototype is intended for **Windows only** right now because it mounts a drive letter using WinFsp.

Install:

```bash
pip install python-libtorrent pywinfspy
```

Also install the WinFsp driver from:

- https://winfsp.dev/

## Usage

```bash
python mounttorrent.py example.torrent D 2 D:/cache
```

Arguments:

1. `example.torrent` → path to the torrent file
2. `D` → drive letter to mount
3. `2` → max cache size in GB
4. `D:/cache` → path where sparse data / cached files are stored

Optional:

```bash
python mounttorrent.py example.torrent D 2 D:/cache --debug
```

## How it works

1. The script reads the `.torrent` file immediately.
   - That means file names, folder layout, and sizes are known before the drive is mounted.
   - There is no separate metadata download step when using a local `.torrent` file.
2. The torrent is mounted as a read-only filesystem.
3. Explorer sees files with their real sizes from the torrent metadata.
4. When an app opens a file, the script:
   - raises that file's torrent priority,
   - requests the pieces around the requested byte range,
   - waits for those pieces to arrive,
   - serves the bytes back to the caller.
5. A background worker prefetches low-priority pieces for files that have been opened.
6. Completed cached files are evicted with a simple LRU strategy when the cache budget is exceeded.

## Important limitations

This is the part that answers "how hard would it be?":

It is **moderately hard to build a prototype** and **fairly hard to make production-grade**.

### Why it is hard

- Windows virtual filesystems are more complex than normal scripts.
- Media players often do non-linear reads, seeks, and metadata probes.
- Torrent pieces rarely line up perfectly with individual file reads.
- Good streaming behavior depends heavily on swarm health and piece availability.
- Explorer and antivirus tools may trigger background opens or reads unexpectedly.
- Robust caching, resume logic, and error handling take a lot of work.

### What is still missing for production use

- Better seek-aware streaming heuristics
- Smarter cache accounting for sparse partial files
- Explicit piece deadline management for interactive playback
- Per-file download state reporting
- Handling for magnet links in addition to `.torrent` files
- Better protection against Explorer/thumbnailer over-reading
- Automated Windows integration tests
- Packaging into a simple installer or tray application

## Design notes

- The mounted volume reports:
  - total size = total torrent size
  - free space = 0
- That matches your idea of a "full red drive" in Explorer.
- The volume is intentionally read-only.
- Torrent metadata is available immediately from the `.torrent` file.
- Downloads start only when a file is actually read.

## Practical advice

If your real goal is smooth playback from huge media torrents, the design is valid, but success depends on:

- good peers,
- stable sequential download behavior,
- enough local cache,
- careful handling of random seeks from your player.

The prototype in this repo gives you a strong starting point for that approach in Python.
