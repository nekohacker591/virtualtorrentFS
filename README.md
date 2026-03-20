# VirtualTorrentFS

VirtualTorrentFS is a dependency-free C++20 torrent namespace engine with a **native Windows ProjFS-based mount path**. It parses `.torrent` files from scratch, computes the info hash internally, builds the full virtual file tree immediately, and on Windows can project that namespace into a mounted drive letter mapped to a ProjFS virtualization root.

## No third-party dependencies

This project intentionally does **not** use Dokan, libtorrent, vcpkg packages, JSON libraries, or logging libraries.

It implements internally:

- bencode parsing
- SHA-1 hashing
- torrent metadata loading
- virtual namespace generation
- cache bookkeeping
- a native Windows ProjFS mount provider

## How the Windows mount works

On Windows, the executable now:

1. Parses the torrent file.
2. Builds the full read-only namespace immediately.
3. Creates a ProjFS virtualization root inside the cache directory.
4. Maps your requested drive letter to that root with a Windows DOS device mapping.
5. Responds to directory enumeration and placeholder queries from the OS.
6. Maintains an explicit projected-path index so Unicode torrent names can be resolved back to the original torrent entries during ProjFS callbacks.
7. Attempts to serve file data from the local payload cache when files are read, with a larger initial read-ahead window for media players.
8. Falls back to a cache-aware zero-byte placeholder tree if ProjFS is unavailable on the machine.

## Important limitation

The project now contains a real native Windows mount provider, but it still does **not** contain a finished peer-wire torrent downloader yet.

That means:

- the drive can project the file tree
- placeholder files can exist at full logical sizes
- reads can be served from already-cached payload files
- missing payload currently returns an offline/unavailable-style error instead of downloading from peers in real time
- if ProjFS is unavailable, the app falls back to a cache-aware sparse placeholder tree mounted to the requested drive letter

So this revision specifically addresses your request to **make our own Windows mount provider** instead of relying on a third-party filesystem library, but the next major subsystem still required is the actual torrent transport/download engine.

## Command line

```powershell
virtualtorrentfs.exe <torrent-file> <drive-letter> <cache-size-gb> <cache-path>
```

Example:

```powershell
virtualtorrentfs.exe music.torrent G 2 D:\cache
```

## Build

### Linux/macOS

```bash
cmake -S . -B build
cmake --build build
```

### Windows

```powershell
cmake -S . -B build
cmake --build build --config Release
```

> On Windows, the ProjFS provider requires the Windows Projected File System feature/runtime support to be available.

## Project layout

- `Bencode`: from-scratch torrent decoding
- `Sha1`: from-scratch SHA-1
- `TorrentMetadata`: single-file and multi-file torrent ingestion
- `TorrentSession`: namespace and cache-facing file model
- `VirtualDrive`: native Windows ProjFS provider plus non-Windows fallback
- `CacheManager`: cache root and eviction bookkeeping


## Windows path handling

The Windows mount/materialization code now attempts to handle non-Latin torrent paths more robustly by decoding torrent path bytes as UTF-8 first, then falling back to the active Windows ANSI code page, sanitizing characters that Windows does not allow in file names, and indexing the projected display path so ProjFS callbacks can resolve the same file reliably when applications such as VLC reopen it by path.
