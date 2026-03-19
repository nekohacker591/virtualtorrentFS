#pragma once

#include "virtualtorrentfs/Config.hpp"
#include "virtualtorrentfs/TorrentIndex.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace vtfs {

class LibtorrentSession {
public:
    explicit LibtorrentSession(Config config);

    bool initialize();
    bool prefetchMetadata();
    [[nodiscard]] TorrentIndex buildIndex() const;
    [[nodiscard]] bool prepareFileForStreaming(const std::filesystem::path& path);
    [[nodiscard]] std::vector<std::byte> read(const std::filesystem::path& path, std::uint64_t offset, std::size_t length);

private:
    Config config_;
};

} // namespace vtfs
