/**
 * mcpd — Native HTTP Integration Tests
 *
 * Starts the MCP server on localhost with real POSIX sockets,
 * sends actual HTTP requests, and verifies MCP compliance.
 */

// Must define MCPD_TEST before includes
#define MCPD_TEST

// Include mocks
#include "../arduino_mock.h"

// ArduinoJson config
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_STD_STRING 1
#define ARDUINOJSON_ENABLE_STD_STREAM 0
#include "../ArduinoJson.h"

// Override WebServer with POSIX socket version BEFORE mcpd.h
// We need to prevent mcpd.h from including the mock WebServer
// since arduino_mock.h already defines it. Let's undefine and redefine.

// The arduino_mock.h already has a WebServer class. We need to replace it
// with the POSIX socket version. Since we can't un-include, we'll use
// a different approach: compile with the mocks/ directory which has the
// POSIX WebServer.

// Actually, let's just use the inline WebServer from arduino_mock.h
// but override handleClient with a POSIX version via subclass.
// Simpler approach: just include mcpd.h which will use the mock WebServer,
// then test via the internal _processJsonRpc + via real HTTP to the POSIX server.

#include "../../src/mcpd.h"
#include "../../src/mcpd.cpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <sstream>

using namespace mcpd;

// ── Simple POSIX HTTP Server wrapping mcpd ─────────────────────────────

class PosixMCPServer {
public:
    Server* mcp;
    int serverFd = -1;
    uint16_t port;
    std::atomic<bool> running{false};
    std::thread thread;

    PosixMCPServer(uint16_t p) : port(p) {
        mcp = new Server("test-device", p);

        mcp->addTool("echo", "Echoes back the input",
            R"({"type":"object","properties":{"message":{"type":"string"}},"required":["message"]})",
            [](const JsonObject& args) -> String {
                const char* msg = args["message"].as<const char*>();
                return String("{\"echo\":\"") + (msg ? msg : "") + "\"}";
            });

        mcp->addTool("add", "Add two numbers",
            R"({"type":"object","properties":{"a":{"type":"integer"},"b":{"type":"integer"}},"required":["a","b"]})",
            [](const JsonObject& args) -> String {
                int a = args["a"];
                int b = args["b"];
                return String("{\"result\":") + String(a + b) + "}";
            });

        mcp->addResource("device://info", "Device Info", "System info", "application/json",
            []() -> String { return "{\"model\":\"ESP32-MOCK\",\"heap\":200000}"; });
    }

    ~PosixMCPServer() {
        stop();
        delete mcp;
    }

