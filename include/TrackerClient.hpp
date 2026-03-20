#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

struct TrackerPeer {
    std::string host;
    std::uint16_t port = 0;
};

struct TrackerResponse {
    std::uint32_t intervalSeconds = 1800;
    std::vector<TrackerPeer> peers;
};

class TrackerClient {
  public:
    [[nodiscard]] std::optional<TrackerResponse> announce(const std::string& announceUrl,
                                                          const std::array<std::uint8_t, 20>& infoHash,
                                                          const std::array<std::uint8_t, 20>& peerId,
                                                          std::uint64_t downloaded,
                                                          std::uint64_t left,
                                                          std::uint64_t uploaded) const;
};

} // namespace vtfs
