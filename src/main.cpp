#include "virtualtorrentfs/ArgumentParser.hpp"
#include "virtualtorrentfs/CacheManager.hpp"
#include "virtualtorrentfs/DokanFilesystem.hpp"
#include "virtualtorrentfs/LibtorrentSession.hpp"
#include "virtualtorrentfs/Logger.hpp"
#include "virtualtorrentfs/ServiceHost.hpp"
#include "virtualtorrentfs/VirtualDriveManifest.hpp"

#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::vector<std::wstring> collectArgs(int argc, wchar_t** argv) {
    std::vector<std::wstring> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    return args;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
    std::vector<std::wstring> wideArgs;
    wideArgs.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        const std::string narrow = argv[i];
        wideArgs.emplace_back(narrow.begin(), narrow.end());
    }
    wchar_t** ignored = nullptr;
    (void)ignored;
#endif

#ifdef _WIN32
    const auto args = collectArgs(argc, argv);
#else
    const auto& args = wideArgs;
#endif

    const auto config = vtfs::ArgumentParser::parse(args);
    if (!config.has_value()) {
        std::wcerr << vtfs::ArgumentParser::usage() << std::endl;
        return 1;
    }

    vtfs::ServiceHost service;
    if (config->removeService) {
        return service.remove() ? 0 : 1;
    }
    if (config->runService) {
        return service.run(*config);
    }
    if (config->installService) {
        return service.install(*config) ? 0 : 1;
    }

    vtfs::LibtorrentSession torrent(*config);
    if (!torrent.initialize() || !torrent.prefetchMetadata()) {
        vtfs::Logger::instance().log(vtfs::LogLevel::Error, L"Failed to initialize torrent metadata.");
        return 1;
    }

    auto index = torrent.buildIndex();
    vtfs::VirtualDriveManifest manifest;
    manifest.build(index);

    vtfs::CacheManager cache(config->cacheSizeBytes);
    vtfs::DokanFilesystem filesystem(torrent, manifest, cache);
    if (!filesystem.mount(config->driveLetter)) {
        vtfs::Logger::instance().log(vtfs::LogLevel::Error, L"Failed to mount virtual filesystem.");
        return 1;
    }

    vtfs::Logger::instance().log(vtfs::LogLevel::Info, L"VirtualTorrentFS is running.");
    return 0;
}
