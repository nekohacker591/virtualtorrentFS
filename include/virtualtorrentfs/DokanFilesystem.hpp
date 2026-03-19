#pragma once

#include "virtualtorrentfs/CacheManager.hpp"
#include "virtualtorrentfs/LibtorrentSession.hpp"
#include "virtualtorrentfs/VirtualDriveManifest.hpp"

namespace vtfs {

class DokanFilesystem {
public:
    DokanFilesystem(LibtorrentSession& torrent, VirtualDriveManifest& manifest, CacheManager& cache);
    bool mount(wchar_t driveLetter);

private:
    LibtorrentSession& torrent_;
    VirtualDriveManifest& manifest_;
    CacheManager& cache_;
};

} // namespace vtfs
