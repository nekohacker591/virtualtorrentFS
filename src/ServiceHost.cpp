#include "virtualtorrentfs/ServiceHost.hpp"
#include "virtualtorrentfs/Logger.hpp"

namespace vtfs {

bool ServiceHost::install(const Config& config) {
    Logger::instance().log(LogLevel::Info, L"Installing Windows service for mount " + config.mountPoint());
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool ServiceHost::remove() {
    Logger::instance().log(LogLevel::Info, L"Removing Windows service.");
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

int ServiceHost::run(const Config& config) {
    Logger::instance().log(LogLevel::Info, L"Running Windows service for mount " + config.mountPoint());
#ifdef _WIN32
    return 0;
#else
    return 1;
#endif
}

} // namespace vtfs
