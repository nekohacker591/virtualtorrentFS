#include "CacheManager.hpp"

#include <algorithm>
#include <vector>

namespace vtfs {

CacheManager::CacheManager(std::filesystem::path root, std::uint64_t maxBytes)
    : root_(std::move(root)), maxBytes_(maxBytes) {}

void CacheManager::ensureLayout() {
    std::filesystem::create_directories(payloadRoot());
    std::filesystem::create_directories(stateRoot());
}

void CacheManager::touch(const std::string& key, std::uint64_t sizeBytes) {
    std::scoped_lock lock(mutex_);
    entries_[key] = Entry{sizeBytes, ++tick_};
}

void CacheManager::evictIfNeeded() {
    std::scoped_lock lock(mutex_);
    std::uint64_t total = 0;
    for (const auto& [_, entry] : entries_) {
        total += entry.sizeBytes;
    }
    if (total <= maxBytes_) {
        return;
    }

    std::vector<std::pair<std::string, Entry>> ordered(entries_.begin(), entries_.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.second.tick < rhs.second.tick;
    });

    for (const auto& [key, entry] : ordered) {
        std::error_code ec;
        std::filesystem::remove(payloadRoot() / key, ec);
        total -= entry.sizeBytes;
        entries_.erase(key);
        if (total <= maxBytes_) {
            break;
        }
    }
}

std::filesystem::path CacheManager::payloadRoot() const {
    return root_ / "payload";
}

std::filesystem::path CacheManager::stateRoot() const {
    return root_ / "state";
}

} // namespace vtfs
