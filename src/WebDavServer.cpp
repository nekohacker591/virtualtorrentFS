#include "WebDavServer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vtfs {

namespace {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
void closeSocket(SocketHandle socket) {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
    }
}
struct WsaScope {
    bool ok = false;
    WsaScope() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WsaScope() {
        if (ok) {
            WSACleanup();
        }
    }
};
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
void closeSocket(SocketHandle socket) {
    if (socket >= 0) {
        close(socket);
    }
}
#endif

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::string xmlEscape(const std::string& input) {
    std::string out;
    for (char ch : input) {
        switch (ch) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

int fromHex(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

std::string urlDecode(std::string_view path) {
    std::string out;
    out.reserve(path.size());
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size()) {
            const int hi = fromHex(path[i + 1]);
            const int lo = fromHex(path[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(path[i] == '+' ? ' ' : path[i]);
    }
    return out.empty() ? "/" : out;
}

std::string webDavHref(const FileEntry& entry) {
    if (entry.virtualPath == "/") {
        return "/";
    }
    return entry.virtualPath;
}

void appendPropResponse(std::string& xml, const FileEntry& entry) {
    xml += "<D:response><D:href>";
    xml += xmlEscape(webDavHref(entry));
    xml += "</D:href><D:propstat><D:prop><D:resourcetype>";
    if (entry.isDirectory) {
        xml += "<D:collection/>";
    }
    xml += "</D:resourcetype><D:getcontentlength>";
    xml += std::to_string(entry.size);
    xml += "</D:getcontentlength></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>";
}

bool sendAll(SocketHandle socket, const void* data, std::size_t length) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < length) {
#ifdef _WIN32
        const int written = send(socket, bytes + sent, static_cast<int>(length - sent), 0);
#else
        const int written = static_cast<int>(send(socket, bytes + sent, length - sent, 0));
#endif
        if (written <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(written);
    }
    return true;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> parseRangeHeader(const std::string& value, std::uint64_t size) {
    const auto prefix = std::string("bytes=");
    if (value.rfind(prefix, 0) != 0) {
        return std::nullopt;
    }
    const auto dash = value.find('-', prefix.size());
    if (dash == std::string::npos) {
        return std::nullopt;
    }
    try {
        const auto start = static_cast<std::uint64_t>(std::stoull(value.substr(prefix.size(), dash - prefix.size())));
        const auto endText = trim(value.substr(dash + 1));
        const auto end = endText.empty() ? (size == 0 ? 0 : size - 1) : static_cast<std::uint64_t>(std::stoull(endText));
        if (start >= size || end < start) {
            return std::nullopt;
        }
        return std::make_pair(start, std::min<std::uint64_t>(end, size - 1));
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

WebDavServer::~WebDavServer() {
    stop();
}

bool WebDavServer::start(std::shared_ptr<TorrentSession> session, std::uint16_t preferredPort) {
    stop();
    session_ = std::move(session);
    if (!session_) {
        return false;
    }

#ifdef _WIN32
    static WsaScope wsa;
    if (!wsa.ok) {
        return false;
    }
#endif

    for (std::uint16_t candidate = preferredPort; candidate < static_cast<std::uint16_t>(preferredPort + 20); ++candidate) {
        SocketHandle server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server == kInvalidSocket) {
            continue;
        }

        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(candidate);
        if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 && listen(server, 16) == 0) {
            listenSocket_ = static_cast<int>(server);
            port_ = candidate;
            running_.store(true);
            thread_ = std::thread(&WebDavServer::serverLoop, this);
            return true;
        }
        closeSocket(server);
    }

    session_.reset();
    return false;
}

void WebDavServer::stop() {
    running_.store(false);
    closeSocket(static_cast<SocketHandle>(listenSocket_));
    listenSocket_ = -1;
    if (thread_.joinable()) {
        thread_.join();
    }
    session_.reset();
    port_ = 0;
}

bool WebDavServer::running() const {
    return running_.load();
}

std::uint16_t WebDavServer::port() const {
    return port_;
}

std::string WebDavServer::endpoint() const {
    if (port_ == 0) {
        return {};
    }
    return "http://127.0.0.1:" + std::to_string(port_) + "/";
}

void WebDavServer::serverLoop() {
    while (running_.load()) {
        sockaddr_in clientAddr{};
#ifdef _WIN32
        int clientLen = sizeof(clientAddr);
#else
        socklen_t clientLen = sizeof(clientAddr);
#endif
        SocketHandle client = accept(static_cast<SocketHandle>(listenSocket_), reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);
        if (client == kInvalidSocket) {
            if (running_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
            }
            continue;
        }

        std::array<char, 8192> buffer{};
#ifdef _WIN32
        const int received = recv(client, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
#else
        const int received = static_cast<int>(recv(client, buffer.data(), buffer.size() - 1, 0));
#endif
        if (received <= 0) {
            closeSocket(client);
            continue;
        }
        buffer[static_cast<std::size_t>(received)] = '\0';
        std::string request(buffer.data(), static_cast<std::size_t>(received));

        const auto lineEnd = request.find("\r\n");
        if (lineEnd == std::string::npos) {
            closeSocket(client);
            continue;
        }

        std::istringstream requestLine(request.substr(0, lineEnd));
        std::string method;
        std::string target;
        std::string version;
        requestLine >> method >> target >> version;

        std::unordered_map<std::string, std::string> headers;
        std::size_t offset = lineEnd + 2;
        while (offset < request.size()) {
            const auto next = request.find("\r\n", offset);
            if (next == std::string::npos || next == offset) {
                break;
            }
            const auto colon = request.find(':', offset);
            if (colon != std::string::npos && colon < next) {
                auto name = request.substr(offset, colon - offset);
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                headers[name] = trim(request.substr(colon + 1, next - colon - 1));
            }
            offset = next + 2;
        }

        const auto path = urlDecode(target);
        const auto entry = session_->lookup(path);

        auto sendSimple = [&](std::string_view status, std::string_view body, std::string_view type = "text/plain") {
            std::string response = "HTTP/1.1 ";
            response += status;
            response += "\r\nContent-Length: ";
            response += std::to_string(body.size());
            response += "\r\nContent-Type: ";
            response += type;
            response += "\r\nConnection: close\r\n\r\n";
            response += body;
            sendAll(client, response.data(), response.size());
        };

        if (method == "OPTIONS") {
            const std::string response =
                "HTTP/1.1 200 OK\r\nAllow: OPTIONS, PROPFIND, GET, HEAD\r\nDAV: 1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            sendAll(client, response.data(), response.size());
            closeSocket(client);
            continue;
        }

        if (!entry) {
            sendSimple("404 Not Found", "not found");
            closeSocket(client);
            continue;
        }

        if (method == "PROPFIND") {
            std::string xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?><D:multistatus xmlns:D=\"DAV:\">";
            appendPropResponse(xml, *entry);
            const auto depth = headers.contains("depth") ? headers["depth"] : "0";
            if (entry->isDirectory && depth != "0") {
                for (const auto& child : session_->listDirectory(entry->virtualPath)) {
                    appendPropResponse(xml, child);
                }
            }
            xml += "</D:multistatus>";

            std::string response = "HTTP/1.1 207 Multi-Status\r\nContent-Type: application/xml; charset=\"utf-8\"\r\nContent-Length: ";
            response += std::to_string(xml.size());
            response += "\r\nConnection: close\r\n\r\n";
            response += xml;
            sendAll(client, response.data(), response.size());
            closeSocket(client);
            continue;
        }

        if (entry->isDirectory) {
            sendSimple("405 Method Not Allowed", "directory reads are not supported");
            closeSocket(client);
            continue;
        }

        std::uint64_t start = 0;
        std::uint64_t end = entry->size == 0 ? 0 : entry->size - 1;
        bool partial = false;
        if (headers.contains("range")) {
            if (const auto range = parseRangeHeader(headers["range"], entry->size)) {
                start = range->first;
                end = range->second;
                partial = true;
            }
        }
        const auto responseLength = entry->size == 0 ? 0 : (end - start + 1);

        session_->beginStreaming(*entry, start, static_cast<std::uint32_t>(std::min<std::uint64_t>(responseLength, 2 * 1024 * 1024ULL)));

        std::string headersOut = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        headersOut += "Accept-Ranges: bytes\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(responseLength) + "\r\n";
        if (partial) {
            headersOut += "Content-Range: bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(entry->size) + "\r\n";
        }
        headersOut += "Connection: close\r\n\r\n";
        if (!sendAll(client, headersOut.data(), headersOut.size())) {
            closeSocket(client);
            continue;
        }

        if (method != "HEAD") {
            std::vector<char> chunk(64 * 1024);
            std::uint64_t position = start;
            while (position <= end) {
                const auto wanted = static_cast<std::uint32_t>(std::min<std::uint64_t>(chunk.size(), end - position + 1));
                std::uint32_t bytesRead = 0;
                bool ready = false;
                for (int attempt = 0; attempt < 100; ++attempt) {
                    ready = session_->tryReadFileRange(*entry, position, chunk.data(), wanted, bytesRead);
                    if (ready && bytesRead > 0) {
                        break;
                    }
                    session_->beginStreaming(*entry, position, wanted);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                if (!ready || bytesRead == 0) {
                    break;
                }
                if (!sendAll(client, chunk.data(), bytesRead)) {
                    break;
                }
                position += bytesRead;
            }
        }

        closeSocket(client);
    }
}

} // namespace vtfs
