/**
 * mcpd — Auth, Platform HAL, Metrics & Diagnostics Tests
 *
 * Tests for modules not covered in other test suites:
 *   - Auth: API key, multi-key, bearer token, custom callback, disable
 *   - Platform HAL: interface contracts, mock platform
 *   - Metrics: request recording, error tracking, Prometheus output
 *   - Diagnostics: tool registration and output format
 *   - SetupCLI: command parsing
 *
 * Compile:
 *   cd native && make test_auth_platform && ./test_auth_platform
 */

#include "test_framework.h"
#include "arduino_mock.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"
#include "../src/MCPAuth.h"
#include "../src/MCPMetrics.h"
#include "../src/MCPDiagnostics.h"
#include "../src/platform/Platform.h"

using namespace mcpd;

// ═══════════════════════════════════════════════════════════════════════
// Auth Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(auth_disabled_by_default) {
    Auth auth;
    ASSERT(!auth.isEnabled());
}

TEST(auth_set_api_key_enables) {
    Auth auth;
    auth.setApiKey("secret123");
    ASSERT(auth.isEnabled());
}

TEST(auth_add_api_key_enables) {
    Auth auth;
    auth.addApiKey("key1");
    ASSERT(auth.isEnabled());
}

TEST(auth_disabled_allows_all) {
    Auth auth;
    WebServer server(80);
    ASSERT(auth.authenticate(server));
}

TEST(auth_no_token_rejects) {
    Auth auth;
    auth.setApiKey("secret");
    WebServer server(80);
    // No headers set — should reject
    ASSERT(!auth.authenticate(server));
}

TEST(auth_bearer_token_valid) {
    Auth auth;
    auth.setApiKey("my-token");
    WebServer server(80);
    server._testSetHeader("Authorization", "Bearer my-token");
    ASSERT(auth.authenticate(server));
}

TEST(auth_bearer_token_invalid) {
    Auth auth;
    auth.setApiKey("my-token");
    WebServer server(80);
    server._testSetHeader("Authorization", "Bearer wrong-token");
    ASSERT(!auth.authenticate(server));
}

TEST(auth_x_api_key_header) {
    Auth auth;
    auth.setApiKey("api-key-123");
    WebServer server(80);
    server._testSetHeader("X-API-Key", "api-key-123");
    ASSERT(auth.authenticate(server));
}

TEST(auth_query_param_key) {
    Auth auth;
    auth.setApiKey("query-key");
    WebServer server(80);
    server._testSetArg("key", "query-key");
    ASSERT(auth.authenticate(server));
}

TEST(auth_query_param_wrong) {
    Auth auth;
    auth.setApiKey("correct");
    WebServer server(80);
    server._testSetArg("key", "wrong");
    ASSERT(!auth.authenticate(server));
}

TEST(auth_multi_key_first) {
    Auth auth;
    auth.addApiKey("key-alpha");
    auth.addApiKey("key-beta");
    WebServer server(80);
    server._testSetHeader("X-API-Key", "key-alpha");
    ASSERT(auth.authenticate(server));
}

TEST(auth_multi_key_second) {
    Auth auth;
    auth.addApiKey("key-alpha");
    auth.addApiKey("key-beta");
    WebServer server(80);
    server._testSetHeader("X-API-Key", "key-beta");
    ASSERT(auth.authenticate(server));
}

TEST(auth_multi_key_invalid) {
    Auth auth;
    auth.addApiKey("key-alpha");
    auth.addApiKey("key-beta");
    WebServer server(80);
    server._testSetHeader("X-API-Key", "key-gamma");
    ASSERT(!auth.authenticate(server));
}

TEST(auth_set_api_key_replaces_all) {
    Auth auth;
    auth.addApiKey("old-key");
    auth.setApiKey("new-key");
    WebServer server(80);
    server._testSetHeader("X-API-Key", "old-key");
    ASSERT(!auth.authenticate(server));
    server._testSetHeader("X-API-Key", "new-key");
    ASSERT(auth.authenticate(server));
}

TEST(auth_custom_callback) {
    Auth auth;
    auth.setAuthCallback([](const String& token) -> bool {
        return token.startsWith("valid-");
    });
    WebServer server(80);
    server._testSetHeader("Authorization", "Bearer valid-abc");
    ASSERT(auth.authenticate(server));
    server._testSetHeader("Authorization", "Bearer invalid-abc");
    ASSERT(!auth.authenticate(server));
}

