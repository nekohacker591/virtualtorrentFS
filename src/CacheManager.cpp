#include "virtualtorrentfs/CacheManager.hpp"

#include <algorithm>

namespace vtfs {

CacheManager::CacheManager(std::uint64_t budgetBytes) : budgetBytes_(budgetBytes) {}

void CacheManager::touch(const CacheEntry& entry) {
    const auto key = entry.path.wstring();
    auto iter = entries_.find(key);
    if (iter == entries_.end()) {
        entries_.emplace(key, entry);
        usedBytes_ += entry.size;
    } else {
        usedBytes_ -= iter->second.size;
        iter->second = entry;
        usedBytes_ += entry.size;
    }

    lru_.erase(std::remove(lru_.begin(), lru_.end(), entry.path), lru_.end());
    lru_.push_front(entry.path);
    trim();
}

void CacheManager::markComplete(const std::filesystem::path& path) {
    const auto key = path.wstring();
    if (auto iter = entries_.find(key); iter != entries_.end()) {
        iter->second.complete = true;
    }
}

std::uint64_t CacheManager::budgetBytes() const {
    return budgetBytes_;
}

std::uint64_t CacheManager::usedBytes() const {
    return usedBytes_;
}

std::vector<std::filesystem::path> CacheManager::evictionCandidates() const {
    std::vector<std::filesystem::path> candidates;
    for (auto iter = lru_.rbegin(); iter != lru_.rend(); ++iter) {
        const auto found = entries_.find(iter->wstring());
        if (found != entries_.end() && found->second.complete) {
            candidates.push_back(*iter);
        }
    }
    return candidates;
}

void CacheManager::trim() {
    while (usedBytes_ > budgetBytes_ && !lru_.empty()) {
        const auto path = lru_.back();
        lru_.pop_back();
        const auto key = path.wstring();
        auto iter = entries_.find(key);
        if (iter == entries_.end()) {
            continue;
        }

        usedBytes_ -= iter->second.size;
        entries_.erase(iter);
    }
}

} // namespace vtfs
