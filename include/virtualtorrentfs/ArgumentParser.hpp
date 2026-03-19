#pragma once

#include "virtualtorrentfs/Config.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace vtfs {

class ArgumentParser {
public:
    static std::optional<Config> parse(const std::vector<std::wstring>& args);
    static std::wstring usage();

private:
    static std::optional<std::uint64_t> parseCacheSizeGb(std::wstring_view text);
    static bool isDriveLetter(std::wstring_view text);
};

} // namespace vtfs