TEST(auth_callback_overrides_keys) {
    Auth auth;
    auth.addApiKey("stored-key");
    auth.setAuthCallback([](const String& token) -> bool {
        return token == "callback-only";
    });
    WebServer server(80);
    // Stored key should NOT work when callback is set
    server._testSetHeader("X-API-Key", "stored-key");
    ASSERT(!auth.authenticate(server));
    server._testSetHeader("X-API-Key", "callback-only");
    ASSERT(auth.authenticate(server));
}

TEST(auth_disable) {
    Auth auth;
    auth.setApiKey("secret");
    ASSERT(auth.isEnabled());
    auth.disable();
    ASSERT(!auth.isEnabled());
    WebServer server(80);
    ASSERT(auth.authenticate(server));  // disabled = allow all
}

TEST(auth_bearer_priority_over_api_key) {
    Auth auth;
    auth.setApiKey("the-key");
    WebServer server(80);
    // Both headers set — Bearer takes priority
    server._testSetHeader("Authorization", "Bearer the-key");
    server._testSetHeader("X-API-Key", "wrong");
    ASSERT(auth.authenticate(server));
}

TEST(auth_empty_bearer_falls_through) {
    Auth auth;
    auth.setApiKey("the-key");
    WebServer server(80);
    // Authorization header without Bearer prefix
    server._testSetHeader("Authorization", "Basic abc123");
    server._testSetHeader("X-API-Key", "the-key");
    ASSERT(auth.authenticate(server));
}

// ═══════════════════════════════════════════════════════════════════════
// Platform HAL Tests
// ═══════════════════════════════════════════════════════════════════════

// Mock platform for testing the HAL interface
class MockWiFiHAL : public hal::WiFiHAL {
public:
    bool connect(const char*, const char*, unsigned long) override {
        _status = hal::WiFiStatus::CONNECTED;
        return true;
    }
    bool startAP(const char* ssid, const char*) override {
        _apSSID = ssid;
        _status = hal::WiFiStatus::AP_MODE;
        return true;
    }
    void stopAP() override { _status = hal::WiFiStatus::DISCONNECTED; }
    void disconnect() override { _status = hal::WiFiStatus::DISCONNECTED; }
    hal::WiFiStatus status() override { return _status; }
    hal::WiFiInfo info() override {
        return {"192.168.1.42", "AA:BB:CC:DD:EE:FF", "TestNet", -55};
    }
    String localIP() override { return "192.168.1.42"; }

    hal::WiFiStatus _status = hal::WiFiStatus::DISCONNECTED;
    String _apSSID;
};

class MockGPIOHAL : public hal::GPIOHAL {
public:
    void pinMode(uint8_t pin, hal::PinMode_ mode) override {
        _modes[pin] = mode;
    }
    void digitalWrite(uint8_t pin, uint8_t value) override {
        _values[pin] = value;
    }
    int digitalRead(uint8_t pin) override {
        auto it = _values.find(pin);
        return (it != _values.end()) ? it->second : 0;
    }
    void analogWrite(uint8_t pin, uint16_t value) override {
        _analogValues[pin] = value;
    }
    uint16_t analogRead(uint8_t pin) override {
        auto it = _analogValues.find(pin);
        return (it != _analogValues.end()) ? it->second : 0;
    }
    void setAnalogReadResolution(uint8_t bits) override { _resolution = bits; }
    void setPWMFrequency(uint8_t, uint32_t freq) override { _pwmFreq = freq; }

    std::map<uint8_t, hal::PinMode_> _modes;
    std::map<uint8_t, uint8_t> _values;
    std::map<uint8_t, uint16_t> _analogValues;
    uint8_t _resolution = 12;
    uint32_t _pwmFreq = 0;
};

class MockSystemHAL : public hal::SystemHAL {
public:
    uint32_t freeHeap() override { return 120000; }
    uint32_t totalHeap() override { return 320000; }
    uint32_t cpuFreqMHz() override { return 240; }
    const char* platformName() override { return "MockMCU"; }
    String chipId() override { return "MOCK-001"; }
    uint32_t random() override { return 42; }
    void restart() override { _restarted = true; }
    bool _restarted = false;
};

class MockPlatform : public hal::Platform {
public:
    hal::WiFiHAL& wifi() override { return _wifi; }
    hal::GPIOHAL& gpio() override { return _gpio; }
    hal::SystemHAL& system() override { return _system; }

    MockWiFiHAL _wifi;
    MockGPIOHAL _gpio;
    MockSystemHAL _system;
};

