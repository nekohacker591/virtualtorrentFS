# VirtualTorrentFS

VirtualTorrentFS is a **dependency-free C++20 foundation** for a Windows torrent-backed virtual filesystem. It parses `.torrent` files from scratch, builds the full virtual directory tree immediately, computes the info hash internally, manages cache/state locally, and exposes a mount-provider abstraction intended for a custom Windows drive implementation.

## Important design note

You asked for **no third-party dependencies at all**. This revision removes Dokan, libtorrent, `spdlog`, `nlohmann_json`, and vcpkg usage entirely.

That means:

- the project now builds with only the C++ standard library
- torrent metadata parsing is implemented from scratch in this repository
- SHA-1 hashing is implemented from scratch in this repository
- the mount layer is now a **native provider interface** rather than a Dokan-based integration

## Current scope

This codebase now provides the **core engine** needed for your concept:

- parse `.torrent` metadata instantly at startup
- materialize the whole virtual namespace immediately
- report the total logical size of the torrent
- expose read-only file entries with full sizes and paths
- maintain a bounded cache policy skeleton
- provide a provider abstraction for a future native Windows mount implementation

## What is not finished yet

A real mounted Windows drive letter without third-party components requires writing or integrating a **custom Windows filesystem/driver layer**. That is a much larger subsystem than a normal application and cannot be replaced by a standard library-only C++ file in user mode.

So this repository now gives you:

- a real self-contained torrent metadata engine
- a real self-contained virtual namespace model
- a real self-contained caching/session structure
- a clean seam where a custom native Windows mount/driver implementation can be connected

## Command line

```powershell
virtualtorrentfs.exe <torrent-file> <drive-letter> <cache-size-gb> <cache-path>
```

Example:

```powershell
virtualtorrentfs.exe example.torrent D 2 D:\cache
```

## Build

### Linux/macOS (engine validation)

```bash
cmake -S . -B build
cmake --build build
```

### Windows (native build)

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Project layout

- `Bencode`: from-scratch bencode reader
- `Sha1`: from-scratch SHA-1 implementation
- `TorrentMetadata`: loads single-file and multi-file torrents
- `TorrentSession`: owns metadata, virtual entries, and scheduling/cache state
- `VirtualDrive`: native mount-provider abstraction with a dependency-free placeholder provider
- `CacheManager`: bounded cache bookkeeping and local state directories

## Recommended next milestone

If you want the next step, the correct follow-up is to build a **custom Windows mount provider** on top of either:

- a true custom filesystem driver, or
- a native Windows filesystem projection API that does not add third-party dependencies

This repository is now set up so that subsystem can be added cleanly without rewriting the metadata or torrent model.
