/**
 * WebServer.h mock — Real POSIX socket HTTP server
 *
 * Implements the ESP32 WebServer API but uses POSIX sockets so that
 * real HTTP clients (curl, test harness) can talk to it on localhost.
 */
#pragma once
#include "../arduino_mock.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <sstream>
#include <algorithm>

#define HTTP_POST    1
#define HTTP_GET     2
#define HTTP_DELETE  3
#define HTTP_OPTIONS 4

class WebServer {
public:
    struct Route {
        String path;
        int method;
        std::function<void()> handler;
    };

    WebServer(uint16_t port) : _port(port) {}
    ~WebServer() { stop(); }

    void on(const char* path, int method, std::function<void()> handler) {
        _routes.push_back({String(path), method, handler});
    }

    void collectHeaders(const char** headers, int count) {
        for (int i = 0; i < count; i++) {
            _collectHeaderNames.push_back(String(headers[i]));
        }
    }

    void begin() {
        _serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (_serverFd < 0) { perror("socket"); return; }

        int opt = 1;
        setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(_serverFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(_port);

        if (bind(_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(_serverFd); _serverFd = -1;
            return;
        }

        if (listen(_serverFd, 8) < 0) {
            perror("listen");
            close(_serverFd); _serverFd = -1;
            return;
        }

        // Non-blocking for handleClient polling
        fcntl(_serverFd, F_SETFL, O_NONBLOCK);
        _running = true;
    }

    void stop() {
        if (_serverFd >= 0) { close(_serverFd); _serverFd = -1; }
        _running = false;
    }

    /**
     * handleClient() — poll for one incoming connection, parse HTTP, dispatch.
     * Non-blocking: returns immediately if no connection pending.
     */
    void handleClient() {
        if (_serverFd < 0) return;

        struct pollfd pfd = { _serverFd, POLLIN, 0 };
        int ret = poll(&pfd, 1, 5); // 5ms timeout
        if (ret <= 0) return;

        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(_serverFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientFd < 0) return;

        // Set a read timeout
        struct timeval tv = { 2, 0 };
        setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        _handleConnection(clientFd);
        close(clientFd);
    }

    // ── ESP32 WebServer API used by mcpd ────────────────────────────

    String arg(const char* name) {
        if (strcmp(name, "plain") == 0) return _requestBody;
        auto it = _queryParams.find(String(name));
        if (it != _queryParams.end()) return it->second;
        return "";
    }

    String header(const char* name) {
        // Case-insensitive lookup
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        auto it = _requestHeaders.find(String(lower.c_str()));
        if (it != _requestHeaders.end()) return it->second;
        return "";
    }

    void sendHeader(const char* name, const String& value) {
        _responseHeaders.push_back({String(name), value});
    }

    void send(int code) {
        _responseCode = code;
        _responseBody = "";
        _responseContentType = "";
        _responseSent = true;
    }

    void send(int code, const char* contentType, const String& body) {
        _responseCode = code;
        _responseContentType = String(contentType);
        _responseBody = body;
        _responseSent = true;
    }

    uint16_t getPort() const { return _port; }

    // ── Test helpers ────────────────────────────────────────────────

    /** Simulate a request for unit testing (no sockets) */
    void _setBody(const String& body) { _requestBody = body; }
    void _setHeader(const char* name, const String& value) {
        std::string lower(name);
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        _requestHeaders[String(lower.c_str())] = value;
    }
    void _simulateRequest(const char* path, int method) {
        for (auto& r : _routes) {
            if (r.path == String(path) && r.method == method) {
                _responseHeaders.clear();
                _responseSent = false;
                r.handler();
                return;
            }
        }
    }

private:
    uint16_t _port;
    int _serverFd = -1;
    bool _running = false;

    std::vector<Route> _routes;
    std::vector<String> _collectHeaderNames;

    // Per-request state
    String _requestBody;
    std::map<String, String> _requestHeaders;
    std::map<String, String> _queryParams;

    int _responseCode = 200;
    String _responseContentType;
    String _responseBody;
    std::vector<std::pair<String, String>> _responseHeaders;
    bool _responseSent = false;

    void _handleConnection(int fd) {
        // Read full request
        std::string raw;
        char buf[4096];
        while (true) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, n);
            // Check if we have the full request (headers + body per Content-Length)
            size_t headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                // Parse Content-Length
                size_t clPos = raw.find("Content-Length:");
                if (clPos == std::string::npos) clPos = raw.find("content-length:");
                if (clPos != std::string::npos) {
                    size_t valStart = clPos + 15;
                    while (valStart < raw.size() && raw[valStart] == ' ') valStart++;
                    size_t valEnd = raw.find("\r\n", valStart);
                    int contentLength = std::stoi(raw.substr(valStart, valEnd - valStart));
                    size_t bodyStart = headerEnd + 4;
                    if ((int)(raw.size() - bodyStart) >= contentLength) break;
                } else {
                    break; // No Content-Length, assume complete
                }
            }
        }

