#include "virtualtorrentfs/ArgumentParser.hpp"

#include <cwctype>

namespace vtfs {

std::optional<Config> ArgumentParser::parse(const std::vector<std::wstring>& args) {
    if (args.size() < 5) {
        return std::nullopt;
    }

    Config config;
    std::size_t index = 1;

    if (args[index] == L"--install-service") {
        config.installService = true;
        ++index;
    } else if (args[index] == L"--remove-service") {
        config.removeService = true;
        return config;
    } else if (args[index] == L"--run-service") {
        config.runService = true;
        return config;
    }

    if (args.size() <= index + 3) {
        return std::nullopt;
    }

    config.torrentFile = args[index++];
    if (!isDriveLetter(args[index])) {
        return std::nullopt;
    }

    config.driveLetter = static_cast<wchar_t>(std::towupper(args[index++][0]));
    const auto cacheSizeGb = parseCacheSizeGb(args[index++]);
    if (!cacheSizeGb.has_value()) {
        return std::nullopt;
    }

    config.cacheSizeBytes = *cacheSizeGb * 1024ull * 1024ull * 1024ull;
    config.cacheDirectory = args[index];
    return config;
}

std::wstring ArgumentParser::usage() {
    return L"Usage: virtualtorrentfs.exe [--install-service|--run-service|--remove-service] <torrent-file> <drive-letter> <cache-size-gb> <cache-directory>";
}

std::optional<std::uint64_t> ArgumentParser::parseCacheSizeGb(std::wstring_view text) {
    std::uint64_t value = 0;
    for (const wchar_t ch : text) {
        if (!std::iswdigit(ch)) {
            return std::nullopt;
        }
        value = (value * 10) + static_cast<std::uint64_t>(ch - L'0');
    }
    return value > 0 ? std::optional<std::uint64_t>(value) : std::nullopt;
}

bool ArgumentParser::isDriveLetter(std::wstring_view text) {
    return text.size() == 1 && std::iswalpha(text[0]);
}

} // namespace vtfs