    void start() {
        // We don't call mcp->begin() because that uses the mock WebServer.
        // Instead we create our own POSIX socket server and dispatch to mcpd internals.

        // But we need the server to be initialized (tools registered, etc.)
        // Let's initialize it by calling begin and then replacing the transport.
        // Actually, we can call _processJsonRpc directly since MCPD_TEST exposes internals.
        // But we want REAL HTTP. So let's build a minimal HTTP server that calls _processJsonRpc.

        // First, we need to init the mcpd server state (generate session ID on init request)
        // We just use _processJsonRpc for the JSON-RPC layer. The HTTP layer we build ourselves.

        serverFd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(serverFd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif

        struct sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        bind(serverFd, (struct sockaddr*)&addr, sizeof(addr));
        listen(serverFd, 8);
        fcntl(serverFd, F_SETFL, O_NONBLOCK);

        running = true;
        thread = std::thread([this]() { serve(); });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void stop() {
        running = false;
        if (thread.joinable()) thread.join();
        if (serverFd >= 0) { close(serverFd); serverFd = -1; }
    }

private:
    void serve() {
        while (running) {
            struct pollfd pfd = { serverFd, POLLIN, 0 };
            if (poll(&pfd, 1, 20) <= 0) continue;

            struct sockaddr_in ca;
            socklen_t cl = sizeof(ca);
            int fd = accept(serverFd, (struct sockaddr*)&ca, &cl);
            if (fd < 0) continue;

            // Ensure accepted socket is blocking (may inherit non-blocking on macOS)
            int fl = fcntl(fd, F_GETFL, 0);
            fcntl(fd, F_SETFL, fl & ~O_NONBLOCK);

            struct timeval tv = { 2, 0 };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            handleConnection(fd);
            close(fd);
        }
    }

    void handleConnection(int fd) {
        // Read full request
        std::string raw;
        char buf[8192];
        while (true) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            raw.append(buf, n);
            size_t he = raw.find("\r\n\r\n");
            if (he != std::string::npos) {
                // Check content-length
                std::string lower = raw;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                size_t clp = lower.find("content-length:");
                if (clp != std::string::npos) {
                    size_t vs = clp + 15;
                    while (vs < lower.size() && lower[vs] == ' ') vs++;
                    size_t ve = lower.find("\r\n", vs);
                    int cl = std::stoi(lower.substr(vs, ve - vs));
                    if ((int)(raw.size() - he - 4) >= cl) break;
                } else {
                    break;
                }
            }
        }
        if (raw.empty()) return;

        // Parse request line
        size_t fl = raw.find("\r\n");
        std::string rl = raw.substr(0, fl);
        std::istringstream iss(rl);
        std::string method, path, ver;
        iss >> method >> path >> ver;

        // Parse headers
        size_t he = raw.find("\r\n\r\n");
        std::map<std::string, std::string> headers;
        {
            std::string hs = raw.substr(fl + 2, he - fl - 2);
            std::istringstream hss(hs);
            std::string line;
            while (std::getline(hss, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                size_t c = line.find(':');
                if (c != std::string::npos) {
                    std::string k = line.substr(0, c);
                    std::string v = line.substr(c + 1);
                    while (!v.empty() && v[0] == ' ') v.erase(0, 1);
                    std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                    headers[k] = v;
                }
            }
        }

        // Body
        std::string body;
        if (he + 4 <= raw.size()) body = raw.substr(he + 4);

        // Handle the request — dispatch to mcpd's JSON-RPC processor
        int statusCode = 200;
        std::string responseBody;
        std::map<std::string, std::string> respHeaders;

        // CORS
        respHeaders["Access-Control-Allow-Origin"] = "*";
        respHeaders["Access-Control-Allow-Methods"] = "POST, GET, DELETE, OPTIONS";
        respHeaders["Access-Control-Allow-Headers"] = "Content-Type, Accept, Mcp-Session-Id";
        respHeaders["Access-Control-Expose-Headers"] = "Mcp-Session-Id";

        if (method == "OPTIONS") {
            statusCode = 204;
        } else if (method == "POST") {
            if (body.empty()) {
                statusCode = 400;
                responseBody = mcp->_jsonRpcError(JsonVariant(), -32700, "Parse error: empty body").c_str();
            } else {
                // Check session
                if (mcp->_initialized && !mcp->_sessionId.isEmpty()) {
                    auto it = headers.find("mcp-session-id");
                    if (it != headers.end() && it->second != std::string(mcp->_sessionId.c_str())) {
                        statusCode = 404;
                        responseBody = mcp->_jsonRpcError(JsonVariant(), -32600, "Invalid session").c_str();
                    }
                }

                if (statusCode == 200) {
                    String result = mcp->_processJsonRpc(String(body.c_str()));
                    if (result.isEmpty()) {
                        statusCode = 202;
                    } else {
                        responseBody = result.c_str();
                        if (!mcp->_sessionId.isEmpty()) {
                            respHeaders["Mcp-Session-Id"] = mcp->_sessionId.c_str();
                        }
                    }
                }
            }
        } else if (method == "DELETE") {
            auto it = headers.find("mcp-session-id");
            if (it != headers.end() && it->second == std::string(mcp->_sessionId.c_str())) {
                mcp->_initialized = false;
                mcp->_sessionId = "";
                responseBody = "{}";
            } else {
                statusCode = 404;
            }
        } else if (method == "GET") {
            statusCode = 405;
            responseBody = mcp->_jsonRpcError(JsonVariant(), -32601, "SSE not supported yet").c_str();
        }

        // Build response
        std::string resp = "HTTP/1.1 " + std::to_string(statusCode) + " OK\r\n";
        if (!responseBody.empty()) {
            resp += "Content-Type: application/json\r\n";
        }
        for (auto& h : respHeaders) {
            resp += h.first + ": " + h.second + "\r\n";
        }
        resp += "Content-Length: " + std::to_string(responseBody.size()) + "\r\n";
        resp += "Connection: close\r\n\r\n";
        resp += responseBody;

        const char* d = resp.c_str();
        size_t rem = resp.size();
        while (rem > 0) {
            ssize_t n = ::send(fd, d, rem, 0);
            if (n <= 0) break;
            d += n;
            rem -= n;
        }
    }
};

// ── HTTP Client ────────────────────────────────────────────────────────

struct HttpResponse {
    int status = 0;
    std::string body;
    std::map<std::string, std::string> headers;
};

static HttpResponse httpRequest(const std::string& method, uint16_t port,
                                 const std::string& path, const std::string& body = "",
                                 const std::map<std::string, std::string>& extraHeaders = {}) {
    HttpResponse resp;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return resp;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return resp;
    }

    std::string req = method + " " + path + " HTTP/1.1\r\n";
    req += "Host: 127.0.0.1\r\n";
    if (!body.empty()) {
        req += "Content-Type: application/json\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    for (auto& h : extraHeaders) req += h.first + ": " + h.second + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;

    ::send(fd, req.c_str(), req.size(), 0);

    std::string raw;
    char buf[8192];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        raw.append(buf, n);
    }
    close(fd);

    size_t se = raw.find("\r\n");
    if (se != std::string::npos) {
        size_t sp = raw.find(' ');
        if (sp != std::string::npos) resp.status = std::stoi(raw.substr(sp + 1, 3));
    }

    size_t he = raw.find("\r\n\r\n");
    if (he != std::string::npos) {
        // Parse headers
        std::string hs = raw.substr(se + 2, he - se - 2);
        std::istringstream hss(hs);
        std::string line;
        while (std::getline(hss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t c = line.find(':');
            if (c != std::string::npos) {
                std::string k = line.substr(0, c);
                std::string v = line.substr(c + 1);
                while (!v.empty() && v[0] == ' ') v.erase(0, 1);
                std::transform(k.begin(), k.end(), k.begin(), ::tolower);
                resp.headers[k] = v;
            }
        }
        resp.body = raw.substr(he + 4);
    }
    return resp;
}

// ── Test Runner ────────────────────────────────────────────────────────

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  TEST %-50s ", #name); \
    fflush(stdout); \
    try { \
        test_##name(); \
        tests_passed++; \
        printf("✅ PASS\n"); \
    } catch (const char* msg) { \
        tests_failed++; \
        printf("❌ FAIL: %s\n", msg); \
    } catch (const std::exception& e) { \
        tests_failed++; \
        printf("❌ FAIL: %s\n", e.what()); \
    } catch (...) { \
        tests_failed++; \
        printf("❌ FAIL: unknown exception\n"); \
    } \
} while(0)

#define ASSERT(cond) do { if (!(cond)) throw "Assertion failed: " #cond; } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw "Expected equal: " #a " == " #b; } while(0)
#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    std::string _h(haystack); \
    if (_h.find(needle) == std::string::npos) throw "String missing: " #needle; \
} while(0)

static uint16_t PORT = 18923;

static std::string jsonRpc(const std::string& method, const std::string& params, int id = 1) {
    return R"({"jsonrpc":"2.0","id":)" + std::to_string(id) +
           R"(,"method":")" + method + R"(","params":)" + params + "}";
}

// ── Test Functions ─────────────────────────────────────────────────────

void test_initialize() {
    auto r = httpRequest("POST", PORT, "/mcp",
        jsonRpc("initialize", R"({"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}})"));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "protocolVersion");
    ASSERT_STR_CONTAINS(r.body, "2025-03-26");
    ASSERT_STR_CONTAINS(r.body, "test-device");
    ASSERT_STR_CONTAINS(r.body, "capabilities");
    ASSERT(r.headers.count("mcp-session-id") > 0);
}

