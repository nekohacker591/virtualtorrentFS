#pragma once

#include "virtualtorrentfs/Config.hpp"

namespace vtfs {

class ServiceHost {
public:
    bool install(const Config& config);
    bool remove();
    int run(const Config& config);
};

} // namespace vtfs
