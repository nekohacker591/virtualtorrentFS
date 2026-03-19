#include "VirtualDrive.hpp"

#include <iostream>

namespace vtfs {

int NullVirtualDriveProvider::mount(const Config& config, const TorrentSession& session) {
    std::cout
        << "VirtualTorrentFS loaded torrent metadata successfully.\n"
        << "Drive letter requested: " << config.driveLetter << ":\\\n"
        << "Torrent name: " << session.metadata().name() << "\n"
        << "Info hash: " << session.metadata().infoHashHex() << "\n"
        << "Logical size: " << session.totalSize() << " bytes\n"
        << "Files exposed: " << session.metadata().files().size() << "\n\n"
        << "This dependency-free build contains the internal torrent metadata engine and virtual namespace model,\n"
        << "but not a finished custom Windows filesystem driver/mount provider yet.\n";
    return 2;
}

VirtualDrive::VirtualDrive(const Config& config, std::shared_ptr<TorrentSession> session)
    : config_(config),
      session_(std::move(session)),
      provider_(std::make_unique<NullVirtualDriveProvider>()) {}

int VirtualDrive::mount() {
    return provider_->mount(config_, *session_);
}

} // namespace vtfs
