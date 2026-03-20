#pragma once

#include "CacheManager.hpp"
#include "Config.hpp"
#include "TorrentEngine.hpp"
#include "TorrentMetadata.hpp"
#include "WebSeedClient.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_set>
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
    [[nodiscard]] std::filesystem::path payloadPathFor(const FileEntry& entry) const;
    bool tryReadFileRange(const FileEntry& entry, std::uint64_t offset, void* buffer, std::uint32_t bytesToRead,
                          std::uint32_t& bytesRead) const;
    void beginStreaming(const FileEntry& entry, std::uint64_t offset = 0, std::uint32_t length = 0) const;
    void queueRead(const FileEntry& entry, std::uint64_t offset, std::uint32_t length) const;
    [[nodiscard]] bool isHydrating(const FileEntry& entry) const;

  private:
    void buildIndex();
    void ensureWebSeedDownload(const FileEntry& entry) const;
    [[nodiscard]] static std::string normalizePath(const std::string& path);

    Config config_;
    mutable CacheManager cacheManager_;
    TorrentMetadata metadata_;
    mutable std::unique_ptr<TorrentEngine> engine_;
    mutable WebSeedClient webSeedClient_;

    mutable std::shared_mutex mutex_;
    mutable std::mutex webSeedMutex_;
    mutable std::unordered_set<std::string> webSeedInFlight_;
    std::unordered_map<std::string, FileEntry> entries_;
    std::unordered_map<std::string, std::vector<FileEntry>> children_;
};

} // namespace vtfs
