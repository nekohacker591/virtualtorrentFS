#include "TorrentSession.hpp"

#include <set>

namespace vtfs {

namespace {
std::string parentPathOf(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos || pos == 0) {
        return "/";
    }
    return path.substr(0, pos);
}
}

TorrentSession::TorrentSession(const Config& config)
    : config_(config),
      cacheManager_(config.cacheDirectory, config.cacheSizeBytes) {}

void TorrentSession::start() {
    cacheManager_.ensureLayout();
    metadata_ = TorrentMetadata::loadFromFile(config_.torrentFile);
    buildIndex();
}

std::uint64_t TorrentSession::totalSize() const {
    return metadata_.totalSize();
}

const TorrentMetadata& TorrentSession::metadata() const {
    return metadata_;
}

std::optional<FileEntry> TorrentSession::lookup(const std::string& path) const {
    const auto normalized = normalizePath(path);
    std::shared_lock lock(mutex_);
    if (const auto it = entries_.find(normalized); it != entries_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<FileEntry> TorrentSession::listDirectory(const std::string& path) const {
    const auto normalized = normalizePath(path);
    std::shared_lock lock(mutex_);
    if (const auto it = children_.find(normalized); it != children_.end()) {
        return it->second;
    }
    return {};
}

void TorrentSession::beginStreaming(const FileEntry& entry) {
    cacheManager_.touch(entry.virtualPath, entry.size);
    cacheManager_.evictIfNeeded();
}

void TorrentSession::buildIndex() {
    std::unique_lock lock(mutex_);
    entries_.clear();
    children_.clear();

    entries_["/"] = FileEntry{"/", 0, 0, -1, true};
    children_["/"] = {};
    std::set<std::string> seenDirectories{"/"};

    for (std::size_t i = 0; i < metadata_.files().size(); ++i) {
        const auto& record = metadata_.files()[i];
        const auto path = normalizePath(record.relativePath);
        FileEntry fileEntry{path, record.size, record.offset, static_cast<std::int32_t>(i), false};
        entries_[path] = fileEntry;

        std::string current = "/";
        std::string segment;
        for (std::size_t idx = 1; idx < path.size(); ++idx) {
            if (path[idx] == '/') {
                if (!segment.empty()) {
                    if (current.size() > 1) {
                        current += '/';
                    }
                    current += segment;
                    if (seenDirectories.insert(current).second) {
                        FileEntry dirEntry{current, 0, 0, -1, true};
                        entries_[current] = dirEntry;
                        children_[parentPathOf(current)].push_back(dirEntry);
                    }
                    segment.clear();
                }
            } else {
                segment.push_back(path[idx]);
            }
        }

        children_[parentPathOf(path)].push_back(fileEntry);
    }
}

std::string TorrentSession::normalizePath(const std::string& path) {
    if (path.empty() || path == "/") {
        return "/";
    }
    std::string out;
    out.reserve(path.size() + 1);
    if (path.front() != '/') {
        out.push_back('/');
    }
    for (char ch : path) {
        out.push_back(ch == '\\' ? '/' : ch);
    }
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return out;
}

} // namespace vtfs
