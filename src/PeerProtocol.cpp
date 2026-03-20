#include "PeerProtocol.hpp"

#include <chrono>
#include <cstring>
#include <random>

namespace vtfs {

std::array<std::uint8_t, 20> PeerProtocol::makePeerId() {
    std::array<std::uint8_t, 20> id{};
    const std::string prefix = "-TFS001-";
    std::memcpy(id.data(), prefix.data(), prefix.size());

    std::mt19937_64 rng(static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (std::size_t i = prefix.size(); i < id.size(); ++i) {
        id[i] = static_cast<std::uint8_t>('0' + (rng() % 10));
    }
    return id;
}

std::vector<std::uint8_t> PeerProtocol::encodeHandshake(const Handshake& handshake) {
    std::vector<std::uint8_t> out;
    constexpr char protocol[] = "BitTorrent protocol";
    out.push_back(static_cast<std::uint8_t>(sizeof(protocol) - 1));
    out.insert(out.end(), protocol, protocol + sizeof(protocol) - 1);
    out.insert(out.end(), handshake.reserved.begin(), handshake.reserved.end());
    out.insert(out.end(), handshake.infoHash.begin(), handshake.infoHash.end());
    out.insert(out.end(), handshake.peerId.begin(), handshake.peerId.end());
    return out;
}

std::optional<Handshake> PeerProtocol::decodeHandshake(const std::vector<std::uint8_t>& data) {
    if (data.size() < 68 || data[0] != 19) {
        return std::nullopt;
    }
    Handshake hs;
    std::memcpy(hs.reserved.data(), data.data() + 20, 8);
    std::memcpy(hs.infoHash.data(), data.data() + 28, 20);
    std::memcpy(hs.peerId.data(), data.data() + 48, 20);
    return hs;
}

std::vector<std::uint8_t> PeerProtocol::encodeMessage(const PeerMessage& message) {
    if (message.type == PeerMessage::Type::keepAlive) {
        return {0, 0, 0, 0};
    }

    const std::uint32_t length = static_cast<std::uint32_t>(1 + message.payload.size());
    std::vector<std::uint8_t> out(4 + length);
    out[0] = static_cast<std::uint8_t>((length >> 24) & 0xFF);
    out[1] = static_cast<std::uint8_t>((length >> 16) & 0xFF);
    out[2] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
    out[3] = static_cast<std::uint8_t>(length & 0xFF);
    out[4] = static_cast<std::uint8_t>(message.type);
    std::memcpy(out.data() + 5, message.payload.data(), message.payload.size());
    return out;
}

std::optional<PeerMessage> PeerProtocol::decodeMessage(const std::vector<std::uint8_t>& data) {
    if (data.size() < 4) {
        return std::nullopt;
    }

    const std::uint32_t length = (static_cast<std::uint32_t>(data[0]) << 24) |
                                 (static_cast<std::uint32_t>(data[1]) << 16) |
                                 (static_cast<std::uint32_t>(data[2]) << 8) |
                                 static_cast<std::uint32_t>(data[3]);
    if (length == 0) {
        return PeerMessage{PeerMessage::Type::keepAlive, {}};
    }
    if (data.size() < length + 4) {
        return std::nullopt;
    }

    PeerMessage msg;
    msg.type = static_cast<PeerMessage::Type>(data[4]);
    msg.payload.assign(data.begin() + 5, data.begin() + 4 + length);
    return msg;
}

std::string PeerProtocol::userAgent() {
    return "torrentfs";
}

} // namespace vtfs
