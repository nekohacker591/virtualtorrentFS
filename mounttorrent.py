#!/usr/bin/env python3
"""Mount a torrent as a read-only Windows drive.

The mounted filesystem exposes every file from the torrent metadata without
pre-downloading file contents. When an application opens a file, the script
starts a sequential torrent download for the corresponding byte range and
serves reads from a local cache directory.

This implementation targets Windows and uses:
- libtorrent for torrent metadata/session management
- winfspy for the user-space filesystem mount

Example:
    python mounttorrent.py example.torrent D 2 D:/cache
"""

from __future__ import annotations

import argparse
import logging
import os
import queue
import string
import sys
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

try:
    import libtorrent as lt
except ImportError as exc:  # pragma: no cover - dependency guard
    raise SystemExit(
        "libtorrent is required. Install it with `pip install python-libtorrent`."
    ) from exc

try:
    from winfspy import (
        FILE_ATTRIBUTE,
        CREATE_FILE_CREATE_OPTIONS,
        BaseFileSystemOperations,
        FileSystem,
        NTStatusError,
        enable_debug_log,
    )
except ImportError as exc:  # pragma: no cover - dependency guard
    raise SystemExit(
        "winfspy is required. Install it with `pip install winfspy`."
    ) from exc


LOGGER = logging.getLogger("mounttorrent")
CHUNK_SIZE = 256 * 1024
READ_POLL_INTERVAL = 0.15
READ_TIMEOUT = 120.0
META_POLL_INTERVAL = 0.2
SEQUENTIAL_PRIORITY = 7
DEFAULT_READAHEAD_BYTES = 8 * 1024 * 1024


@dataclass(frozen=True)
class TorrentFileEntry:
    index: int
    path: str
    size: int
    offset: int
    piece_start: int
    piece_end: int
    attributes: int
    is_directory: bool = False


@dataclass
class OpenedTorrentFile:
    entry: TorrentFileEntry
    direct_io: bool = False


class MetadataTimeoutError(RuntimeError):
    pass