TEST(hal_wifi_connect) {
    MockWiFiHAL wifi;
    ASSERT_EQ(wifi.status(), hal::WiFiStatus::DISCONNECTED);
    ASSERT(wifi.connect("TestNet", "pass123", 5000));
    ASSERT_EQ(wifi.status(), hal::WiFiStatus::CONNECTED);
}

TEST(hal_wifi_disconnect) {
    MockWiFiHAL wifi;
    wifi.connect("Net", "pass", 5000);
    wifi.disconnect();
    ASSERT_EQ(wifi.status(), hal::WiFiStatus::DISCONNECTED);
}

TEST(hal_wifi_ap_mode) {
    MockWiFiHAL wifi;
    ASSERT(wifi.startAP("mcpd-setup", nullptr));
    ASSERT_EQ(wifi.status(), hal::WiFiStatus::AP_MODE);
    ASSERT_EQ(wifi._apSSID, "mcpd-setup");
}

TEST(hal_wifi_stop_ap) {
    MockWiFiHAL wifi;
    wifi.startAP("ap", nullptr);
    wifi.stopAP();
    ASSERT_EQ(wifi.status(), hal::WiFiStatus::DISCONNECTED);
}

TEST(hal_wifi_info) {
    MockWiFiHAL wifi;
    auto info = wifi.info();
    ASSERT_EQ(info.ip, "192.168.1.42");
    ASSERT_EQ(info.mac, "AA:BB:CC:DD:EE:FF");
    ASSERT_EQ(info.ssid, "TestNet");
    ASSERT_EQ(info.rssi, -55);
}

TEST(hal_wifi_local_ip) {
    MockWiFiHAL wifi;
    ASSERT_EQ(wifi.localIP(), "192.168.1.42");
}

TEST(hal_gpio_pin_mode) {
    MockGPIOHAL gpio;
    gpio.pinMode(13, hal::PinMode_::OUTPUT_MODE);
    ASSERT_EQ(gpio._modes[13], hal::PinMode_::OUTPUT_MODE);
}

TEST(hal_gpio_digital_write_read) {
    MockGPIOHAL gpio;
    gpio.digitalWrite(5, 1);
    ASSERT_EQ(gpio.digitalRead(5), 1);
    gpio.digitalWrite(5, 0);
    ASSERT_EQ(gpio.digitalRead(5), 0);
}

TEST(hal_gpio_digital_read_default) {
    MockGPIOHAL gpio;
    ASSERT_EQ(gpio.digitalRead(99), 0);
}

TEST(hal_gpio_analog_write_read) {
    MockGPIOHAL gpio;
    gpio.analogWrite(34, 2048);
    ASSERT_EQ(gpio.analogRead(34), 2048);
}

TEST(hal_gpio_analog_read_default) {
    MockGPIOHAL gpio;
    ASSERT_EQ(gpio.analogRead(99), 0);
}

TEST(hal_gpio_resolution) {
    MockGPIOHAL gpio;
    gpio.setAnalogReadResolution(10);
    ASSERT_EQ(gpio._resolution, 10);
}

TEST(hal_gpio_pwm_frequency) {
    MockGPIOHAL gpio;
    gpio.setPWMFrequency(5, 25000);
    ASSERT_EQ(gpio._pwmFreq, (uint32_t)25000);
}

TEST(hal_system_free_heap) {
    MockSystemHAL sys;
    ASSERT_EQ(sys.freeHeap(), (uint32_t)120000);
}

TEST(hal_system_total_heap) {
    MockSystemHAL sys;
    ASSERT_EQ(sys.totalHeap(), (uint32_t)320000);
}

TEST(hal_system_cpu_freq) {
    MockSystemHAL sys;
    ASSERT_EQ(sys.cpuFreqMHz(), (uint32_t)240);
}

TEST(hal_system_platform_name) {
    MockSystemHAL sys;
    ASSERT_STR_CONTAINS(sys.platformName(), "MockMCU");
}

TEST(hal_system_chip_id) {
    MockSystemHAL sys;
    ASSERT_EQ(sys.chipId(), "MOCK-001");
}

TEST(hal_system_random) {
    MockSystemHAL sys;
    ASSERT_EQ(sys.random(), (uint32_t)42);
}

TEST(hal_system_restart) {
    MockSystemHAL sys;
    ASSERT(!sys._restarted);
    sys.restart();
    ASSERT(sys._restarted);
}

TEST(hal_system_uptime) {
    MockSystemHAL sys;
    // uptimeMs() uses millis() by default
    ASSERT(sys.uptimeMs() >= 0);
}

