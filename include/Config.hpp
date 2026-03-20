#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace vtfs {

struct Config {
    std::filesystem::path torrentFile;
    char driveLetter = 'Z';
    std::uint64_t cacheSizeBytes = 0;
    std::filesystem::path cacheDirectory;
    std::filesystem::path sessionDirectory;
    std::uint32_t readAheadPieces = 16;

    static std::optional<Config> parse(int argc, char** argv, std::string& error);
};

} // namespace vtfs
