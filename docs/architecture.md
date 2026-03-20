# Architecture

## Implemented now

### 1. Internal torrent metadata core

`Bencode`, `Sha1`, and `TorrentMetadata` are all implemented inside this repository with no external libraries.

### 2. Virtual namespace model

`TorrentSession` materializes the torrent as a read-only tree of directories and files immediately from metadata.

### 3. Native Windows mount path

`VirtualDrive` now uses a Windows ProjFS provider on `_WIN32` builds. It:

- creates a virtualization root
- registers ProjFS callbacks
- answers directory enumeration requests
- answers placeholder info requests
- maps the requested drive letter to the virtualization root
- serves read requests from the local payload cache when data is already present

### 4. Remaining major subsystem

What still has to be implemented for full torrent streaming is the actual peer transport engine:

- tracker communication
- peer wire protocol
- handshake/choke/interested flow
- piece/block scheduling
- piece verification
- persistent download state
- read-driven block prioritization

## Why this is the right split

The user requested that the mount layer be implemented in-house instead of relying on Dokan or another external dependency. This revision does that with a native Windows provider while keeping the torrent transport layer as the next isolated milestone.