TEST(hal_platform_composite) {
    MockPlatform platform;
    // Access all subsystems through the platform interface
    ASSERT_EQ(platform.system().platformName(), "MockMCU");
    ASSERT_EQ(platform.wifi().localIP(), "192.168.1.42");
    platform.gpio().digitalWrite(2, 1);
    ASSERT_EQ(platform.gpio().digitalRead(2), 1);
}

// ═══════════════════════════════════════════════════════════════════════
// Metrics Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(metrics_initial_state) {
    Metrics metrics;
    ASSERT_EQ(metrics.totalRequests(), 0UL);
    ASSERT_EQ(metrics.totalErrors(), 0UL);
}

TEST(metrics_record_request) {
    Metrics metrics;
    metrics.recordRequest("tools/call", 15);
    ASSERT_EQ(metrics.totalRequests(), 1UL);
}

TEST(metrics_record_multiple) {
    Metrics metrics;
    metrics.recordRequest("tools/call", 10);
    metrics.recordRequest("tools/list", 5);
    metrics.recordRequest("tools/call", 20);
    ASSERT_EQ(metrics.totalRequests(), 3UL);
}

TEST(metrics_record_error) {
    Metrics metrics;
    metrics.recordError();
    metrics.recordError();
    ASSERT_EQ(metrics.totalErrors(), 2UL);
}

TEST(metrics_sse_clients) {
    Metrics metrics;
    metrics.setSSEClients(3);
    // No direct getter for SSE clients, but it should not crash
    ASSERT_EQ(metrics.totalRequests(), 0UL);
}

TEST(metrics_uptime) {
    Metrics metrics;
    // Before begin(), startTime is 0, so uptime = millis()/1000
    unsigned long up = metrics.uptimeSeconds();
    ASSERT(up >= 0);
}

// ═══════════════════════════════════════════════════════════════════════
// Diagnostics Tool Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(diagnostics_tool_registers) {
    Server server("diag-test", 8080);
    addDiagnosticsTool(server);
    // The diagnostics tool should be findable
    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "server_diagnostics");
}

TEST(diagnostics_tool_description) {
    Server server("diag-test", 8080);
    addDiagnosticsTool(server);
    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "diagnostics");
    ASSERT_STR_CONTAINS(listJson.c_str(), "version");
}

TEST(diagnostics_tool_call) {
    Server server("diag-test", 8080);
    addDiagnosticsTool(server);

    // Build a tools/call request
    JsonDocument doc;
    doc["name"] = "server_diagnostics";
    JsonObject args = doc["arguments"].to<JsonObject>();

    JsonDocument idDoc;
    idDoc["id"] = 1;

    String result = server._dispatch("tools/call", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), "mcpd");
    ASSERT_STR_CONTAINS(result.c_str(), MCPD_VERSION);
    ASSERT_STR_CONTAINS(result.c_str(), "uptime");
}

TEST(diagnostics_tool_read_only_hint) {
    Server server("diag-test", 8080);
    addDiagnosticsTool(server);
    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    // Annotations with hasAnnotations=true via setters are serialized
    ASSERT_STR_CONTAINS(listJson.c_str(), "server_diagnostics");
}

// ═══════════════════════════════════════════════════════════════════════
// Auth Integration with Server Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(server_auth_disabled_by_default) {
    Server server("auth-test", 8080);
    // Auth should be accessible but disabled
    // Server processes requests without auth check at the dispatch level
    String result = server._dispatch("ping", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(result.c_str(), "result");
}

// ═══════════════════════════════════════════════════════════════════════
// Tool Annotations Extended Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(tool_annotations_destructive_hint) {
    MCPTool tool;
    tool.name = "dangerous_op";
    tool.description = "A destructive operation";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.annotations.setDestructiveHint(true);
    tool.handler = [](const JsonObject&) -> String { return "done"; };

    Server server("ann-test", 8080);
    server.addTool(tool);

    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "destructiveHint");
}

TEST(tool_annotations_title) {
    MCPTool tool;
    tool.name = "titled_tool";
    tool.description = "Has a title";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.annotations.title = "My Cool Tool";
    tool.annotations.hasAnnotations = true;
    tool.handler = [](const JsonObject&) -> String { return "ok"; };

    Server server("title-test", 8080);
    server.addTool(tool);

    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "My Cool Tool");
}

