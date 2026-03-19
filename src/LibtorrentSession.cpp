#include "virtualtorrentfs/LibtorrentSession.hpp"
#include "virtualtorrentfs/Logger.hpp"

#include <fstream>

namespace vtfs {

LibtorrentSession::LibtorrentSession(Config config) : config_(std::move(config)) {}

bool LibtorrentSession::initialize() {
    Logger::instance().log(LogLevel::Info, L"Initializing torrent session.");
    return true;
}

bool LibtorrentSession::prefetchMetadata() {
    Logger::instance().log(LogLevel::Info, L"Prefetching torrent metadata before mount.");
    return std::filesystem::exists(config_.torrentFile);
}

TorrentIndex LibtorrentSession::buildIndex() const {
    TorrentIndex index;

    // Placeholder manifest until real libtorrent parsing is added.
    index.setTotalSize(14ull * 1024ull * 1024ull * 1024ull * 1024ull);
    index.addFile({L"Music\\Example Artist\\Example Track 01.flac", 40ull * 1024ull * 1024ull, 0, 0, 320});
    index.addFile({L"Music\\Example Artist\\Example Track 02.flac", 42ull * 1024ull * 1024ull, 40ull * 1024ull * 1024ull, 320, 336});
    return index;
}

bool LibtorrentSession::prepareFileForStreaming(const std::filesystem::path& path) {
    Logger::instance().log(LogLevel::Info, L"Preparing file for streaming: " + path.wstring());
    return true;
}

std::vector<std::byte> LibtorrentSession::read(const std::filesystem::path& path, std::uint64_t offset, std::size_t length) {
    Logger::instance().log(LogLevel::Debug, L"Read request for " + path.wstring() + L" offset=" + std::to_wstring(offset) + L" length=" + std::to_wstring(length));
    return std::vector<std::byte>(length, std::byte{0});
}

} // namespace vtfs
