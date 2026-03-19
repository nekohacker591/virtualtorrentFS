#include "virtualtorrentfs/Config.hpp"

namespace vtfs {

std::wstring Config::mountPoint() const {
    return std::wstring(1, driveLetter) + L":";
}

} // namespace vtfs
