#include "virtualtorrentfs/VirtualDriveManifest.hpp"

namespace vtfs {

void VirtualDriveManifest::build(const TorrentIndex& index) {
    files_.clear();
    for (const auto& file : index.files()) {
        files_.emplace(file.path.wstring(), file);
    }
}

bool VirtualDriveManifest::exists(const std::filesystem::path& path) const {
    return files_.contains(path.wstring());
}

std::optional<TorrentFileEntry> VirtualDriveManifest::file(const std::filesystem::path& path) const {
    if (auto iter = files_.find(path.wstring()); iter != files_.end()) {
        return iter->second;
    }
    return std::nullopt;
}

std::vector<TorrentFileEntry> VirtualDriveManifest::listDirectory(const std::filesystem::path& path) const {
    std::vector<TorrentFileEntry> results;
    for (const auto& [key, entry] : files_) {
        const auto parent = entry.path.parent_path();
        if (parent == path) {
            results.push_back(entry);
        }
    }
    return results;
}

} // namespace vtfs