class TorrentBackend:
    """Owns the libtorrent session and provides byte-range access helpers."""

    def __init__(
        self,
        torrent_path: Path,
        cache_root: Path,
        cache_limit_gb: int,
        readahead_bytes: int = DEFAULT_READAHEAD_BYTES,
    ) -> None:
        self.torrent_path = torrent_path
        self.cache_root = cache_root
        self.cache_limit_bytes = max(cache_limit_gb, 1) * 1024**3
        self.readahead_bytes = max(readahead_bytes, CHUNK_SIZE)
        self._session = lt.session({
            "listen_interfaces": "0.0.0.0:6881",
            "enable_dht": True,
            "enable_lsd": True,
            "enable_upnp": True,
            "enable_natpmp": True,
            "alert_mask": lt.alert.category_t.status
            | lt.alert.category_t.error
            | lt.alert.category_t.storage,
        })
        settings = {
            "active_downloads": 8,
            "active_seeds": 4,
            "connections_limit": 200,
            "cache_size": 2048,
            "aio_threads": max(4, (os.cpu_count() or 4)),
            "strict_end_game_mode": False,
        }
        self._session.apply_settings(settings)
        self._alert_thread = threading.Thread(target=self._drain_alerts, daemon=True)
        self._stop_event = threading.Event()
        self._piece_lock = threading.Lock()
        self._piece_waiters: Dict[int, threading.Event] = {}
        self._download_queue: "queue.Queue[Tuple[int, int]]" = queue.Queue()
        self._download_thread = threading.Thread(target=self._download_worker, daemon=True)

        info = lt.torrent_info(str(torrent_path))
        params = {
            "ti": info,
            "save_path": str(cache_root),
            "storage_mode": lt.storage_mode_t.storage_mode_sparse,
            "flags": lt.torrent_flags.auto_managed,
        }
        self.handle = self._session.add_torrent(params)
        self.handle.set_sequential_download(True)
        self.handle.set_max_connections(80)
        self.handle.set_upload_limit(256 * 1024)
        self._info = info
        self._file_storage = info.files()
        self._entries = self._build_entries()
        self._directories = self._build_directories()

    @property
    def piece_length(self) -> int:
        return self._info.piece_length()

    @property
    def total_size(self) -> int:
        return self._info.total_size()

    def start(self) -> None:
        self.cache_root.mkdir(parents=True, exist_ok=True)
        self._alert_thread.start()
        self._download_thread.start()
        self._wait_for_metadata()
        self._configure_storage()

    def close(self) -> None:
        self._stop_event.set()
        try:
            self._download_queue.put_nowait((-1, -1))
        except queue.Full:
            pass
        if self._download_thread.is_alive():
            self._download_thread.join(timeout=2)
        if self._alert_thread.is_alive():
            self._alert_thread.join(timeout=2)
        try:
            self._session.pause()
        except Exception:
            LOGGER.exception("Failed to pause libtorrent session")

    def _wait_for_metadata(self, timeout: float = 30.0) -> None:
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.handle.status().has_metadata:
                return
            time.sleep(META_POLL_INTERVAL)
        raise MetadataTimeoutError(
            f"Timed out waiting for torrent metadata from {self.torrent_path}"
        )

    def _configure_storage(self) -> None:
        self.handle.set_download_limit(0)
        self.handle.set_upload_limit(0)
        self.handle.set_flags(lt.torrent_flags.sequential_download)
        self.handle.file_priority([0] * self._file_storage.num_files())

    def _drain_alerts(self) -> None:
        while not self._stop_event.is_set():
            alerts = self._session.pop_alerts()
            for alert in alerts:
                msg = alert.message()
                if isinstance(alert, lt.piece_finished_alert):
                    piece = alert.piece_index
                    with self._piece_lock:
                        waiter = self._piece_waiters.get(piece)
                    if waiter:
                        waiter.set()
                elif isinstance(alert, lt.save_resume_data_failed_alert):
                    LOGGER.warning("Resume data save failed: %s", msg)
                elif isinstance(alert, lt.torrent_error_alert):
                    LOGGER.error("Torrent error: %s", msg)
            time.sleep(0.1)

    def _download_worker(self) -> None:
        while not self._stop_event.is_set():
            try:
                piece_start, piece_end = self._download_queue.get(timeout=0.2)
            except queue.Empty:
                continue
            if piece_start < 0:
                return
            try:
                self._prioritize_range(piece_start, piece_end)
                self._enforce_cache_limit()
            finally:
                self._download_queue.task_done()

    def _prioritize_range(self, piece_start: int, piece_end: int) -> None:
        piece_priorities = [0] * self._info.num_pieces()
        deadline_ms = 0
        for piece in range(piece_start, min(piece_end + 1, self._info.num_pieces())):
            piece_priorities[piece] = SEQUENTIAL_PRIORITY
            self.handle.set_piece_deadline(piece, deadline_ms, lt.deadline_flags_t.alert_when_available)
            deadline_ms += 150
        self.handle.prioritize_pieces(piece_priorities)

    def _enforce_cache_limit(self) -> None:
        files: List[Tuple[float, Path, int]] = []
        total = 0
        for root, _, names in os.walk(self.cache_root):
            for name in names:
                path = Path(root) / name
                try:
                    stat = path.stat()
                except FileNotFoundError:
                    continue
                total += stat.st_size
                files.append((stat.st_atime, path, stat.st_size))
        if total <= self.cache_limit_bytes:
            return
        files.sort()
        for _, path, size in files:
            if total <= self.cache_limit_bytes:
                break
            try:
                path.unlink()
                total -= size
            except FileNotFoundError:
                continue
            except OSError:
                LOGGER.warning("Could not evict cached file %s", path)

    def _build_entries(self) -> Dict[str, TorrentFileEntry]:
        entries: Dict[str, TorrentFileEntry] = {
            "/": TorrentFileEntry(
                index=-1,
                path="/",
                size=0,
                offset=0,
                piece_start=0,
                piece_end=0,
                attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY),
                is_directory=True,
            )
        }
        piece_length = self._info.piece_length()
        for index in range(self._file_storage.num_files()):
            relative = self._file_storage.file_path(index).replace("\\", "/")
            size = self._file_storage.file_size(index)
            offset = self._file_storage.file_offset(index)
            piece_start = offset // piece_length
            piece_end = max(offset + max(size - 1, 0), offset) // piece_length
            entries[f"/{relative}"] = TorrentFileEntry(
                index=index,
                path=f"/{relative}",
                size=size,
                offset=offset,
                piece_start=piece_start,
                piece_end=piece_end,
                attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_ARCHIVE),
                is_directory=False,
            )
        return entries

    def _build_directories(self) -> Dict[str, List[str]]:
        directories: Dict[str, List[str]] = {"/": []}
        for path in list(self._entries):
            if path == "/":
                continue
            parent = str(Path(path).parent).replace("\\", "/")
            current = ""
            for part in Path(path.lstrip("/")).parts[:-1]:
                current = f"{current}/{part}" if current else f"/{part}"
                directories.setdefault(current, [])
            directories.setdefault(parent if parent != "." else "/", [])
            name = Path(path).name
            if name not in directories[parent if parent != "." else "/"]:
                directories[parent if parent != "." else "/"].append(name)
        for directory in list(directories):
            if directory not in self._entries:
                self._entries[directory] = TorrentFileEntry(
                    index=-1,
                    path=directory,
                    size=0,
                    offset=0,
                    piece_start=0,
                    piece_end=0,
                    attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY),
                    is_directory=True,
                )
        return directories

    def list_directory(self, path: str) -> List[str]:
        normalized = self._normalize_path(path)
        return sorted(self._directories.get(normalized, []))

    def get_entry(self, path: str) -> TorrentFileEntry:
        normalized = self._normalize_path(path)
        try:
            return self._entries[normalized]
        except KeyError as exc:
            raise FileNotFoundError(normalized) from exc

    def _normalize_path(self, path: str) -> str:
        normalized = str(Path(path)).replace("\\", "/")
        if not normalized.startswith("/"):
            normalized = f"/{normalized}"
        return normalized

    def queue_file(self, entry: TorrentFileEntry, offset: int = 0, length: Optional[int] = None) -> None:
        if entry.is_directory:
            return
        length = entry.size - offset if length is None else length
        absolute_start = entry.offset + offset
        absolute_end = min(entry.offset + entry.size, absolute_start + length + self.readahead_bytes)
        piece_start = absolute_start // self.piece_length
        piece_end = max(absolute_end - 1, absolute_start) // self.piece_length
        self._download_queue.put((piece_start, piece_end))
        self.handle.file_priority(entry.index, 1)

    def read(self, entry: TorrentFileEntry, offset: int, length: int) -> bytes:
        if offset >= entry.size:
            return b""
        length = min(length, entry.size - offset)
        self.queue_file(entry, offset, length)
        first_piece = (entry.offset + offset) // self.piece_length
        last_piece = (entry.offset + offset + length - 1) // self.piece_length
        for piece in range(first_piece, last_piece + 1):
            self._wait_for_piece(piece)
        cache_path = self.cache_root / self._file_storage.file_path(entry.index)
        deadline = time.time() + READ_TIMEOUT
        while time.time() < deadline:
            try:
                with cache_path.open("rb") as fh:
                    fh.seek(offset)
                    return fh.read(length)
            except FileNotFoundError:
                time.sleep(READ_POLL_INTERVAL)
        raise TimeoutError(f"Timed out waiting for cached file data: {cache_path}")

    def _wait_for_piece(self, piece: int) -> None:
        status = self.handle.status()
        if piece < len(status.pieces) and status.pieces[piece]:
            return
        with self._piece_lock:
            waiter = self._piece_waiters.setdefault(piece, threading.Event())
        if not waiter.wait(timeout=READ_TIMEOUT):
            raise TimeoutError(f"Timed out waiting for piece {piece}")


