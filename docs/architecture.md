# Architecture overview

## Components

- `ArgumentParser`: Converts CLI input into a validated runtime configuration.
- `Config`: Strongly typed runtime settings for mount, cache, and service behavior.
- `TorrentIndex`: Immutable representation of torrent files and directory layout once metadata is loaded.
- `LibtorrentSession`: Wrapper responsible for metadata prefetch, torrent session lifecycle, piece priority, and streaming reads.
- `CacheManager`: Enforces the configured cache budget and tracks residency/eviction candidates.
- `VirtualDriveManifest`: Converts torrent entries into a filesystem-like tree and provides lookups by path.
- `DokanFilesystem`: Windows-specific bridge between filesystem callbacks and the torrent/cache layer.
- `ServiceHost`: Installs, removes, and runs the mount as a Windows service.

## Read path

1. App starts, validates input, and initializes logging.
2. Torrent metadata is loaded before mount completion.
3. The manifest is generated from the torrent file list.
4. The virtual drive mounts read-only and reports the torrent payload size as total capacity.
5. When a client opens a file, the filesystem notifies `LibtorrentSession`.
6. Sequential piece priorities are raised, readahead starts, and cache reservations are created.
7. Reads block briefly only when required pieces are unavailable; already-cached ranges return immediately.

## Professional additions beyond the original request

- Cache pressure telemetry and throttling hooks.
- Session state persistence to remount the last torrent quickly.
- Path manifest export for debugging and shell integration.
- Structured logging for filesystem operations and torrent alerts.
- Clear separation between portable core logic and Windows-specific mount code.
