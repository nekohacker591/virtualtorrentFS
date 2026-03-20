#include "PeerConnection.hpp"

#include <array>
#include <cstring>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vtfs {
namespace {
int connectSocket(const std::string& host, std::uint16_t port) {
#ifdef _WIN32
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        initialized = true;
    }
#endif

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const auto portText = std::to_string(port);
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0 || result == nullptr) {
        return -1;
    }

    int sock = -1;
    for (addrinfo* it = result; it != nullptr; it = it->ai_next) {
        sock = static_cast<int>(::socket(it->ai_family, it->ai_socktype, it->ai_protocol));
        if (sock < 0) continue;
        if (::connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) break;
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        sock = -1;
    }
    freeaddrinfo(result);
    return sock;
}

void closeSocket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

bool sendAll(int sock, const std::vector<std::uint8_t>& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int rc = send(sock, reinterpret_cast<const char*>(data.data() + sent), static_cast<int>(data.size() - sent), 0);
#else
        const int rc = static_cast<int>(::send(sock, data.data() + sent, data.size() - sent, 0));
#endif
        if (rc <= 0) return false;
        sent += static_cast<std::size_t>(rc);
    }
    return true;
}

bool recvExact(int sock, std::uint8_t* data, std::size_t size) {
    std::size_t read = 0;
    while (read < size) {
#ifdef _WIN32
        const int rc = recv(sock, reinterpret_cast<char*>(data + read), static_cast<int>(size - read), 0);
#else
        const int rc = static_cast<int>(::recv(sock, data + read, size - read, 0));
#endif
        if (rc <= 0) return false;
        read += static_cast<std::size_t>(rc);
    }
    return true;
}
}

PeerConnection::PeerConnection() = default;
PeerConnection::~PeerConnection() { close(); }

bool PeerConnection::connectTo(const TrackerPeer& peer, int) {
    close();
    socket_ = connectSocket(peer.host, peer.port);
    return socket_ >= 0;
}

bool PeerConnection::performHandshake(const std::array<std::uint8_t, 20>& infoHash, const std::array<std::uint8_t, 20>& peerId) {
    if (socket_ < 0) return false;
    Handshake hs{};
    hs.infoHash = infoHash;
    hs.peerId = peerId;
    const auto outbound = PeerProtocol::encodeHandshake(hs);
    if (!sendAll(socket_, outbound)) return false;

    std::array<std::uint8_t, 68> inbound{};
    if (!recvExact(socket_, inbound.data(), inbound.size())) return false;
    const auto decoded = PeerProtocol::decodeHandshake(std::vector<std::uint8_t>(inbound.begin(), inbound.end()));
    return decoded.has_value() && decoded->infoHash == infoHash;
}

bool PeerConnection::sendInterested() {
    if (socket_ < 0) return false;
    return sendAll(socket_, PeerProtocol::encodeMessage(PeerMessage{PeerMessage::Type::interested, {}}));
}

bool PeerConnection::requestBlock(std::uint32_t pieceIndex, std::uint32_t blockOffset, std::uint32_t blockLength) {
    if (socket_ < 0) return false;
    std::vector<std::uint8_t> payload(12);
    payload[0] = static_cast<std::uint8_t>((pieceIndex >> 24) & 0xFF);
    payload[1] = static_cast<std::uint8_t>((pieceIndex >> 16) & 0xFF);
    payload[2] = static_cast<std::uint8_t>((pieceIndex >> 8) & 0xFF);
    payload[3] = static_cast<std::uint8_t>(pieceIndex & 0xFF);
    payload[4] = static_cast<std::uint8_t>((blockOffset >> 24) & 0xFF);
    payload[5] = static_cast<std::uint8_t>((blockOffset >> 16) & 0xFF);
    payload[6] = static_cast<std::uint8_t>((blockOffset >> 8) & 0xFF);
    payload[7] = static_cast<std::uint8_t>(blockOffset & 0xFF);
    payload[8] = static_cast<std::uint8_t>((blockLength >> 24) & 0xFF);
    payload[9] = static_cast<std::uint8_t>((blockLength >> 16) & 0xFF);
    payload[10] = static_cast<std::uint8_t>((blockLength >> 8) & 0xFF);
    payload[11] = static_cast<std::uint8_t>(blockLength & 0xFF);
    return sendAll(socket_, PeerProtocol::encodeMessage(PeerMessage{PeerMessage::Type::request, payload}));
}

bool PeerConnection::isConnected() const { return socket_ >= 0; }

void PeerConnection::close() {
    if (socket_ >= 0) {
        closeSocket(socket_);
        socket_ = -1;
    }
}

} // namespace vtfs
