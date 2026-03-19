#!/usr/bin/env python3
"""Mount a .torrent as a read-only Windows drive and stream files on demand.

This is a prototype-oriented implementation that combines:
- libtorrent for torrent session management and piece/file prioritization
- pywinfspy for a read-only user-mode filesystem on Windows

Expected usage:
    python mounttorrent.py example.torrent D 2 D:/cache

Requirements on Windows:
    pip install python-libtorrent pywinfspy
    WinFsp must also be installed: https://winfsp.dev/
"""

from __future__ import annotations

import argparse
import logging
import math
import os
import queue
import shutil
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

try:
    import libtorrent as lt
except ImportError:  # pragma: no cover - validated at runtime on target machine
    lt = None

try:
    from winfspy import (
        CREATE_FILE_CREATE_OPTIONS,
        FILE_ATTRIBUTE,
        NTStatusError,
        BaseFileSystemOperations,
        FileInfo,
        SecurityDescriptor,
        VolumeInfo,
        enable_debug_log,
    )
    from winfspy.plumbing import FILE_ACCESS_RIGHTS, NTSTATUS, win32_filetime_now
    from winfspy.service import FileSystemService
except ImportError:  # pragma: no cover - validated at runtime on target machine
    CREATE_FILE_CREATE_OPTIONS = FILE_ATTRIBUTE = NTStatusError = None
    BaseFileSystemOperations = object
    FileInfo = SecurityDescriptor = VolumeInfo = None
    enable_debug_log = None
    FILE_ACCESS_RIGHTS = NTSTATUS = win32_filetime_now = None
    FileSystemService = None


LOG = logging.getLogger("mounttorrent")
FILE_SD = "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;WD)"
DIR_SD = "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FRFX;;;WD)"
WANTED_PIECES_AHEAD = 8


@dataclass(frozen=True)
class TorrentEntry:
    index: int
    torrent_path: str
    file_name: str
    size: int
    offset: int
    attributes: int
    is_dir: bool = False


class DependencyError(RuntimeError):
    pass


class MetadataIndex:
    """Immutable index of torrent files used by the filesystem layer."""

    def __init__(self, info: "lt.torrent_info"):
        self.info = info
        self.entries: Dict[str, TorrentEntry] = {}
        self.children: Dict[str, List[str]] = {"\\": []}
        self._build()

    def _build(self) -> None:
        files = self.info.files()
        cumulative_offset = 0
        for file_index in range(files.num_files()):
            file_storage = files.file_path(file_index).replace("/", "\\")
            norm_path = self._norm(file_storage)
            size = files.file_size(file_index)
            self._ensure_parents(norm_path)
            self.entries[norm_path] = TorrentEntry(
                index=file_index,
                torrent_path=norm_path,
                file_name=Path(norm_path).name,
                size=size,
                offset=cumulative_offset,
                attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_ARCHIVE),
                is_dir=False,
            )
            parent = self._parent(norm_path)
            self.children.setdefault(parent, [])
            self.children[parent].append(norm_path)
            cumulative_offset += size

        for directory in list(self.children):
            if directory != "\\" and directory not in self.entries:
                self.entries[directory] = TorrentEntry(
                    index=-1,
                    torrent_path=directory,
                    file_name=Path(directory).name,
                    size=0,
                    offset=0,
                    attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY),
                    is_dir=True,
                )
        for path_children in self.children.values():
            path_children.sort(key=str.lower)

    def _ensure_parents(self, path: str) -> None:
        parts = [part for part in path.split("\\") if part]
        current = "\\"
        for part in parts[:-1]:
            current = self._join(current, part)
            self.children.setdefault(current, [])
            parent = self._parent(current)
            siblings = self.children.setdefault(parent, [])
            if current not in siblings:
                siblings.append(current)

    @staticmethod
    def _norm(path: str) -> str:
        path = path.replace("/", "\\")
        return "\\" + path.strip("\\") if path.strip("\\") else "\\"

    @staticmethod
    def _parent(path: str) -> str:
        stripped = path.strip("\\")
        if not stripped or "\\" not in stripped:
            return "\\"
        return "\\" + stripped.rsplit("\\", 1)[0]

    @staticmethod
    def _join(base: str, name: str) -> str:
        if base == "\\":
            return "\\" + name
        return base + "\\" + name

    def get(self, path: str) -> TorrentEntry:
        norm = self._norm(path)
        if norm == "\\":
            return TorrentEntry(
                index=-1,
                torrent_path="\\",
                file_name="",
                size=0,
                offset=0,
                attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY),
                is_dir=True,
            )
        try:
            return self.entries[norm]
        except KeyError as exc:
            raise FileNotFoundError(norm) from exc

    def list_dir(self, path: str) -> List[TorrentEntry]:
        norm = self._norm(path)
        if norm not in self.children:
            raise FileNotFoundError(norm)
        return [self.entries[child] for child in self.children[norm]]

    @property
    def total_size(self) -> int:
        return self.info.total_size()


