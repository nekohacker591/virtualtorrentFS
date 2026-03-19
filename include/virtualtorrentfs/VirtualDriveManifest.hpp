#pragma once

#include "virtualtorrentfs/TorrentIndex.hpp"

#include <filesystem>
#include <optional>
#include <unordered_map>

namespace vtfs {

class VirtualDriveManifest {
public:
    void build(const TorrentIndex& index);
    [[nodiscard]] bool exists(const std::filesystem::path& path) const;
    [[nodiscard]] std::optional<TorrentFileEntry> file(const std::filesystem::path& path) const;
    [[nodiscard]] std::vector<TorrentFileEntry> listDirectory(const std::filesystem::path& path) const;

private:
    std::unordered_map<std::wstring, TorrentFileEntry> files_;
};

} // namespace vtfs
