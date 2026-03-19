# TorrentFS prototype

This repository now contains a Python prototype for the idea you described: mount a `.torrent` as a **read-only Windows drive**, expose the full directory tree immediately from torrent metadata, and only begin downloading a file when an application opens and reads it.

## How hard is this?

**Medium-to-hard, but realistic** if you accept a Windows-only prototype and some rough edges.

The hardest parts are:

1. **Virtual filesystem integration on Windows**
   - Python cannot mount a Windows drive by itself.
   - You need a filesystem driver layer such as **WinFsp**, plus a Python binding such as `winfspy`.

2. **Torrent piece scheduling**
   - To make playback feel instant, the app has to prioritize the pieces needed for the file currently being read.
   - That means the torrent engine must switch file/piece priorities dynamically.

3. **Player compatibility**
   - Media players usually do normal file reads, seeks, and metadata probes.
   - The virtual filesystem must tolerate many small reads, random seeks, and repeated opens.

4. **Cache control**
   - A torrent client normally wants to keep downloaded data.
   - Your design needs a cache policy so old files can be discarded when the cache limit is reached.

## Current implementation

`mounttorrent.py` implements the following prototype behavior:

- Loads a `.torrent` file up front, so the full metadata tree is available immediately.
- Mounts a read-only filesystem view using WinFsp.
- Reports total volume size as the torrent's total size and free space as `0`, so Windows sees the mounted drive as full.
- Creates directories/files from torrent metadata without downloading file payloads up front.
- Raises file priority when a file is opened.
- Starts piece download on first read and waits until the requested piece range exists locally.
- Reads bytes from the cache directory once pieces are available.
- Keeps a soft cache size limit and evicts least-recently-used inactive cached files.

## Requirements

Install on Windows:

```powershell
pip install libtorrent winfspy
```

Also install **WinFsp** itself:

- https://winfsp.dev/

## Usage

```powershell
python mounttorrent.py example.torrent d 2 d:\cache
```

That means:

- `example.torrent` = torrent metadata file
- `d` = mount as drive `D:`
- `2` = soft cache limit of 2 GB
- `d:\cache` = directory where downloaded content is stored

## Important limitations

This is a **prototype**, not a production-ready torrent filesystem yet.

Known limitations:

- Windows-only.
- Depends on WinFsp and Python bindings being installed correctly.
- Cache eviction deletes local files from the cache folder, but does not yet do advanced piece-level reclamation.
- Magnet-link metadata prefetch is not implemented; this version expects a `.torrent` file so metadata is available immediately.
- Sequential streaming is optimized for "read when opened", but aggressive random seeking from some media players may still feel slow on poorly seeded torrents.
- Error handling for edge-case libtorrent alerts can be expanded further.

## Recommended next steps

If you want to turn this into a serious tool, the next upgrades should be:

1. Add **magnet URI support** with metadata prefetch and persistent resume data.
2. Add **per-file state tracking** and smarter eviction tied to torrent storage state.
3. Add **read-ahead heuristics** based on media-player seek behavior.
4. Add **service mode / tray app** so the mount survives independently of a terminal window.
5. Add **proper tests on Windows** using a small test torrent.
