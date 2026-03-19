#pragma once

#include <filesystem>
#include <string>

namespace vtfs {

struct Config {
    std::filesystem::path torrentFile;
    wchar_t driveLetter = L'Z';
    std::uint64_t cacheSizeBytes = 0;
    std::filesystem::path cacheDirectory;
    bool installService = false;
    bool removeService = false;
    bool runService = false;
    bool verboseLogging = true;

    [[nodiscard]] std::wstring mountPoint() const;
};

} // namespace vtfs
