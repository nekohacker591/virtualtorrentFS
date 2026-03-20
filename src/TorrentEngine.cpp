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
    (void)portMapper_.buildNatPmpMapRequest(6881, 6881, 3600);
    (void)portMapper_.buildPcpMapRequest(6881, 6881, 3600);
    (void)portMapper_.buildSsdpSearchRequest();
    (void)dht_.buildPingQuery("aa");
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


void TorrentEngine::pumpOnce() {
    std::scoped_lock lock(mutex_);
    if (!running_) {
        return;
    }

    if (peers_.empty()) {
        for (const auto& node : dht_.defaultBootstrapNodes()) {
            peers_.push_back(TrackerPeer{node.host, node.port});
        }
    }

    if (peers_.empty() && !metadata_.announceUrl().empty()) {
        if (const auto response = tracker_.announce(metadata_.announceUrl(), metadata_.pieceHashes().empty() ? std::array<std::uint8_t,20>{} : metadata_.pieceHashes()[0], peerId_, 0, metadata_.totalSize(), 0)) {
            peers_ = response->peers;
        }
    }

    if (!peerConnection_.has_value() && !peers_.empty()) {
        peerConnection_.emplace();
        if (!peerConnection_->connectTo(peers_.front(), 5000)) {
            peerConnection_.reset();
            return;
        }
    }

    if (peerConnection_ && !requests_.empty()) {
        const auto request = requests_.front();
        const auto pieceLength = static_cast<std::uint32_t>(metadata_.pieceLength());
        const auto globalOffset = metadata_.files()[static_cast<std::size_t>(request.fileIndex)].offset + request.offset;
        const auto pieceIndex = static_cast<std::uint32_t>(globalOffset / pieceLength);
        const auto pieceOffset = static_cast<std::uint32_t>(globalOffset % pieceLength);
        if (peerConnection_->sendInterested()) {
            peerConnection_->requestBlock(pieceIndex, pieceOffset, std::min<std::uint32_t>(request.length, 16 * 1024));
        }
        requests_.pop();
    }
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
