# virtualtorrentFS

A Python prototype for mounting a `.torrent` as a **read-only Windows drive** whose visible size matches the torrent size, while file data is fetched **on demand** when an application reads it.

## What this prototype does

- Parses torrent metadata up front, so the directory tree is available before any file payload is downloaded.
- Mounts the torrent as a **virtual read-only drive** using WinFsp via `winfspy`.
- Shows every file in Windows Explorer with its real file size from the torrent metadata.
- Reports the volume as **full** (`free_size = 0`) so Explorer displays the fake disk as full.
- Starts a **sequential torrent download** for a file when it is opened/read.
- Serves reads from a local **cache directory** backed by libtorrent sparse storage.
- Uses background threads for alerts, download prioritization, and filesystem responsiveness.

## Reality check

This is feasible, but there are a few hard parts:

1. **A virtual filesystem driver layer** is required. In Python on Windows, WinFsp is the practical option.
2. **Media players do not all read sequentially.** VLC usually streams nicely, but some apps seek aggressively, which means you need robust piece prioritization.
3. **Torrent piece boundaries do not match file boundaries.** Reads may need neighboring pieces from shared files.
4. **Cache eviction is tricky.** Removing cached files that libtorrent still expects can confuse the storage backend, so this prototype keeps eviction simple.
5. **Playback responsiveness depends on swarm health.** If peers are slow, the file open/read can still stall.

## Dependencies

Install these on Windows:

```powershell
pip install python-libtorrent winfspy
```

You also need the [WinFsp](https://winfsp.dev/) driver installed system-wide.

## Usage

The command line matches the shape you requested:

```powershell
python mounttorrent.py example.torrent D 2 D:/cache
```

Arguments:

1. `example.torrent` → torrent file to mount
2. `D` → drive letter to assign
3. `2` → cache size limit in GB
4. `D:/cache` → location where downloaded file data is stored

Optional tuning:

```powershell
python mounttorrent.py example.torrent D 2 D:/cache --readahead-mb 16 --log-level DEBUG
```

## How it works

### Metadata first

The script loads the `.torrent` file immediately using `libtorrent.torrent_info`, builds the full directory tree, and exposes it to WinFsp before any payload files are downloaded.

### Fake full disk

The virtual volume reports:

- `total_size = torrent total size`
- `free_size = 0`

That makes Explorer show the drive as full even though the actual bytes live in your cache only when requested.

### On-demand streaming

When a file is opened/read:

1. The filesystem maps the file to its byte offset inside the torrent.
2. The backend queues the relevant piece range plus configurable readahead.
3. libtorrent switches those pieces to high priority with deadlines.
4. The read blocks until the required pieces are available in cache.
5. The bytes are read from the sparse cache file and returned to the caller.

## Important limitations

- **Windows only** right now.
- **Read-only only** by design.
- Best for **large mostly-sequential media files**.
- Cache eviction is intentionally conservative and may need refinement for very large active sessions.
- Magnet links are not implemented; this currently expects a `.torrent` file so metadata is available immediately.

## Suggested next upgrades

If you want this to become a serious tool instead of a prototype, the next improvements should be:

1. Magnet link support with background metadata warm-up.
2. Smarter cache management that cooperates with libtorrent resume/storage state.
3. Piece-aware buffering tuned for media seeking.
4. Better Windows file timestamps and security descriptors.
5. Optional pre-buffering on `open` before the first `read`.
6. A tray app / GUI wrapper so mounting feels seamless.

## Example workflow

1. Start the mount.
2. Open the new drive in Explorer.
3. Browse the full torrent tree instantly.
4. Double-click a song in VLC.
5. The file begins downloading into the cache and is streamed through the mounted drive.

That is the core "fake 14 TB drive that only downloads what you actually open" behavior you described.
