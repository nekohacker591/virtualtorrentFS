#!/usr/bin/env python3
"""Mount a .torrent as a read-only Windows drive and stream files on open.

This prototype targets Windows and uses:
- libtorrent for piece selection and metadata loading
- WinFsp/winfspy for the virtual filesystem surface

Example:
    python mounttorrent.py example.torrent d 2 d:/cache
"""

from __future__ import annotations

import argparse
import logging
import os
import threading
import time
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path, PureWindowsPath
from typing import Dict, Iterable, List, Optional

import libtorrent as lt
from winfspy import (
    BaseFileSystemOperations,
    FILE_ATTRIBUTE,
    FileSystem,
    NTStatusAccessDenied,
    NTStatusDirectoryNotEmpty,
    NTStatusEndOfFile,
    NTStatusMediaWriteProtected,
    NTStatusNotADirectory,
    NTStatusObjectNameNotFound,
)
from winfspy.plumbing.security_descriptor import SecurityDescriptor
from winfspy.plumbing.win32_filetime import filetime_now


LOG = logging.getLogger("mounttorrent")
READ_CHUNK_SIZE = 256 * 1024
PIECE_POLL_SECONDS = 0.15
CACHE_SWEEP_SECONDS = 10.0
DEFAULT_VOLUME_LABEL = "TorrentFS"


def operation(fn):
    """Provide coarse thread safety and structured debug logging."""

    def wrapper(self, *args, **kwargs):
        head = args[0] if args else None
        tail = args[1:] if args else ()
        with self._thread_lock:
            try:
                result = fn(self, *args, **kwargs)
            except Exception as exc:
                LOG.debug("NOK %s head=%r tail=%r exc=%r", fn.__name__, head, tail, exc)
                raise
            LOG.debug("OK %s head=%r tail=%r result=%r", fn.__name__, head, tail, result)
            return result

    return wrapper


@dataclass
class TorrentNode:
    path: PureWindowsPath
    attributes: int
    security_descriptor: SecurityDescriptor
    creation_time: int = field(default_factory=filetime_now)
    last_access_time: int = field(default_factory=filetime_now)
    last_write_time: int = field(default_factory=filetime_now)
    change_time: int = field(default_factory=filetime_now)
    index_number: int = 0
    file_size: int = 0
    allocation_size: int = 0

    @property
    def name(self) -> str:
        return self.path.name

    @property
    def file_name(self) -> str:
        return str(self.path)

    def get_file_info(self) -> dict:
        return {
            "file_attributes": self.attributes,
            "allocation_size": self.allocation_size,
            "file_size": self.file_size,
            "creation_time": self.creation_time,
            "last_access_time": self.last_access_time,
            "last_write_time": self.last_write_time,
            "change_time": self.change_time,
            "index_number": self.index_number,
        }


@dataclass
class TorrentFolder(TorrentNode):
    pass


@dataclass
class TorrentFile(TorrentNode):
    file_index: int = -1
    cache_path: Path = Path('.')
    piece_length: int = 0
    first_piece: int = 0
    last_piece: int = 0

    def __post_init__(self) -> None:
        self.attributes |= FILE_ATTRIBUTE.FILE_ATTRIBUTE_ARCHIVE

    def read(self, manager: "TorrentManager", offset: int, length: int) -> bytes:
        if offset >= self.file_size:
            raise NTStatusEndOfFile()
        length = min(length, self.file_size - offset)
        manager.ensure_range(self, offset, length)
        with self.cache_path.open("rb") as handle:
            handle.seek(offset)
            return handle.read(length)


@dataclass
class OpenedObj:
    file_obj: TorrentNode


