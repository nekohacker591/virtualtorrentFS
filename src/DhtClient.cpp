#include "DhtClient.hpp"

#include "Bencode.hpp"
#include "PeerProtocol.hpp"

#include <cstring>

namespace vtfs {

namespace {
BencodeString byteString(const std::array<std::uint8_t, 20>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
}

DhtClient::DhtClient()
    : nodeId_(PeerProtocol::makePeerId()) {}

std::array<std::uint8_t, 20> DhtClient::nodeId() const {
    return nodeId_;
}

std::vector<std::uint8_t> DhtClient::buildPingQuery(std::string_view txId) const {
    BencodeDictionary args;
    args.emplace("id", byteString(nodeId_));

    BencodeDictionary root;
    root.emplace("t", std::string(txId));
    root.emplace("y", std::string("q"));
    root.emplace("q", std::string("ping"));
    root.emplace("a", args);

    const auto encoded = encodeBencode(root);
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}

std::vector<std::uint8_t> DhtClient::buildFindNodeQuery(std::string_view txId,
                                                        const std::array<std::uint8_t, 20>& targetId) const {
    BencodeDictionary args;
    args.emplace("id", byteString(nodeId_));
    args.emplace("target", byteString(targetId));

    BencodeDictionary root;
    root.emplace("t", std::string(txId));
    root.emplace("y", std::string("q"));
    root.emplace("q", std::string("find_node"));
    root.emplace("a", args);

    const auto encoded = encodeBencode(root);
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}


std::vector<std::uint8_t> DhtClient::buildGetPeersQuery(std::string_view txId, const std::array<std::uint8_t, 20>& infoHash) const {
    BencodeDictionary args;
    args.emplace("id", byteString(nodeId_));
    args.emplace("info_hash", byteString(infoHash));

    BencodeDictionary root;
    root.emplace("t", std::string(txId));
    root.emplace("y", std::string("q"));
    root.emplace("q", std::string("get_peers"));
    root.emplace("a", args);

    const auto encoded = encodeBencode(root);
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}

std::vector<std::uint8_t> DhtClient::buildAnnouncePeerQuery(std::string_view txId,
                                                            const std::array<std::uint8_t, 20>& infoHash,
                                                            std::uint16_t port,
                                                            std::string_view token) const {
    BencodeDictionary args;
    args.emplace("id", byteString(nodeId_));
    args.emplace("info_hash", byteString(infoHash));
    args.emplace("port", static_cast<BencodeInteger>(port));
    args.emplace("token", std::string(token));
    args.emplace("implied_port", static_cast<BencodeInteger>(0));

    BencodeDictionary root;
    root.emplace("t", std::string(txId));
    root.emplace("y", std::string("q"));
    root.emplace("q", std::string("announce_peer"));
    root.emplace("a", args);

    const auto encoded = encodeBencode(root);
    return std::vector<std::uint8_t>(encoded.begin(), encoded.end());
}

std::vector<DhtNode> DhtClient::defaultBootstrapNodes() const {
    return {
        {"router.bittorrent.com", 6881},
        {"dht.transmissionbt.com", 6881},
        {"router.utorrent.com", 6881},
    };
}

} // namespace vtfs
