#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace vtfs {

struct CacheEntry {
    std::filesystem::path path;
    std::uint64_t size = 0;
    bool complete = false;
};

class CacheManager {
public:
    explicit CacheManager(std::uint64_t budgetBytes);

    void touch(const CacheEntry& entry);
    void markComplete(const std::filesystem::path& path);
    [[nodiscard]] std::uint64_t budgetBytes() const;
    [[nodiscard]] std::uint64_t usedBytes() const;
    [[nodiscard]] std::vector<std::filesystem::path> evictionCandidates() const;

private:
    void trim();

    std::uint64_t budgetBytes_;
    std::uint64_t usedBytes_ = 0;
    std::deque<std::filesystem::path> lru_;
    std::unordered_map<std::wstring, CacheEntry> entries_;
};

} // namespace vtfs