class TorrentManager:
    """Own the libtorrent session and file-on-demand policy."""

    def __init__(self, torrent_path: Path, cache_dir: Path, cache_limit_gb: int) -> None:
        self.torrent_path = torrent_path
        self.cache_dir = cache_dir
        self.cache_limit_bytes = cache_limit_gb * 1024**3
        self._session = lt.session({
            "enable_dht": True,
            "enable_lsd": True,
            "enable_upnp": True,
            "enable_natpmp": True,
            "alert_mask": lt.alert.category_t.status_notification
            | lt.alert.category_t.error_notification
            | lt.alert.category_t.storage_notification,
            "active_downloads": 8,
            "connections_limit": 200,
        })
        self._handle: Optional[lt.torrent_handle] = None
        self._info: Optional[lt.torrent_info] = None
        self._files = None
        self._piece_length = 0
        self._lock = threading.RLock()
        self._active_files: Dict[int, int] = {}
        self._last_access: "OrderedDict[int, float]" = OrderedDict()
        self._stop_event = threading.Event()
        self._cache_thread = threading.Thread(target=self._cache_maintainer, daemon=True)

    def start(self) -> None:
        self.cache_dir.mkdir(parents=True, exist_ok=True)
        info = lt.torrent_info(str(self.torrent_path))
        params = {
            "ti": info,
            "save_path": str(self.cache_dir),
            "flags": lt.torrent_flags.default_flags | lt.torrent_flags.upload_mode,
        }
        handle = self._session.add_torrent(params)
        self._handle = handle
        self._info = info
        self._files = info.files()
        self._piece_length = info.piece_length()
        handle.resume()
        self._set_all_file_priorities(0)
        self._cache_thread.start()
        LOG.info("Loaded torrent metadata for %s", info.name())

    def stop(self) -> None:
        self._stop_event.set()
        if self._cache_thread.is_alive():
            self._cache_thread.join(timeout=2)
        if self._handle is not None:
            self._session.remove_torrent(self._handle)

    @property
    def torrent_info(self) -> lt.torrent_info:
        assert self._info is not None
        return self._info

    @property
    def piece_length(self) -> int:
        return self._piece_length

    def iter_files(self) -> Iterable[dict]:
        assert self._files is not None
        for file_index in range(self._files.num_files()):
            file_storage = self._files
            yield {
                "index": file_index,
                "path": Path(file_storage.file_path(file_index)),
                "size": file_storage.file_size(file_index),
                "offset": file_storage.file_offset(file_index),
            }

    def cache_path_for(self, relative_path: Path) -> Path:
        return self.cache_dir / relative_path

    def mark_open(self, file_index: int) -> None:
        with self._lock:
            self._active_files[file_index] = self._active_files.get(file_index, 0) + 1
            self._last_access[file_index] = time.time()
            self._last_access.move_to_end(file_index)
            self._set_file_priority(file_index, 7)

    def mark_close(self, file_index: int) -> None:
        with self._lock:
            count = self._active_files.get(file_index, 0)
            if count <= 1:
                self._active_files.pop(file_index, None)
            else:
                self._active_files[file_index] = count - 1
            self._last_access[file_index] = time.time()
            self._last_access.move_to_end(file_index)

    def ensure_range(self, file_obj: TorrentFile, offset: int, length: int) -> None:
        start_piece = file_obj.first_piece + (offset // file_obj.piece_length)
        end_piece = file_obj.first_piece + ((offset + length - 1) // file_obj.piece_length)
        self._prioritize_piece_window(start_piece, end_piece)
        self._wait_for_pieces(start_piece, end_piece)
        with self._lock:
            self._last_access[file_obj.file_index] = time.time()
            self._last_access.move_to_end(file_obj.file_index)

    def _set_all_file_priorities(self, priority: int) -> None:
        assert self._handle is not None
        count = self.torrent_info.num_files()
        self._handle.prioritize_files([priority] * count)

    def _set_file_priority(self, file_index: int, priority: int) -> None:
        assert self._handle is not None
        priorities = list(self._handle.get_file_priorities())
        priorities[file_index] = priority
        self._handle.prioritize_files(priorities)

    def _prioritize_piece_window(self, start_piece: int, end_piece: int) -> None:
        assert self._handle is not None
        horizon = 4
        updates = []
        for piece in range(start_piece, min(end_piece + 1 + horizon, self.torrent_info.num_pieces())):
            priority = 7 if piece <= end_piece else 5
            updates.append((piece, priority))
        if updates:
            self._handle.prioritize_pieces(updates)
            self._handle.set_sequential_download(True)

    def _wait_for_pieces(self, start_piece: int, end_piece: int) -> None:
        assert self._handle is not None
        while not self._stop_event.is_set():
            status = self._handle.status()
            if status.errc.value():
                raise RuntimeError(f"Torrent error: {status.errc.message()}")
            pieces = status.pieces
            if pieces and all(pieces[piece] for piece in range(start_piece, end_piece + 1)):
                return
            time.sleep(PIECE_POLL_SECONDS)

    def _cache_maintainer(self) -> None:
        while not self._stop_event.wait(CACHE_SWEEP_SECONDS):
            try:
                self._evict_if_needed()
            except Exception as exc:
                LOG.warning("Cache sweep failed: %s", exc)

    def _evict_if_needed(self) -> None:
        if self.cache_limit_bytes <= 0:
            return
        total = self._directory_size(self.cache_dir)
        if total <= self.cache_limit_bytes:
            return
        with self._lock:
            candidates = [index for index in self._last_access if index not in self._active_files]
        for file_index in candidates:
            relative = Path(self.torrent_info.files().file_path(file_index))
            full_path = self.cache_dir / relative
            if not full_path.exists() or not full_path.is_file():
                continue
            size = full_path.stat().st_size
            full_path.unlink(missing_ok=True)
            total -= size
            LOG.info("Evicted cached file %s", full_path)
            if total <= self.cache_limit_bytes:
                return

    @staticmethod
    def _directory_size(path: Path) -> int:
        total = 0
        for root, _dirs, files in os.walk(path):
            for name in files:
                try:
                    total += (Path(root) / name).stat().st_size
                except FileNotFoundError:
                    continue
        return total


class TorrentFileSystemOperations(BaseFileSystemOperations):
    def __init__(self, manager: TorrentManager, volume_label: str = DEFAULT_VOLUME_LABEL):
        super().__init__()
        if len(volume_label) > 31:
            raise ValueError("volume label must be 31 characters or fewer")
        self.manager = manager
        self._thread_lock = threading.RLock()
        self._root_path = PureWindowsPath("/")
        self._volume_info = {
            "total_size": manager.torrent_info.total_size(),
            "free_size": 0,
            "volume_label": volume_label,
        }
        self._root_obj = TorrentFolder(
            path=self._root_path,
            attributes=FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY,
            security_descriptor=SecurityDescriptor.from_string(
                "O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FR;;;WD)"
            ),
        )
        self._entries: Dict[PureWindowsPath, TorrentNode] = {self._root_path: self._root_obj}
        self._populate_tree()

    def _populate_tree(self) -> None:
        next_index = 1
        for item in self.manager.iter_files():
            relative = item["path"]
            current = self._root_path
            for part in relative.parts[:-1]:
                current = current / part
                if current not in self._entries:
                    self._entries[current] = TorrentFolder(
                        path=current,
                        attributes=FILE_ATTRIBUTE.FILE_ATTRIBUTE_DIRECTORY,
                        security_descriptor=self._root_obj.security_descriptor,
                        index_number=next_index,
                    )
                    next_index += 1
            file_path = self._root_path / str(relative).replace("\\", "/")
            size = item["size"]
            first_piece = item["offset"] // self.manager.piece_length
            last_piece = (item["offset"] + max(size - 1, 0)) // self.manager.piece_length if size else first_piece
            cache_path = self.manager.cache_path_for(relative)
            cache_path.parent.mkdir(parents=True, exist_ok=True)
            self._entries[file_path] = TorrentFile(
                path=file_path,
                attributes=FILE_ATTRIBUTE.FILE_ATTRIBUTE_READONLY,
                security_descriptor=self._root_obj.security_descriptor,
                index_number=next_index,
                file_size=size,
                allocation_size=size,
                file_index=item["index"],
                cache_path=cache_path,
                piece_length=self.manager.piece_length,
                first_piece=first_piece,
                last_piece=last_piece,
            )
            next_index += 1

    @operation
    def get_volume_info(self):
        return self._volume_info

    @operation
    def set_volume_label(self, volume_label):
        self._volume_info["volume_label"] = volume_label

    @operation
    def get_security_by_name(self, file_name):
        file_obj = self._lookup(file_name)
        return (
            file_obj.attributes,
            file_obj.security_descriptor.handle,
            file_obj.security_descriptor.size,
        )

    def _lookup(self, file_name: str) -> TorrentNode:
        path = PureWindowsPath(file_name)
        try:
            return self._entries[path]
        except KeyError as exc:
            raise NTStatusObjectNameNotFound() from exc

    @operation
    def open(self, file_name, create_options, granted_access):
        file_obj = self._lookup(file_name)
        if isinstance(file_obj, TorrentFile):
            self.manager.mark_open(file_obj.file_index)
        return OpenedObj(file_obj)

    @operation
    def create(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def close(self, file_context):
        if isinstance(file_context.file_obj, TorrentFile):
            self.manager.mark_close(file_context.file_obj.file_index)

    @operation
    def get_file_info(self, file_context):
        return file_context.file_obj.get_file_info()

    @operation
    def get_security(self, file_context):
        return file_context.file_obj.security_descriptor

    @operation
    def set_security(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def rename(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def set_basic_info(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def set_file_size(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def can_delete(self, file_context, file_name):
        file_obj = self._lookup(file_name)
        if isinstance(file_obj, TorrentFolder):
            for entry_path in self._entries:
                if entry_path != file_obj.path and entry_path.parent == file_obj.path:
                    raise NTStatusDirectoryNotEmpty()
        raise NTStatusMediaWriteProtected()

    @operation
    def get_dir_info_by_name(self, file_context, file_name):
        path = file_context.file_obj.path / file_name
        try:
            entry_obj = self._entries[path]
        except KeyError as exc:
            raise NTStatusObjectNameNotFound() from exc
        return {"file_name": file_name, **entry_obj.get_file_info()}

    @operation
    def read_directory(self, file_context, marker):
        file_obj = file_context.file_obj
        if isinstance(file_obj, TorrentFile):
            raise NTStatusNotADirectory()
        entries = []
        if file_obj.path != self._root_path:
            parent = self._entries[file_obj.path.parent]
            entries.append({"file_name": ".", **file_obj.get_file_info()})
            entries.append({"file_name": "..", **parent.get_file_info()})
        for entry_path, entry in self._entries.items():
            try:
                relative = entry_path.relative_to(file_obj.path)
            except ValueError:
                continue
            if len(relative.parts) != 1:
                continue
            entries.append({"file_name": entry_path.name, **entry.get_file_info()})
        entries.sort(key=lambda item: item["file_name"])
        if marker is None:
            return entries
        for index, entry in enumerate(entries):
            if entry["file_name"] == marker:
                return entries[index + 1 :]
        return []

    @operation
    def read(self, file_context, offset, length):
        file_obj = file_context.file_obj
        if isinstance(file_obj, TorrentFolder):
            raise NTStatusAccessDenied()
        return file_obj.read(self.manager, offset, length)

    @operation
    def write(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def cleanup(self, *args, **kwargs):
        return None

    @operation
    def overwrite(self, *args, **kwargs):
        raise NTStatusMediaWriteProtected()

    @operation
    def flush(self, file_context):
        return None


class TorrentMountApp:
    def __init__(self, torrent_file: Path, drive_letter: str, cache_limit_gb: int, cache_dir: Path):
        self.drive_letter = drive_letter.rstrip(":") + ":"
        self.manager = TorrentManager(torrent_file, cache_dir, cache_limit_gb)
        self.fs: Optional[FileSystem] = None

    def start(self) -> None:
        self.manager.start()
        operations = TorrentFileSystemOperations(self.manager)
        mountpoint = self.drive_letter
        is_drive = True
        self.fs = FileSystem(
            mountpoint,
            operations,
            sector_size=4096,
            sectors_per_allocation_unit=1,
            volume_creation_time=filetime_now(),
            volume_serial_number=0x5151A11,
            file_info_timeout=1000,
            case_sensitive_search=0,
            case_preserved_names=1,
            unicode_on_disk=1,
            persistent_acls=1,
            post_cleanup_when_modified_only=1,
            um_file_context_is_user_context2=1,
            file_system_name="TorrentFS",
            prefix="",
            debug=False,
            reject_irp_prior_to_transact0=not is_drive,
        )
        self.fs.start()
        LOG.info("Mounted %s on %s", self.manager.torrent_path, mountpoint)

    def stop(self) -> None:
        if self.fs is not None:
            self.fs.stop()
        self.manager.stop()

    def wait_forever(self) -> None:
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            LOG.info("Stopping mount")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("torrent", type=Path, help="Path to a .torrent file")
    parser.add_argument("drive_letter", help="Drive letter to mount, for example D")
    parser.add_argument("cache_limit_gb", type=int, help="Soft cache size in gigabytes")
    parser.add_argument("cache_dir", type=Path, help="Directory where downloaded data is cached")
    parser.add_argument("--debug", action="store_true", help="Enable verbose debug logging")
    return parser.parse_args(argv)


def validate_windows_environment(args: argparse.Namespace) -> None:
    if os.name != "nt":
        raise SystemExit("This prototype only runs on Windows because it requires WinFsp.")
    if args.cache_limit_gb < 0:
        raise SystemExit("cache_limit_gb must be 0 or greater")
    if args.torrent.suffix.lower() != ".torrent":
        raise SystemExit("torrent must point to a .torrent file")
    if not args.torrent.exists():
        raise SystemExit(f"torrent file not found: {args.torrent}")


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    validate_windows_environment(args)
    app = TorrentMountApp(args.torrent, args.drive_letter, args.cache_limit_gb, args.cache_dir)
    app.start()
    try:
        app.wait_forever()
    finally:
        app.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