class TorrentFsOperations(BaseFileSystemOperations):
    def __init__(self, backend: TorrentBackend, volume_label: str) -> None:
        super().__init__()
        self.backend = backend
        self.volume_label = volume_label
        self._handles: Dict[int, OpenedTorrentFile] = {}
        self._handle_lock = threading.Lock()
        self._next_handle = 1

    def get_volume_info(self):
        return {
            "total_size": self.backend.total_size,
            "free_size": 0,
            "volume_label": self.volume_label,
        }

    def get_security_by_name(self, file_name):
        entry = self.backend.get_entry(file_name)
        return entry.attributes, None

    def create(self, file_name, create_options, granted_access, file_attributes, security_descriptor, allocation_size):
        if create_options & CREATE_FILE_CREATE_OPTIONS.FILE_DIRECTORY_FILE:
            entry = self.backend.get_entry(file_name)
            if not entry.is_directory:
                raise NTStatusError(0xC0000103)
            return self._allocate_handle(entry)

        entry = self.backend.get_entry(file_name)
        if entry.is_directory:
            return self._allocate_handle(entry)
        self.backend.queue_file(entry)
        return self._allocate_handle(entry)

    def _allocate_handle(self, entry: TorrentFileEntry) -> int:
        with self._handle_lock:
            handle = self._next_handle
            self._next_handle += 1
            self._handles[handle] = OpenedTorrentFile(entry=entry)
            return handle

    def close(self, file_context):
        with self._handle_lock:
            self._handles.pop(file_context, None)

    def get_file_info(self, file_context, file_name):
        entry = self._handles[file_context].entry if file_context else self.backend.get_entry(file_name)
        return self._file_info_from_entry(entry)

    def _file_info_from_entry(self, entry: TorrentFileEntry):
        now = int(time.time() * 10_000_000)
        return {
            "file_attributes": entry.attributes,
            "allocation_size": entry.size,
            "file_size": entry.size,
            "creation_time": now,
            "last_access_time": now,
            "last_write_time": now,
            "change_time": now,
            "index_number": max(entry.index, 0),
        }

    def read_directory(self, file_context, file_name, marker):
        entry = self._handles[file_context].entry if file_context else self.backend.get_entry(file_name)
        if not entry.is_directory:
            raise NTStatusError(0xC0000103)
        items = [".", ".."] + self.backend.list_directory(entry.path)
        if marker:
            try:
                start = items.index(marker) + 1
            except ValueError:
                start = 0
        else:
            start = 0
        output = []
        for name in items[start:]:
            if name == ".":
                child = entry
            elif name == "..":
                parent = str(Path(entry.path).parent).replace("\\", "/")
                child = self.backend.get_entry(parent if parent != "." else "/")
            else:
                child_path = str(Path(entry.path) / name).replace("\\", "/")
                child = self.backend.get_entry(child_path)
            output.append({"file_name": name, **self._file_info_from_entry(child)})
        return output

    def read(self, file_context, offset, length):
        entry = self._handles[file_context].entry
        if entry.is_directory:
            raise NTStatusError(0xC00000BA)
        return self.backend.read(entry, offset, length)

    def write(self, file_context, buffer, offset, write_to_end_of_file, constrained_io):
        raise NTStatusError(0xC00000A2)

    def flush(self, file_context):
        return

    def cleanup(self, file_context, file_name, flags):
        return

    def overwrite(self, file_context, file_attributes, replace_file_attributes, allocation_size):
        raise NTStatusError(0xC00000A2)

    def set_basic_info(self, file_context, file_attributes, creation_time, last_access_time, last_write_time, change_time, file_info):
        raise NTStatusError(0xC00000A2)

    def set_file_size(self, file_context, new_size, set_allocation_size):
        raise NTStatusError(0xC00000A2)

    def rename(self, file_context, file_name, new_file_name, replace_if_exists):
        raise NTStatusError(0xC00000A2)

    def can_delete(self, file_context, file_name):
        raise NTStatusError(0xC00000A2)

    def get_dir_info_by_name(self, file_context, file_name):
        entry = self.backend.get_entry(file_name)
        return {"file_name": Path(entry.path).name, **self._file_info_from_entry(entry)}


def parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Mount a .torrent file as a read-only Windows drive")
    parser.add_argument("torrent", type=Path, help="Path to the .torrent file")
    parser.add_argument("drive_letter", help="Drive letter to mount, e.g. D")
    parser.add_argument("cache_gb", type=int, help="Maximum cache size in gigabytes")
    parser.add_argument("cache_dir", type=Path, help="Directory for downloaded file data")
    parser.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging verbosity",
    )
    parser.add_argument(
        "--readahead-mb",
        type=int,
        default=8,
        help="How much data to queue ahead of reads, in megabytes",
    )
    return parser.parse_args(argv)


def validate_args(args: argparse.Namespace) -> str:
    drive = args.drive_letter.rstrip(":").upper()
    if len(drive) != 1 or drive not in string.ascii_uppercase:
        raise SystemExit("drive_letter must be a single Windows drive letter, such as D")
    if not args.torrent.is_file():
        raise SystemExit(f"Torrent file not found: {args.torrent}")
    if args.cache_gb <= 0:
        raise SystemExit("cache_gb must be a positive integer")
    return f"{drive}:"


def main(argv: Optional[Iterable[str]] = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    mountpoint = validate_args(args)
    if os.name != "nt":
        raise SystemExit("This script currently supports Windows only because it uses WinFsp")

    enable_debug_log()
    backend = TorrentBackend(
        torrent_path=args.torrent,
        cache_root=args.cache_dir,
        cache_limit_gb=args.cache_gb,
        readahead_bytes=args.readahead_mb * 1024 * 1024,
    )
    backend.start()
    operations = TorrentFsOperations(backend, volume_label="TorrentFS")
    fs = FileSystem(
        mountpoint,
        operations,
        sector_size=4096,
        sectors_per_allocation_unit=1,
        volume_creation_time=int(time.time() * 10_000_000),
        volume_serial_number=0x19870319,
        file_info_timeout=1000,
        case_sensitive_search=False,
        case_preserved_names=True,
        unicode_on_disk=True,
        persistent_acls=False,
        post_cleanup_when_modified_only=True,
        read_only_volume=True,
    )
    try:
        LOGGER.info("Mounting %s at %s", args.torrent, mountpoint)
        fs.start()
        LOGGER.info("Mounted. Press Ctrl+C to unmount.")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        LOGGER.info("Unmount requested")
    finally:
        fs.stop()
        backend.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
