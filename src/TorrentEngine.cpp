#include "TorrentEngine.hpp"

#include "PeerProtocol.hpp"
#include "Sha1.hpp"

#include <algorithm>
#include <cstring>

namespace vtfs {


TorrentEngine::TorrentEngine(const TorrentMetadata& metadata)
    : metadata_(metadata),
      peerId_(PeerProtocol::makePeerId()) {}

void TorrentEngine::start() {
    std::scoped_lock lock(mutex_);
    running_ = true;
}

void TorrentEngine::stop() {
    std::scoped_lock lock(mutex_);
    running_ = false;
    std::queue<StreamRequest> empty;
    requests_.swap(empty);
}

void TorrentEngine::requestFileData(std::int32_t fileIndex, std::uint64_t offset, std::uint32_t length) {
    std::scoped_lock lock(mutex_);
    if (!running_) {
        return;
    }
    requests_.push(StreamRequest{fileIndex, offset, length});
}

std::string TorrentEngine::userAgent() const {
    return PeerProtocol::userAgent();
}

std::array<std::uint8_t, 20> TorrentEngine::peerId() const {
    return peerId_;
}

std::optional<StreamRequest> TorrentEngine::nextRequest() const {
    std::scoped_lock lock(mutex_);
    if (requests_.empty()) {
        return std::nullopt;
    }
    return requests_.front();
}

bool TorrentEngine::verifyPiece(std::size_t pieceIndex, const std::vector<std::uint8_t>& pieceData) const {
    if (pieceIndex >= metadata_.pieceHashes().size()) {
        return false;
    }
    Sha1 sha1;
    sha1.update(pieceData.data(), pieceData.size());
    const auto digest = sha1.final();
    return std::equal(digest.begin(), digest.end(), metadata_.pieceHashes()[pieceIndex].begin());
}

} // namespace vtfs