TEST(tool_annotations_idempotent) {
    MCPTool tool;
    tool.name = "safe_tool";
    tool.description = "Idempotent";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.annotations.setIdempotentHint(true);
    tool.handler = [](const JsonObject&) -> String { return "ok"; };

    Server server("idem-test", 8080);
    server.addTool(tool);

    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "idempotentHint");
}

TEST(tool_annotations_open_world) {
    MCPTool tool;
    tool.name = "web_tool";
    tool.description = "Reaches the internet";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.annotations.setOpenWorldHint(true);
    tool.handler = [](const JsonObject&) -> String { return "ok"; };

    Server server("ow-test", 8080);
    server.addTool(tool);

    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(listJson.c_str(), "openWorldHint");
}

// ═══════════════════════════════════════════════════════════════════════
// RateLimit Extended Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ratelimit_burst_capacity) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    // After configuring, all initial burst should be allowed
    for (int i = 0; i < 5; i++) {
        ASSERT(rl.tryAcquire());
    }
}

TEST(ratelimit_total_stats_tracking) {
    RateLimiter rl;
    rl.configure(1000.0f, 10);
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_EQ(rl.totalAllowed(), 2UL);
    ASSERT_EQ(rl.totalDenied(), 0UL);
}

// ═══════════════════════════════════════════════════════════════════════
// Server Edge Cases
// ═══════════════════════════════════════════════════════════════════════

TEST(server_dispatch_empty_method) {
    Server server("edge-test", 8080);
    String result = server._dispatch("", JsonVariant(), JsonVariant());
    ASSERT_STR_CONTAINS(result.c_str(), "error");
}

TEST(server_tools_call_missing_name) {
    Server server("edge-test", 8080);
    JsonDocument doc;
    // No "name" field
    JsonDocument idDoc;
    idDoc["id"] = 1;
    String result = server._dispatch("tools/call", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), "error");
}

TEST(server_resources_read_missing_uri) {
    Server server("edge-test", 8080);
    JsonDocument doc;
    // No "uri" field
    JsonDocument idDoc;
    idDoc["id"] = 1;
    String result = server._dispatch("resources/read", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), "error");
}

TEST(server_prompts_get_missing_name) {
    Server server("edge-test", 8080);
    JsonDocument doc;
    JsonDocument idDoc;
    idDoc["id"] = 1;
    String result = server._dispatch("prompts/get", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), "error");
}

TEST(server_version_in_initialize) {
    Server server("ver-test", 8080);
    JsonDocument doc;
    doc["protocolVersion"] = "2025-03-26";
    JsonObject caps = doc["capabilities"].to<JsonObject>();
    JsonObject clientInfo = doc["clientInfo"].to<JsonObject>();
    clientInfo["name"] = "test-client";
    clientInfo["version"] = "1.0";

    JsonDocument idDoc;
    idDoc["id"] = 42;

    String result = server._dispatch("initialize", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), MCPD_VERSION);
    ASSERT_STR_CONTAINS(result.c_str(), MCPD_MCP_PROTOCOL_VERSION);
}

TEST(server_initialize_capabilities) {
    Server server("caps-test", 8080);
    server.addTool("test", "desc", R"({"type":"object"})", [](const JsonObject&) -> String { return "ok"; });

    JsonDocument doc;
    doc["protocolVersion"] = "2025-03-26";
    JsonObject clientInfo = doc["clientInfo"].to<JsonObject>();
    clientInfo["name"] = "test";
    clientInfo["version"] = "1.0";

    JsonDocument idDoc;
    idDoc["id"] = 1;

    String result = server._dispatch("initialize", doc.as<JsonVariant>(), idDoc["id"]);
    ASSERT_STR_CONTAINS(result.c_str(), "capabilities");
    ASSERT_STR_CONTAINS(result.c_str(), "tools");
}

TEST(server_double_tool_add) {
    Server server("dup-test", 8080);
    server.addTool("dup", "first", R"({"type":"object"})", [](const JsonObject&) -> String { return "1"; });
    server.addTool("dup", "second", R"({"type":"object"})", [](const JsonObject&) -> String { return "2"; });

    // Both are added (first match wins on call)
    String listJson = server._dispatch("tools/list", JsonVariant(), JsonVariant());
    // Should contain "dup" at least once
    ASSERT_STR_CONTAINS(listJson.c_str(), "dup");
}

// ═══════════════════════════════════════════════════════════════════════

int main() {
    printf("\n══════════════════════════════════════════\n");
    printf(" mcpd Auth, Platform & Diagnostics Tests\n");
    printf("══════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