void test_ping() {
    auto r = httpRequest("POST", PORT, "/mcp", jsonRpc("ping", "{}", 2));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "\"id\":2");
    ASSERT_STR_CONTAINS(r.body, "result");
}

void test_tools_list() {
    auto r = httpRequest("POST", PORT, "/mcp", jsonRpc("tools/list", "{}", 3));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "echo");
    ASSERT_STR_CONTAINS(r.body, "add");
    ASSERT_STR_CONTAINS(r.body, "inputSchema");
}

void test_tools_call_echo() {
    auto r = httpRequest("POST", PORT, "/mcp",
        jsonRpc("tools/call", R"({"name":"echo","arguments":{"message":"hello world"}})", 4));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "hello world");
    ASSERT_STR_CONTAINS(r.body, "\"type\":\"text\"");
}

void test_tools_call_add() {
    auto r = httpRequest("POST", PORT, "/mcp",
        jsonRpc("tools/call", R"({"name":"add","arguments":{"a":17,"b":25}})", 5));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "42");
}

void test_tools_call_not_found() {
    auto r = httpRequest("POST", PORT, "/mcp",
        jsonRpc("tools/call", R"({"name":"nonexistent","arguments":{}})", 6));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "error");
    ASSERT_STR_CONTAINS(r.body, "Tool not found");
}

