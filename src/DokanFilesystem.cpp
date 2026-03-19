#include "virtualtorrentfs/DokanFilesystem.hpp"
#include "virtualtorrentfs/Logger.hpp"

namespace vtfs {

DokanFilesystem::DokanFilesystem(LibtorrentSession& torrent, VirtualDriveManifest& manifest, CacheManager& cache)
    : torrent_(torrent), manifest_(manifest), cache_(cache) {}

bool DokanFilesystem::mount(wchar_t driveLetter) {
#ifdef _WIN32
    Logger::instance().log(LogLevel::Info, L"Mounting read-only Dokan filesystem on drive " + std::wstring(1, driveLetter) + L":");
    Logger::instance().log(LogLevel::Info, L"Integrate Dokan callback table here to expose manifest-backed files.");
    return true;
#else
    (void)driveLetter;
    Logger::instance().log(LogLevel::Warning, L"Dokan mount is only available on Windows.");
    return false;
#endif
}

} // namespace vtfs
