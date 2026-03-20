#pragma once

#include "PeerProtocol.hpp"
#include "TrackerClient.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

class PeerConnection {
  public:
    PeerConnection();
    ~PeerConnection();

    bool connectTo(const TrackerPeer& peer, int timeoutMs);
    bool performHandshake(const std::array<std::uint8_t, 20>& infoHash, const std::array<std::uint8_t, 20>& peerId);
    bool sendInterested();
    bool requestBlock(std::uint32_t pieceIndex, std::uint32_t blockOffset, std::uint32_t blockLength);
    [[nodiscard]] bool isConnected() const;
    void close();

  private:
    int socket_ = -1;
};

} // namespace vtfs
