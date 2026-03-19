#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace vtfs {

class CacheManager {
  public:
    CacheManager(std::filesystem::path root, std::uint64_t maxBytes);

    void ensureLayout();
    void touch(const std::string& key, std::uint64_t sizeBytes);
    void evictIfNeeded();

    [[nodiscard]] std::filesystem::path payloadRoot() const;
    [[nodiscard]] std::filesystem::path stateRoot() const;

  private:
    struct Entry {
        std::uint64_t sizeBytes {};
        std::uint64_t tick {};
    };

    std::filesystem::path root_;
    std::uint64_t maxBytes_;
    mutable std::mutex mutex_;
    std::uint64_t tick_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace vtfs
