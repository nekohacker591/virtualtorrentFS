#include "App.hpp"

#include "TorrentSession.hpp"
#include "VirtualDrive.hpp"

#include <iostream>
#include <memory>

namespace vtfs {

int App::run(const Config& config) {
    try {
        auto session = std::make_shared<TorrentSession>(config);
        session->start();

        VirtualDrive drive(config, session);
        return drive.mount();
    } catch (const std::exception& ex) {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }
}

} // namespace vtfs
