#include "App.hpp"
#include "Config.hpp"

#include <iostream>

int main(int argc, char** argv) {
    std::string error;
    const auto config = vtfs::Config::parse(argc, argv, error);
    if (!config) {
        std::cerr << error << '\n';
        return 1;
    }

    vtfs::App app;
    return app.run(*config);
}
