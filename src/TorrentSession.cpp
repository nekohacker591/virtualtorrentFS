#include "TorrentSession.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
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
      cacheManager_(config.cacheDirectory, config.cacheSizeBytes),
      metadata_(),
      engine_(nullptr) {}

void TorrentSession::start() {
    cacheManager_.ensureLayout();
    metadata_ = TorrentMetadata::loadFromFile(config_.torrentFile);
    engine_ = std::make_unique<TorrentEngine>(metadata_);
    engine_->start();
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

std::filesystem::path TorrentSession::payloadPathFor(const FileEntry& entry) const {
    auto relative = entry.virtualPath;
    if (!relative.empty() && relative.front() == '/') {
        relative.erase(relative.begin());
    }
    for (char& ch : relative) {
        if (ch == '/') {
            ch = std::filesystem::path::preferred_separator;
        }
    }
    return config_.cacheDirectory / "payload" / relative;
}

bool TorrentSession::tryReadFileRange(const FileEntry& entry,
                                      std::uint64_t offset,
                                      void* buffer,
                                      std::uint32_t bytesToRead,
                                      std::uint32_t& bytesRead) const {
    bytesRead = 0;
    if (entry.isDirectory || offset >= entry.size) {
        return true;
    }

    const auto available = static_cast<std::uint32_t>(std::min<std::uint64_t>(bytesToRead, entry.size - offset));
    const auto path = payloadPathFor(entry);
    if (!std::filesystem::exists(path)) {
        return false;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }

    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    in.read(static_cast<char*>(buffer), static_cast<std::streamsize>(available));
    bytesRead = static_cast<std::uint32_t>(in.gcount());
    return true;
}

void TorrentSession::beginStreaming(const FileEntry& entry) const {
    queueRead(entry, 0, static_cast<std::uint32_t>(std::min<std::uint64_t>(entry.size, 256 * 1024ULL)));
}

void TorrentSession::queueRead(const FileEntry& entry, std::uint64_t offset, std::uint32_t length) const {
    cacheManager_.touch(entry.virtualPath, entry.size);
    cacheManager_.evictIfNeeded();
    if (engine_) {
        engine_->requestFileData(entry.index, offset, length);
        engine_->pumpOnce();
    }
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
