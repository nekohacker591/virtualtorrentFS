#include "WebSeedClient.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

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
    std::string path = "/";
    bool valid = false;
};

UrlParts parseHttpUrl(const std::string& url) {
    UrlParts parts;
    constexpr std::string_view scheme = "http://";
    if (!url.starts_with(scheme)) {
        return parts;
    }

    const auto rest = url.substr(scheme.size());
    const auto slash = rest.find('/');
    const auto hostPort = rest.substr(0, slash);
    parts.path = slash == std::string::npos ? "/" : rest.substr(slash);

    const auto colon = hostPort.find(':');
    if (colon == std::string::npos) {
        parts.host = hostPort;
    } else {
        parts.host = hostPort.substr(0, colon);
        std::uint16_t parsed = 0;
        const auto portText = hostPort.substr(colon + 1);
        std::from_chars(portText.data(), portText.data() + portText.size(), parsed);
        parts.port = parsed;
    }
    parts.valid = !parts.host.empty();
    return parts;
}

std::string percentEncodePath(std::string_view text) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char ch : text) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '/' || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << ((ch >> 4) & 0xF) << (ch & 0xF);
        }
    }
    return out.str();
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

bool sendRequest(int sock, const std::string& request) {
    std::size_t sent = 0;
    while (sent < request.size()) {
#ifdef _WIN32
        const int rc = send(sock, request.data() + sent, static_cast<int>(request.size() - sent), 0);
#else
        const int rc = static_cast<int>(::send(sock, request.data() + sent, request.size() - sent, 0));
#endif
        if (rc <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(rc);
    }
    return true;
}

} // namespace

WebSeedClient::WebSeedClient(std::string baseUrl)
    : baseUrl_(std::move(baseUrl)) {}

bool WebSeedClient::enabled() const {
    return !baseUrl_.empty();
}

std::optional<std::string> WebSeedClient::buildFileUrl(const std::string& virtualPath) const {
    if (!enabled()) {
        return std::nullopt;
    }

    auto normalized = virtualPath;
    if (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(normalized.begin());
    }

    std::string url = baseUrl_;
    if (!url.empty() && url.back() != '/') {
        url.push_back('/');
    }
    url += percentEncodePath(normalized);
    return url;
}

bool WebSeedClient::downloadFile(const std::string& virtualPath, const std::filesystem::path& destination) const {
    const auto url = buildFileUrl(virtualPath);
    if (!url) {
        return false;
    }

    const auto parts = parseHttpUrl(*url);
    if (!parts.valid) {
        return false;
    }

    const int sock = openSocket(parts.host, parts.port);
    if (sock < 0) {
        return false;
    }

    const std::string request =
        "GET " + parts.path + " HTTP/1.1\r\n"
        "Host: " + parts.host + "\r\n"
        "User-Agent: torrentfs-webseed\r\n"
        "Connection: close\r\n\r\n";
    if (!sendRequest(sock, request)) {
        closeSocket(sock);
        return false;
    }

    std::filesystem::create_directories(destination.parent_path());
    const auto temp = destination.string() + ".part";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        closeSocket(sock);
        return false;
    }

    std::array<char, 8192> buffer{};
    std::string headerBuffer;
    bool headerParsed = false;
    bool ok = false;

    for (;;) {
#ifdef _WIN32
        const int received = recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
#else
        const int received = static_cast<int>(::recv(sock, buffer.data(), buffer.size(), 0));
#endif
        if (received <= 0) {
            ok = headerParsed;
            break;
        }

        if (!headerParsed) {
            headerBuffer.append(buffer.data(), static_cast<std::size_t>(received));
            const auto headerEnd = headerBuffer.find("\r\n\r\n");
            if (headerEnd == std::string::npos) {
                continue;
            }
            const auto statusLineEnd = headerBuffer.find("\r\n");
            const auto statusLine = headerBuffer.substr(0, statusLineEnd);
            if (statusLine.find("200") == std::string::npos) {
                break;
            }
            out.write(headerBuffer.data() + headerEnd + 4, static_cast<std::streamsize>(headerBuffer.size() - headerEnd - 4));
            headerParsed = true;
            headerBuffer.clear();
        } else {
            out.write(buffer.data(), received);
        }
    }

    closeSocket(sock);
    out.close();
    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(temp, ec);
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(temp, destination, ec);
    if (ec) {
        std::filesystem::remove(destination, ec);
        ec.clear();
        std::filesystem::rename(temp, destination, ec);
    }
    return !ec;
}

} // namespace vtfs
