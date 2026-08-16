#pragma once

// NAMFX WebUI host — minimal HTTP/1.1 server (std only, no third-party).
// Control-plane scope: static files, JSON commands, SSE events. One thread
// per connection; connections are short-lived except SSE streams.
// Audio-path red lines do not apply here (shell code, not core).

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using namfx_socket_t = SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using namfx_socket_t = int;
#endif

namespace namfx {
namespace web {

struct HttpRequest {
    std::string method;
    std::string path;   // without query string
    std::string query;
    std::string body;
    std::map<std::string, std::string> headers;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain";
    std::string body;
    std::vector<std::pair<std::string, std::string>> extraHeaders;
};

// stream: non-null only for SSE endpoints — writes a chunk to the open
// connection; returns false when the client disconnected.
using StreamWriter = std::function<bool(const std::string&)>;
using HttpHandler = std::function<void(const HttpRequest&, HttpResponse&, const StreamWriter&)>;

class HttpServer {
public:
    explicit HttpServer(HttpHandler handler) : handler_(std::move(handler)) {}
    ~HttpServer() { stop(); }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // returns false when the listen socket cannot be opened
    bool start(const std::string& bindAddr, int port)
    {
#ifdef _WIN32
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            return false;
        }
#endif
        namfx_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == invalidSocket()) {
            return false;
        }
#ifdef _WIN32
        const BOOL exclusive = TRUE;
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                   reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<unsigned short>(port));
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bindAddr == "127.0.0.1") {
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        }
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0
            || listen(fd, 16) != 0) {
            closeSocket(fd);
            return false;
        }
        running_ = true;
        listener_ = std::thread([this, fd] { acceptLoop(fd); });
        return true;
    }

    void stop()
    {
        running_ = false;
        if (listener_.joinable()) {
            listener_.join();
        }
    }

private:
    static namfx_socket_t invalidSocket()
    {
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }

    static void closeSocket(namfx_socket_t fd)
    {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
    }

    void acceptLoop(namfx_socket_t listenFd)
    {
        while (running_) {
            sockaddr_in peer{};
#ifdef _WIN32
            int len = static_cast<int>(sizeof(peer));
#else
            socklen_t len = sizeof(peer);
#endif
            namfx_socket_t fd = accept(listenFd, reinterpret_cast<sockaddr*>(&peer), &len);
            if (fd == invalidSocket()) {
                if (!running_) {
                    break;
                }
                continue;
            }
            std::thread([this, fd] { handleConnection(fd); }).detach();
        }
        closeSocket(listenFd);
    }

    void handleConnection(namfx_socket_t fd)
    {
        HttpRequest req;
        if (!readRequest(fd, req)) {
            closeSocket(fd);
            return;
        }
        HttpResponse resp;
        StreamWriter stream = nullptr;
        const bool isSse = req.path == "/api/events";
        if (isSse) {
            // SSE: the handler writes events until the client disconnects
            resp.status = 200;
            resp.contentType = "text/event-stream";
            resp.extraHeaders.emplace_back("Cache-Control", "no-cache");
            resp.extraHeaders.emplace_back("Connection", "keep-alive");
            std::atomic<bool> closed{false};
            stream = [fd, &closed](const std::string& chunk) {
                if (closed.load(std::memory_order_relaxed)) {
                    return false;
                }
                return writeAll(fd, chunk);
            };
            if (!writeHead(fd, resp, false)) {
                closeSocket(fd);
                return;
            }
            handler_(req, resp, stream);
            closed.store(true, std::memory_order_relaxed);
            closeSocket(fd);
            return;
        }
        handler_(req, resp, nullptr);
        const std::string head = buildHead(resp, true);
        writeAll(fd, head);
        if (!resp.body.empty()) {
            writeAll(fd, resp.body);
        }
        closeSocket(fd);
    }

    static bool readRequest(namfx_socket_t fd, HttpRequest& req)
    {
        std::string head;
        char buf[4096];
        const std::string sep = "\r\n\r\n";
        while (head.size() < 65536) {
            const int n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                return false;
            }
            head.append(buf, static_cast<std::size_t>(n));
            if (head.find(sep) != std::string::npos) {
                break;
            }
        }
        const std::size_t sepPos = head.find(sep);
        if (sepPos == std::string::npos) {
            return false;
        }
        const std::string headerBlock = head.substr(0, sepPos);
        std::size_t lineEnd = 0;
        std::size_t lineNo = 0;
        while (lineEnd < headerBlock.size()) {
            std::size_t next = headerBlock.find("\r\n", lineEnd);
            if (next == std::string::npos) {
                next = headerBlock.size();
            }
            const std::string line = headerBlock.substr(lineEnd, next - lineEnd);
            if (lineNo == 0) {
                // request line: METHOD PATH HTTP/x.y
                std::size_t sp1 = line.find(' ');
                if (sp1 == std::string::npos) {
                    return false;
                }
                req.method = line.substr(0, sp1);
                std::size_t sp2 = line.find(' ', sp1 + 1);
                const std::string target =
                    line.substr(sp1 + 1, sp2 == std::string::npos ? std::string::npos : sp2 - sp1 - 1);
                const std::size_t q = target.find('?');
                req.path = q == std::string::npos ? target : target.substr(0, q);
                req.query = q == std::string::npos ? "" : target.substr(q + 1);
            } else {
                const std::size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    for (char& ch : key) {
                        if (ch >= 'A' && ch <= 'Z') {
                            ch = static_cast<char>(ch - 'A' + 'a');
                        }
                    }
                    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                        value.erase(value.begin());
                    }
                    req.headers[key] = value;
                }
            }
            lineEnd = next + 2;
            ++lineNo;
        }
        const auto it = req.headers.find("content-length");
        if (it != req.headers.end()) {
            const std::size_t len = static_cast<std::size_t>(std::strtoull(it->second.c_str(), nullptr, 10));
            std::string body = head.substr(sepPos + 4);
            while (body.size() < len) {
                const int n = recv(fd, buf, sizeof(buf), 0);
                if (n <= 0) {
                    return false;
                }
                body.append(buf, static_cast<std::size_t>(n));
            }
            req.body = body.substr(0, len);
        }
        return true;
    }

    static bool writeAll(namfx_socket_t fd, const std::string& data)
    {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const int n = send(fd, data.data() + sent,
                               static_cast<int>(data.size() - sent), 0);
            if (n <= 0) {
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    static bool writeHead(namfx_socket_t fd, const HttpResponse& resp, bool includeLength)
    {
        return writeAll(fd, buildHead(resp, includeLength));
    }

    static std::string buildHead(const HttpResponse& resp, bool includeLength)
    {
        std::string head;
        head.reserve(256);
        head += "HTTP/1.1 ";
        head += std::to_string(resp.status);
        head += " ";
        head += statusText(resp.status);
        head += "\r\nContent-Type: ";
        head += resp.contentType;
        if (includeLength) {
            head += "\r\nContent-Length: ";
            head += std::to_string(resp.body.size());
        }
        head += "\r\nAccess-Control-Allow-Origin: *\r\n";
        for (const auto& kv : resp.extraHeaders) {
            head += kv.first;
            head += ": ";
            head += kv.second;
            head += "\r\n";
        }
        head += "\r\n";
        return head;
    }

    static const char* statusText(int status)
    {
        switch (status) {
            case 200: return "OK";
            case 400: return "Bad Request";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 413: return "Payload Too Large";
            case 500: return "Internal Server Error";
            default: return "Unknown";
        }
    }

    HttpHandler handler_;
    std::atomic<bool> running_{false};
    std::thread listener_;
};

} // namespace web
} // namespace namfx
