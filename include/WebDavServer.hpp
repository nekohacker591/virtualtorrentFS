#pragma once

#include "TorrentSession.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace vtfs {

class WebDavServer {
  public:
    WebDavServer() = default;
    ~WebDavServer();

    WebDavServer(const WebDavServer&) = delete;
    WebDavServer& operator=(const WebDavServer&) = delete;

    bool start(std::shared_ptr<TorrentSession> session, std::uint16_t preferredPort = 9867);
    void stop();

    [[nodiscard]] bool running() const;
    [[nodiscard]] std::uint16_t port() const;
    [[nodiscard]] std::string endpoint() const;

  private:
    void serverLoop();

    std::shared_ptr<TorrentSession> session_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::uint16_t port_ = 0;
    int listenSocket_ = -1;
};

} // namespace vtfs
