# Architecture

## Layers

### 1. Metadata ingestion

`Bencode` parses raw torrent files and `TorrentMetadata` converts them into a strongly typed in-memory model.

### 2. Virtual namespace

`TorrentSession` builds a full tree of directories and files immediately from torrent metadata so the filesystem can report full capacity and the complete song list before payload download starts.

### 3. Cache/control plane

`CacheManager` creates local state folders, tracks cache pressure, and provides a place to plug in future file eviction/pinning logic.

### 4. Mount provider seam

`VirtualDrive` is intentionally isolated behind a provider boundary so the core engine remains dependency-free while a future native Windows mount implementation can be developed independently.

## Why this redesign exists

The earlier version depended on Dokan and libtorrent. The user explicitly requested a from-scratch implementation with no external dependencies, so this revision replaces those libraries with internal code for torrent parsing, hashing, namespace construction, and cache orchestration.

## Next subsystem to implement

The next major deliverable is a Windows-native mount provider capable of surfacing the namespace as a real read-only drive letter and routing open/read requests into the torrent streaming engine.
