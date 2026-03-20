#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vtfs {

struct Handshake {
    std::array<std::uint8_t, 8> reserved{};
    std::array<std::uint8_t, 20> infoHash{};
    std::array<std::uint8_t, 20> peerId{};
};

struct PeerMessage {
    enum class Type : std::uint8_t {
        choke = 0,
        unchoke = 1,
        interested = 2,
        notInterested = 3,
        have = 4,
        bitfield = 5,
        request = 6,
        piece = 7,
        cancel = 8,
        port = 9,
        keepAlive = 255
    };

    Type type = Type::keepAlive;
    std::vector<std::uint8_t> payload;
};

class PeerProtocol {
  public:
    static std::array<std::uint8_t, 20> makePeerId();
    static std::vector<std::uint8_t> encodeHandshake(const Handshake& handshake);
    static std::optional<Handshake> decodeHandshake(const std::vector<std::uint8_t>& data);
    static std::vector<std::uint8_t> encodeMessage(const PeerMessage& message);
    static std::optional<PeerMessage> decodeMessage(const std::vector<std::uint8_t>& data);
    static std::string userAgent();
};

} // namespace vtfs
