#include "Config.hpp"

#include <cctype>
#include <limits>

namespace vtfs {

std::optional<Config> Config::parse(int argc, char** argv, std::string& error) {
    if (argc != 5) {
        error = "Usage: virtualtorrentfs.exe <torrent-file> <drive-letter> <cache-size-gb> <cache-path>";
        return std::nullopt;
    }

    Config config;
    config.torrentFile = argv[1];
    config.cacheDirectory = argv[4];
    config.sessionDirectory = config.cacheDirectory / ".vtfs-state";

    if (!std::filesystem::exists(config.torrentFile)) {
        error = "Torrent file not found: " + config.torrentFile.string();
        return std::nullopt;
    }

    const std::string driveArg = argv[2];
    if (driveArg.empty() || !std::isalpha(static_cast<unsigned char>(driveArg[0]))) {
        error = "Drive letter must begin with A-Z.";
        return std::nullopt;
    }
    config.driveLetter = static_cast<char>(std::toupper(static_cast<unsigned char>(driveArg[0])));

    try {
        constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
        const auto cacheSizeGb = std::stoull(argv[3]);
        if (cacheSizeGb > std::numeric_limits<std::uint64_t>::max() / gib) {
            error = "Cache size is too large.";
            return std::nullopt;
        }
        config.cacheSizeBytes = cacheSizeGb * gib;
    } catch (...) {
        error = "Cache size must be a valid integer number of gigabytes.";
        return std::nullopt;
    }

    return config;
}

} // namespace vtfs