class CacheManager:
    """Best-effort LRU eviction for completed file payloads."""

    def __init__(self, cache_dir: Path, max_bytes: int):
        self.cache_dir = cache_dir
        self.max_bytes = max_bytes
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        self._lock = threading.Lock()

    def touch(self, path: Path) -> None:
        if path.exists():
            os.utime(path, None)

    def evict_if_needed(self) -> None:
        with self._lock:
            files = [
                item for item in self.cache_dir.rglob("*") if item.is_file()
            ]
            total = sum(item.stat().st_size for item in files)
            if total <= self.max_bytes:
                return
            files.sort(key=lambda item: item.stat().st_atime)
            for item in files:
                if total <= self.max_bytes:
                    break
                size = item.stat().st_size
                try:
                    item.unlink()
                    total -= size
                    LOG.info("Evicted cached file %s", item)
                except FileNotFoundError:
                    continue


class TorrentStreamer:
    """Owns the torrent session and allows ranged reads with background prefetch."""

    def __init__(self, torrent_path: Path, cache_dir: Path, cache_bytes: int):
        self.torrent_path = torrent_path
        self.cache_dir = cache_dir
        self.cache_manager = CacheManager(cache_dir, cache_bytes)
        self._session: Optional["lt.session"] = None
        self._handle: Optional["lt.torrent_handle"] = None
        self._metadata = None
        self._metadata_index: Optional[MetadataIndex] = None
        self._lock = threading.RLock()
        self._stop_event = threading.Event()
        self._prefetch_queue: "queue.Queue[int]" = queue.Queue()
        self._prefetch_thread = threading.Thread(target=self._prefetch_worker, daemon=True)
        self._started_files: set[int] = set()

    @property
    def metadata_index(self) -> MetadataIndex:
        if self._metadata_index is None:
            raise RuntimeError("Torrent metadata has not been loaded yet")
        return self._metadata_index

    def start(self) -> None:
        if lt is None:
            raise DependencyError("python-libtorrent is required")
        settings = {
            "user_agent": "mounttorrent/0.1",
            "enable_dht": True,
            "enable_lsd": True,
            "enable_upnp": True,
            "alert_mask": lt.alert.category_t.status_notification
            | lt.alert.category_t.error_notification,
            "connections_limit": 200,
            "active_downloads": 8,
            "aio_threads": max(4, (os.cpu_count() or 2)),
        }
        self._session = lt.session(settings)
        info = lt.torrent_info(str(self.torrent_path))
        params = {
            "ti": info,
            "save_path": str(self.cache_dir),
            "storage_mode": lt.storage_mode_t.storage_mode_sparse,
            "flags": lt.torrent_flags.seed_mode
            & ~lt.torrent_flags.seed_mode,
        }
        self._handle = self._session.add_torrent(params)
        self._handle.set_sequential_download(True)
        self._metadata = info
        self._metadata_index = MetadataIndex(info)
        self._prefetch_thread.start()
        LOG.info("Loaded torrent metadata for %s", self.torrent_path)

    def stop(self) -> None:
        self._stop_event.set()
        if self._prefetch_thread.is_alive():
            self._prefetch_thread.join(timeout=2)
        if self._session and self._handle:
            try:
                self._session.remove_torrent(self._handle)
            except Exception:  # pragma: no cover - best effort shutdown
                LOG.exception("Failed to remove torrent handle cleanly")

    def status_snapshot(self) -> str:
        if not self._handle:
            return "torrent session not started"
        status = self._handle.status()
        return (
            f"state={status.state} progress={status.progress * 100:.2f}% "
            f"download_rate={status.download_rate / 1024:.1f}KiB/s peers={status.num_peers}"
        )

    def open_file(self, entry: TorrentEntry) -> None:
        if entry.is_dir:
            return
        with self._lock:
            if entry.index in self._started_files:
                return
            self._started_files.add(entry.index)
            self._handle.file_priority(entry.index, 7)
            self._queue_prefetch(entry.index)
            LOG.info("Started streaming for %s", entry.torrent_path)

    def read(self, entry: TorrentEntry, offset: int, length: int, wait_timeout: float = 30.0) -> bytes:
        if entry.is_dir:
            return b""
        if offset >= entry.size:
            return b""
        target_length = min(length, entry.size - offset)
        self.open_file(entry)
        piece_length = self._metadata.piece_length()
        start_piece, end_piece = self._piece_window(entry, offset, target_length, piece_length)
        self._prioritize_window(start_piece, end_piece)
        deadline = time.time() + wait_timeout
        while time.time() < deadline:
            if self._window_available(start_piece, end_piece):
                file_path = self.cache_dir / Path(entry.torrent_path.strip("\\"))
                if file_path.exists():
                    self.cache_manager.touch(file_path)
                    with file_path.open("rb") as handle:
                        handle.seek(offset)
                        data = handle.read(target_length)
                    if data:
                        return data
            time.sleep(0.1)
        raise TimeoutError(
            f"Timed out waiting for torrent data for {entry.torrent_path} at {offset}:{target_length}"
        )

    def _piece_window(
        self,
        entry: TorrentEntry,
        offset: int,
        length: int,
        piece_length: int,
    ) -> Tuple[int, int]:
        absolute_start = entry.offset + offset
        absolute_end = absolute_start + max(0, length - 1)
        start_piece = absolute_start // piece_length
        end_piece = absolute_end // piece_length
        return start_piece, end_piece

    def _prioritize_window(self, start_piece: int, end_piece: int) -> None:
        if not self._handle:
            return
        for piece in range(start_piece, end_piece + 1 + WANTED_PIECES_AHEAD):
            try:
                self._handle.piece_priority(piece, 7 if piece <= end_piece else 4)
            except RuntimeError:
                break
        self._handle.force_reannounce()
        self._handle.resume()

    def _window_available(self, start_piece: int, end_piece: int) -> bool:
        if not self._handle:
            return False
        pieces = self._handle.status().pieces
        if not pieces:
            return False
        upper = min(end_piece, len(pieces) - 1)
        return all(pieces[piece] for piece in range(start_piece, upper + 1))

    def _queue_prefetch(self, file_index: int) -> None:
        self._prefetch_queue.put(file_index)

    def _prefetch_worker(self) -> None:
        while not self._stop_event.is_set():
            try:
                file_index = self._prefetch_queue.get(timeout=0.25)
            except queue.Empty:
                continue
            try:
                self._prefetch_file(file_index)
            finally:
                self.cache_manager.evict_if_needed()
                self._prefetch_queue.task_done()

    def _prefetch_file(self, file_index: int) -> None:
        if not self._handle or not self._metadata:
            return
        file_entry = self.metadata_index.get(
            "\\" + self._metadata.files().file_path(file_index).replace("/", "\\").strip("\\")
        )
        piece_length = self._metadata.piece_length()
        piece_count = math.ceil(file_entry.size / piece_length)
        start_piece, end_piece = self._piece_window(file_entry, 0, file_entry.size, piece_length)
        LOG.debug(
            "Prefetching %s across %d pieces (%d-%d)",
            file_entry.torrent_path,
            piece_count,
            start_piece,
            end_piece,
        )
        for piece in range(start_piece, end_piece + 1):
            self._handle.piece_priority(piece, 2)