void test_resources_list() {
    auto r = httpRequest("POST", PORT, "/mcp", jsonRpc("resources/list", "{}", 7));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "device://info");
}

void test_resources_read() {
    auto r = httpRequest("POST", PORT, "/mcp",
        jsonRpc("resources/read", R"({"uri":"device://info"})", 8));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "ESP32-MOCK");
}

void test_method_not_found() {
    auto r = httpRequest("POST", PORT, "/mcp", jsonRpc("bogus/method", "{}", 9));
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "error");
    ASSERT_STR_CONTAINS(r.body, "-32601");
}

void test_invalid_json() {
    auto r = httpRequest("POST", PORT, "/mcp", "not json {{{");
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "error");
    ASSERT_STR_CONTAINS(r.body, "-32700");
}

void test_batch_request() {
    std::string batch = R"([{"jsonrpc":"2.0","id":20,"method":"ping"},{"jsonrpc":"2.0","id":21,"method":"tools/list","params":{}}])";
    auto r = httpRequest("POST", PORT, "/mcp", batch);
    ASSERT_EQ(r.status, 200);
    ASSERT_STR_CONTAINS(r.body, "\"id\":20");
    ASSERT_STR_CONTAINS(r.body, "\"id\":21");
}

void test_notification_returns_202() {
    auto r = httpRequest("POST", PORT, "/mcp",
        R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    ASSERT_EQ(r.status, 202);
}

void test_cors_headers() {
    auto r = httpRequest("POST", PORT, "/mcp", jsonRpc("ping", "{}", 30));
    ASSERT_EQ(r.status, 200);
    ASSERT(r.headers.count("access-control-allow-origin") > 0);
}

void test_session_lifecycle() {
    // Initialize
    auto r1 = httpRequest("POST", PORT, "/mcp",
        jsonRpc("initialize", R"({"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}})"));
    ASSERT_EQ(r1.status, 200);
    std::string sid = r1.headers.count("mcp-session-id") ? r1.headers["mcp-session-id"] : "";
    ASSERT(!sid.empty());

    // Delete session
    auto r2 = httpRequest("DELETE", PORT, "/mcp", "", {{"Mcp-Session-Id", sid}});
    ASSERT_EQ(r2.status, 200);

    // Delete with wrong session → 404
    auto r3 = httpRequest("DELETE", PORT, "/mcp", "", {{"Mcp-Session-Id", "wrong-session"}});
    ASSERT_EQ(r3.status, 404);
}

void test_empty_body() {
    auto r = httpRequest("POST", PORT, "/mcp", "");
    ASSERT_EQ(r.status, 400);
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — Native HTTP Integration Tests\n");
    printf("  ════════════════════════════════════════\n\n");
    printf("  Starting MCP server on localhost:%d...\n\n", PORT);

    PosixMCPServer server(PORT);
    server.start();

    RUN_TEST(initialize);
    RUN_TEST(ping);
    RUN_TEST(tools_list);
    RUN_TEST(tools_call_echo);
    RUN_TEST(tools_call_add);
    RUN_TEST(tools_call_not_found);
    RUN_TEST(resources_list);
    RUN_TEST(resources_read);
    RUN_TEST(method_not_found);
    RUN_TEST(invalid_json);
    RUN_TEST(batch_request);
    RUN_TEST(notification_returns_202);
    RUN_TEST(cors_headers);
    RUN_TEST(session_lifecycle);
    RUN_TEST(empty_body);

    printf("\n  ────────────────────────────────────────\n");
    printf("  Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf("\n\n");

    server.stop();
    return tests_failed > 0 ? 1 : 0;
}
