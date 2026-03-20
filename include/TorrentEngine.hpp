#pragma once

#include "TorrentMetadata.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace vtfs {

struct StreamRequest {
    std::int32_t fileIndex = -1;
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
};

class TorrentEngine {
  public:
    explicit TorrentEngine(const TorrentMetadata& metadata);

    void start();
    void stop();
    void requestFileData(std::int32_t fileIndex, std::uint64_t offset, std::uint32_t length);
    [[nodiscard]] std::string userAgent() const;
    [[nodiscard]] std::array<std::uint8_t, 20> peerId() const;
    [[nodiscard]] std::optional<StreamRequest> nextRequest() const;
    [[nodiscard]] bool verifyPiece(std::size_t pieceIndex, const std::vector<std::uint8_t>& pieceData) const;

  private:
    const TorrentMetadata& metadata_;
    std::array<std::uint8_t, 20> peerId_{};
    mutable std::mutex mutex_;
    std::queue<StreamRequest> requests_;
    bool running_ = false;
};

} // namespace vtfs