        if (raw.empty()) return;

        // Parse request line
        size_t firstLine = raw.find("\r\n");
        if (firstLine == std::string::npos) return;
        std::string requestLine = raw.substr(0, firstLine);

        std::istringstream iss(requestLine);
        std::string methodStr, pathStr, httpVer;
        iss >> methodStr >> pathStr >> httpVer;

        int method = 0;
        if (methodStr == "POST") method = HTTP_POST;
        else if (methodStr == "GET") method = HTTP_GET;
        else if (methodStr == "DELETE") method = HTTP_DELETE;
        else if (methodStr == "OPTIONS") method = HTTP_OPTIONS;

        // Parse headers
        _requestHeaders.clear();
        _queryParams.clear();
        size_t headerStart = firstLine + 2;
        size_t headerEnd = raw.find("\r\n\r\n", headerStart);
        if (headerEnd == std::string::npos) headerEnd = raw.size();

        std::string headerSection = raw.substr(headerStart, headerEnd - headerStart);
        std::istringstream headerStream(headerSection);
        std::string line;
        while (std::getline(headerStream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string val = line.substr(colon + 1);
                while (!val.empty() && val[0] == ' ') val.erase(0, 1);
                // Store lowercase key
                std::transform(key.begin(), key.end(), key.begin(), ::tolower);
                _requestHeaders[String(key.c_str())] = String(val.c_str());
            }
        }

        // Parse body
        _requestBody = "";
        if (headerEnd + 4 <= raw.size()) {
            _requestBody = String(raw.substr(headerEnd + 4).c_str());
        }

        // Strip query string from path
        std::string cleanPath = pathStr;
        size_t qmark = pathStr.find('?');
        if (qmark != std::string::npos) {
            cleanPath = pathStr.substr(0, qmark);
        }

        // Dispatch to matching route
        _responseHeaders.clear();
        _responseSent = false;
        _responseCode = 404;
        _responseBody = "";
        _responseContentType = "";

        for (auto& r : _routes) {
            if (r.path == String(cleanPath.c_str()) && r.method == method) {
                r.handler();
                break;
            }
        }

        // Build HTTP response
        std::string resp = "HTTP/1.1 " + std::to_string(_responseCode) + " OK\r\n";
        if (!_responseContentType.isEmpty()) {
            resp += "Content-Type: " + std::string(_responseContentType.c_str()) + "\r\n";
        }
        for (auto& h : _responseHeaders) {
            resp += std::string(h.first.c_str()) + ": " + std::string(h.second.c_str()) + "\r\n";
        }
        resp += "Content-Length: " + std::to_string(_responseBody.length()) + "\r\n";
        resp += "Connection: close\r\n";
        resp += "\r\n";
        resp += std::string(_responseBody.c_str(), _responseBody.length());

        // Send response
        const char* data = resp.c_str();
        size_t remaining = resp.size();
        while (remaining > 0) {
            ssize_t n = ::send(fd, data, remaining, 0);
            if (n <= 0) break;
            data += n;
            remaining -= n;
        }
    }
};