class TorrentFsOperations(BaseFileSystemOperations):
    def __init__(self, metadata: MetadataIndex, streamer: TorrentStreamer, volume_label: str):
        super().__init__()
        self.metadata = metadata
        self.streamer = streamer
        self.volume_label = volume_label
        now = win32_filetime_now()
        self._file_sd = SecurityDescriptor.from_string(FILE_SD)
        self._dir_sd = SecurityDescriptor.from_string(DIR_SD)
        self._volume_info = VolumeInfo(
            total_size=metadata.total_size,
            free_size=0,
            volume_label=volume_label,
        )
        self._root_info = FileInfo(
            file_attributes=int(FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY),
            allocation_size=0,
            file_size=0,
            creation_time=now,
            last_access_time=now,
            last_write_time=now,
            change_time=now,
            index_number=0,
        )

    def get_volume_info(self):
        return self._volume_info

    def set_volume_label(self, volume_label):
        self._volume_info.volume_label = volume_label

    def get_security_by_name(self, file_name):
        entry = self.metadata.get(file_name)
        descriptor = self._dir_sd if entry.is_dir else self._file_sd
        return entry.attributes, descriptor.handle, descriptor.size

    def create(self, file_name, create_options, granted_access, file_attributes, security_descriptor, allocation_size):
        if create_options & CREATE_FILE_CREATE_OPTIONS.FILE_DIRECTORY_FILE:
            entry = self.metadata.get(file_name)
            if not entry.is_dir:
                raise NTStatusError(NTSTATUS.STATUS_NOT_A_DIRECTORY)
            return OpenedObj(entry, self._dir_sd)
        entry = self.metadata.get(file_name)
        if entry.is_dir:
            raise NTStatusError(NTSTATUS.STATUS_FILE_IS_A_DIRECTORY)
        if granted_access & FILE_ACCESS_RIGHTS.FILE_WRITE_DATA:
            raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)
        return OpenedObj(entry, self._file_sd)

    def get_file_info(self, file_context, file_name):
        entry = file_context.entry if file_context else self.metadata.get(file_name)
        return self._make_file_info(entry)

    def read_directory(self, file_context, marker):
        entry = file_context.entry if file_context else self.metadata.get("\\")
        if not entry.is_dir:
            raise NTStatusError(NTSTATUS.STATUS_NOT_A_DIRECTORY)
        children = self.metadata.list_dir(entry.torrent_path)
        rows = [
            (".", self._make_file_info(entry)),
            ("..", self._make_file_info(self.metadata.get(self.metadata._parent(entry.torrent_path)) if entry.torrent_path != "\\" else entry)),
        ]
        rows.extend((child.file_name, self._make_file_info(child)) for child in children)
        if marker is None:
            return rows
        seen = False
        filtered = []
        for name, info in rows:
            if seen:
                filtered.append((name, info))
            elif name == marker:
                seen = True
        return filtered

    def open(self, file_name, create_options, granted_access):
        entry = self.metadata.get(file_name)
        if granted_access & FILE_ACCESS_RIGHTS.FILE_WRITE_DATA:
            raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)
        return OpenedObj(entry, self._dir_sd if entry.is_dir else self._file_sd)

    def close(self, file_context):
        return None

    def read(self, file_context, offset, length):
        entry = file_context.entry
        try:
            return self.streamer.read(entry, offset, length)
        except TimeoutError as exc:
            LOG.warning("Read timeout: %s", exc)
            raise NTStatusError(NTSTATUS.STATUS_IO_TIMEOUT) from exc

    def write(self, file_context, buffer, offset, write_to_end_of_file, constrained_io):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def flush(self, file_context):
        return None

    def cleanup(self, file_context, file_name, flags):
        return None

    def overwrite(self, file_context, file_attributes, replace_file_attributes, allocation_size):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def set_basic_info(self, file_context, file_attributes, creation_time, last_access_time, last_write_time, change_time, file_info):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def set_file_size(self, file_context, new_size, set_allocation_size):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def can_delete(self, file_context, file_name):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def rename(self, file_context, file_name, new_file_name, replace_if_exists):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def get_security(self, file_context):
        return file_context.security_descriptor

    def set_security(self, file_context, security_information, modification_descriptor):
        raise NTStatusError(NTSTATUS.STATUS_MEDIA_WRITE_PROTECTED)

    def _make_file_info(self, entry: TorrentEntry) -> FileInfo:
        now = win32_filetime_now()
        allocation = ((entry.size + 4095) // 4096) * 4096 if not entry.is_dir else 0
        return FileInfo(
            file_attributes=entry.attributes,
            allocation_size=allocation,
            file_size=entry.size,
            creation_time=now,
            last_access_time=now,
            last_write_time=now,
            change_time=now,
            index_number=max(entry.index, 0),
        )


@dataclass
class OpenedObj:
    entry: TorrentEntry
    security_descriptor: SecurityDescriptor


class TorrentMount:
    def __init__(self, torrent_path: Path, drive_letter: str, cache_gb: int, cache_dir: Path, debug: bool = False):
        self.torrent_path = torrent_path
        self.drive_letter = drive_letter.upper().rstrip(":")
        self.cache_gb = cache_gb
        self.cache_dir = cache_dir
        self.debug = debug
        self.streamer = TorrentStreamer(torrent_path, cache_dir, cache_gb * 1024**3)
        self.fs_service: Optional[FileSystemService] = None

    def run(self) -> None:
        self._validate_environment()
        self.streamer.start()
        metadata = self.streamer.metadata_index
        mountpoint = f"{self.drive_letter}:"
        operations = TorrentFsOperations(metadata, self.streamer, volume_label="TorrentFS")
        self.fs_service = FileSystemService(
            mountpoint,
            operations,
            sector_size=4096,
            sectors_per_allocation_unit=1,
            volume_creation_time=win32_filetime_now(),
            volume_serial_number=0x19831116,
            file_info_timeout=1000,
            case_sensitive_search=0,
            case_preserved_names=1,
            unicode_on_disk=1,
            persistent_acls=1,
            post_cleanup_when_modified_only=1,
            read_only_volume=1,
            um_file_context_is_user_context2=1,
            file_system_name="TorrentFS",
        )
        LOG.info("Mounting %s with cache dir %s", mountpoint, self.cache_dir)
        self.fs_service.start()
        LOG.info("Mounted %s. %s", mountpoint, self.streamer.status_snapshot())
        try:
            while True:
                time.sleep(2)
                LOG.debug(self.streamer.status_snapshot())
        except KeyboardInterrupt:
            LOG.info("Stopping mount")
        finally:
            self.stop()

    def stop(self) -> None:
        if self.fs_service:
            self.fs_service.stop()
        self.streamer.stop()

    def _validate_environment(self) -> None:
        if os.name != "nt":
            raise RuntimeError("This prototype currently supports Windows only.")
        if lt is None:
            raise DependencyError("Install python-libtorrent before running this script.")
        if FileSystemService is None:
            raise DependencyError(
                "Install pywinfspy and the WinFsp driver before running this script."
            )
        if not self.torrent_path.exists():
            raise FileNotFoundError(self.torrent_path)
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        free_bytes = shutil.disk_usage(self.cache_dir).free
        if free_bytes < min(self.cache_gb * 1024**3, 512 * 1024**2):
            LOG.warning("Cache path has very little free space: %s bytes", free_bytes)
        if self.debug and enable_debug_log:
            enable_debug_log()


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Mount a torrent as a read-only virtual Windows drive."
    )
    parser.add_argument("torrent", type=Path, help="Path to the .torrent file")
    parser.add_argument("drive_letter", help="Drive letter to mount, such as D")
    parser.add_argument(
        "cache_gb",
        type=int,
        help="Maximum completed-file cache size in gigabytes",
    )
    parser.add_argument(
        "cache_dir",
        type=Path,
        help="Directory where sparse torrent data and cached files will be stored",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Enable verbose filesystem logging",
    )
    return parser.parse_args(argv)


def configure_logging(debug: bool) -> None:
    logging.basicConfig(
        level=logging.DEBUG if debug else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    configure_logging(args.debug)
    try:
        TorrentMount(
            torrent_path=args.torrent,
            drive_letter=args.drive_letter,
            cache_gb=args.cache_gb,
            cache_dir=args.cache_dir,
            debug=args.debug,
        ).run()
    except DependencyError as exc:
        LOG.error("Missing dependency: %s", exc)
        return 2
    except Exception as exc:
        LOG.exception("Fatal error: %s", exc)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
