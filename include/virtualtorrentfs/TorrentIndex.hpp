#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

struct TorrentFileEntry {
    std::filesystem::path path;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    std::uint32_t firstPiece = 0;
    std::uint32_t pieceCount = 0;
};

class TorrentIndex {
public:
    void setTotalSize(std::uint64_t size);
    void addFile(TorrentFileEntry entry);

    [[nodiscard]] std::uint64_t totalSize() const;
    [[nodiscard]] const std::vector<TorrentFileEntry>& files() const;
    [[nodiscard]] std::optional<TorrentFileEntry> findByPath(const std::filesystem::path& path) const;

private:
    std::uint64_t totalSize_ = 0;
    std::vector<TorrentFileEntry> files_;
};

} // namespace vtfs
