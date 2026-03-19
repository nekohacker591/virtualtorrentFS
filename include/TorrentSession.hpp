#pragma once

#include "CacheManager.hpp"
#include "Config.hpp"
#include "TorrentMetadata.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vtfs {

struct FileEntry {
    std::string virtualPath;
    std::uint64_t size = 0;
    std::uint64_t offset = 0;
    std::int32_t index = -1;
    bool isDirectory = false;
};

class TorrentSession {
  public:
    explicit TorrentSession(const Config& config);

    void start();
    [[nodiscard]] std::uint64_t totalSize() const;
    [[nodiscard]] const TorrentMetadata& metadata() const;
    [[nodiscard]] std::optional<FileEntry> lookup(const std::string& path) const;
    [[nodiscard]] std::vector<FileEntry> listDirectory(const std::string& path) const;
    void beginStreaming(const FileEntry& entry);

  private:
    void buildIndex();
    [[nodiscard]] static std::string normalizePath(const std::string& path);

    Config config_;
    CacheManager cacheManager_;
    TorrentMetadata metadata_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, FileEntry> entries_;
    std::unordered_map<std::string, std::vector<FileEntry>> children_;
};

} // namespace vtfs
