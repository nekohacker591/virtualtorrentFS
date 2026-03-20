#pragma once

#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace vtfs {

struct TorrentFileRecord {
    std::string relativePath;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
};

class TorrentMetadata {
  public:
    static TorrentMetadata loadFromFile(const std::filesystem::path& path);

    [[nodiscard]] const std::string& name() const;
    [[nodiscard]] const std::string& infoHashHex() const;
    [[nodiscard]] std::uint64_t totalSize() const;
    [[nodiscard]] std::uint64_t pieceLength() const;
    [[nodiscard]] std::size_t pieceCount() const;
    [[nodiscard]] const std::vector<TorrentFileRecord>& files() const;
    [[nodiscard]] const std::string& announceUrl() const;
    [[nodiscard]] const std::vector<std::array<std::uint8_t, 20>>& pieceHashes() const;

  private:
    std::string name_;
    std::string infoHashHex_;
    std::string announceUrl_;
    std::uint64_t totalSize_ = 0;
    std::uint64_t pieceLength_ = 0;
    std::size_t pieceCount_ = 0;
    std::vector<std::array<std::uint8_t, 20>> pieceHashes_;
    std::vector<TorrentFileRecord> files_;
};

} // namespace vtfs
