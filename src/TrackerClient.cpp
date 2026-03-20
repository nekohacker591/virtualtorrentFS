#include "TrackerClient.hpp"

#include "Bencode.hpp"

#include <array>
#include <charconv>
#include <cstring>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

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
struct UrlParts {
    std::string host;
    std::uint16_t port = 80;
    std::string path;
    bool valid = false;
};

std::string percentEncode(const std::uint8_t* data, std::size_t size) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < size; ++i) {
        out << '%' << std::setw(2) << static_cast<int>(data[i]);
    }
    return out.str();
}

UrlParts parseHttpUrl(const std::string& url) {
    UrlParts parts;
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return parts;
    }
    const std::string rest = url.substr(scheme.size());
    const auto slash = rest.find('/');
    const auto hostPort = rest.substr(0, slash);
    parts.path = slash == std::string::npos ? "/" : rest.substr(slash);
    const auto colon = hostPort.find(':');
    if (colon == std::string::npos) {
        parts.host = hostPort;
    } else {
        parts.host = hostPort.substr(0, colon);
        const auto portToken = hostPort.substr(colon + 1);
        std::uint16_t port = 0;
        std::from_chars(portToken.data(), portToken.data() + portToken.size(), port);
        parts.port = port;
    }
    parts.valid = !parts.host.empty();
    return parts;
}

int openSocket(const std::string& host, std::uint16_t port) {
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
        if (sock < 0) {
            continue;
        }
        if (::connect(sock, it->ai_addr, static_cast<int>(it->ai_addrlen)) == 0) {
            break;
        }
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

std::optional<TrackerResponse> parseTrackerResponse(const std::string& body) {
    const auto root = BencodeParser(body).parse();
    if (!root.isDictionary()) {
        return std::nullopt;
    }
    const auto& dict = root.asDictionary();
    TrackerResponse response;
    if (const auto it = dict.find("interval"); it != dict.end() && it->second.isInteger()) {
        response.intervalSeconds = static_cast<std::uint32_t>(it->second.asInteger());
    }
    if (const auto it = dict.find("peers"); it != dict.end() && it->second.isString()) {
        const auto& peersBlob = it->second.asString();
        for (std::size_t i = 0; i + 5 < peersBlob.size(); i += 6) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(peersBlob.data() + i);
            TrackerPeer peer;
            peer.host = std::to_string(bytes[0]) + "." + std::to_string(bytes[1]) + "." +
                        std::to_string(bytes[2]) + "." + std::to_string(bytes[3]);
            peer.port = static_cast<std::uint16_t>((bytes[4] << 8) | bytes[5]);
            response.peers.push_back(std::move(peer));
        }
    }
    return response;
}
}

std::optional<TrackerResponse> TrackerClient::announce(const std::string& announceUrl,
                                                       const std::array<std::uint8_t, 20>& infoHash,
                                                       const std::array<std::uint8_t, 20>& peerId,
                                                       std::uint64_t downloaded,
                                                       std::uint64_t left,
                                                       std::uint64_t uploaded) const {
    const auto url = parseHttpUrl(announceUrl);
    if (!url.valid) {
        return std::nullopt;
    }

    const int sock = openSocket(url.host, url.port);
    if (sock < 0) {
        return std::nullopt;
    }

    const std::string path = url.path +
        (url.path.find('?') == std::string::npos ? "?" : "&") +
        "info_hash=" + percentEncode(infoHash.data(), infoHash.size()) +
        "&peer_id=" + percentEncode(peerId.data(), peerId.size()) +
        "&port=6881&uploaded=" + std::to_string(uploaded) +
        "&downloaded=" + std::to_string(downloaded) +
        "&left=" + std::to_string(left) +
        "&compact=1&event=started";

    const std::string request =
        "GET " + path + " HTTP/1.1\r\n" +
        "Host: " + url.host + "\r\n" +
        "User-Agent: torrentfs\r\n" +
        "Connection: close\r\n\r\n";

#ifdef _WIN32
    send(sock, request.data(), static_cast<int>(request.size()), 0);
#else
    ::send(sock, request.data(), request.size(), 0);
#endif

    std::string response;
    std::array<char, 4096> buffer{};
    for (;;) {
#ifdef _WIN32
        const int received = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int received = static_cast<int>(::recv(sock, buffer.data(), buffer.size(), 0));
#endif
        if (received <= 0) {
            break;
        }
        response.append(buffer.data(), static_cast<std::size_t>(received));
    }
    closeSocket(sock);

    const auto headerEnd = response.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return std::nullopt;
    }
    return parseTrackerResponse(response.substr(headerEnd + 4));
}

} // namespace vtfs
