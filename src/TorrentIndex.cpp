#include "virtualtorrentfs/TorrentIndex.hpp"

namespace vtfs {

void TorrentIndex::setTotalSize(std::uint64_t size) {
    totalSize_ = size;
}

void TorrentIndex::addFile(TorrentFileEntry entry) {
    files_.push_back(std::move(entry));
}

std::uint64_t TorrentIndex::totalSize() const {
    return totalSize_;
}

const std::vector<TorrentFileEntry>& TorrentIndex::files() const {
    return files_;
}

std::optional<TorrentFileEntry> TorrentIndex::findByPath(const std::filesystem::path& path) const {
    for (const auto& file : files_) {
        if (file.path == path) {
            return file;
        }
    }
    return std::nullopt;
}

} // namespace vtfs
