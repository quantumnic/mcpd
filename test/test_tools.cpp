/**
 * mcpd — Built-in Tools Unit Tests
 *
 * Tests for interrupt monitor, analog watchdog, and other tool-specific logic.
 * Runs on the host (not on MCU) using Arduino API mocks.
 *
 * Compile: g++ -std=c++17 -I../src -I. -Imock_includes -DMCPD_TEST test_tools.cpp -o test_tools && ./test_tools
 */

#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"
#include "../src/tools/MCPGPIOTool.h"

using namespace mcpd;

// ── Helper ─────────────────────────────────────────────────────────────

static Server* makeServer() {
    static Server* s = nullptr;
    if (s) { delete s; }
    s = new Server("tool-test", 8080);
    return s;
}

static String call(Server* s, const char* tool, const char* argsJson) {
    String req = String(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")") +
                 tool + R"(","arguments":)" + argsJson + "}}";
    return s->_processJsonRpc(req);
}

// ── GPIO Tool Tests ────────────────────────────────────────────────────

// Note: tool call responses wrap handler output in {"content":[{"type":"text","text":"..."}]}
// The handler's JSON gets serialized as a string inside "text", so we check for escaped quotes

TEST(gpio_digital_read_returns_value) {
    auto* s = makeServer();
    s->addTool("gpio_digital_read", "Read a digital pin",
        R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})",
        [](const JsonObject& args) -> String {
            int pin = args["pin"] | 0;
            int val = digitalRead(pin);
            return String(R"({"pin":)") + pin + R"(,"value":)" + val + "}";
        });
    String resp = call(s, "gpio_digital_read", R"({"pin":13})");
    // Handler output is embedded as string in content[0].text
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "13");
}

TEST(gpio_digital_write_sets_pin) {
    auto* s = makeServer();
    s->addTool("gpio_digital_write", "Write to a digital pin",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"value":{"type":"integer"}},"required":["pin","value"]})",
        [](const JsonObject& args) -> String {
            int pin = args["pin"] | 0;
            int val = args["value"] | 0;
            pinMode(pin, OUTPUT);
            digitalWrite(pin, val);
            return String(R"({"pin":)") + pin + R"(,"value":)" + val + R"(,"set":true})";
        });
    String resp = call(s, "gpio_digital_write", R"({"pin":2,"value":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "set");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
}

TEST(gpio_analog_read_returns_value) {
    auto* s = makeServer();
    s->addTool("gpio_analog_read", "Read analog pin",
        R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})",
        [](const JsonObject& args) -> String {
            int pin = args["pin"] | 0;
            int val = analogRead(pin);
            return String(R"({"pin":)") + pin + R"(,"value":)" + val + "}";
        });
    String resp = call(s, "gpio_analog_read", R"({"pin":34})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "34");
}

// ── Tool Registration Edge Cases ───────────────────────────────────────

TEST(add_tool_via_mcptool_object) {
    auto* s = makeServer();
    MCPTool tool("test_tool", "A test tool",
        R"({"type":"object","properties":{"x":{"type":"integer"}}})",
        [](const JsonObject& args) -> String {
            int x = args["x"] | 0;
            return String(R"({"result":)") + (x * 2) + "}";
        });
    s->addTool(tool);
    String resp = call(s, "test_tool", R"({"x":21})");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
}

TEST(add_multiple_tools_and_list) {
    auto* s = makeServer();
    s->addTool("t1", "Tool 1", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "{}"; });
    s->addTool("t2", "Tool 2", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "{}"; });
    s->addTool("t3", "Tool 3", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "{}"; });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "t1");
    ASSERT_STR_CONTAINS(resp.c_str(), "t2");
    ASSERT_STR_CONTAINS(resp.c_str(), "t3");
}

TEST(remove_tool_then_call_returns_error) {
    auto* s = makeServer();
    s->addTool("ephemeral", "Temporary tool", R"({"type":"object"})",
        [](const JsonObject&) -> String { return R"({"ok":true})"; });

    // Call succeeds
    String resp1 = call(s, "ephemeral", "{}");
    ASSERT_STR_CONTAINS(resp1.c_str(), "ok");

    // Remove and try again
    ASSERT(s->removeTool("ephemeral"));
    String resp2 = call(s, "ephemeral", "{}");
    ASSERT_STR_CONTAINS(resp2.c_str(), "Tool not found");
}

// ── Resource Registration ──────────────────────────────────────────────

TEST(resource_read_custom_handler) {
    auto* s = makeServer();
    s->addResource("device://battery", "Battery", "Battery level", "application/json",
        []() -> String { return R"({"level":87,"charging":true})"; });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"device://battery"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "87");
    ASSERT_STR_CONTAINS(resp.c_str(), "charging");
}

TEST(resource_template_with_variable) {
    auto* s = makeServer();
    s->addResourceTemplate("sensor://{id}/reading", "Sensor Reading",
        "Read a specific sensor", "application/json",
        [](const std::map<String, String>& params) -> String {
            auto it = params.find("id");
            String id = (it != params.end()) ? it->second : "unknown";
            return String(R"({"sensor":")") + id + R"(","value":23.5})";
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"sensor://temp1/reading"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "temp1");
    ASSERT_STR_CONTAINS(resp.c_str(), "23.5");
}

// ── Prompt Tests ───────────────────────────────────────────────────────

TEST(prompt_with_no_arguments) {
    auto* s = makeServer();
    s->addPrompt("status", "Get system status", {},
        [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> {
            return { MCPPromptMessage("user", "What is the current system status?") };
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"status"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "current system status");
}

TEST(prompt_with_multiple_messages) {
    auto* s = makeServer();
    s->addPrompt("diagnose", "Diagnose a problem",
        { MCPPromptArgument("symptom", "The symptom", true) },
        [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
            String symptom = args.at("symptom");
            return {
                MCPPromptMessage("user", (String("I'm seeing: ") + symptom).c_str()),
                MCPPromptMessage("assistant", "Let me analyze that for you."),
                MCPPromptMessage("user", "Please check all sensors.")
            };
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"diagnose","arguments":{"symptom":"high temperature"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "high temperature");
    ASSERT_STR_CONTAINS(resp.c_str(), "analyze");
    ASSERT_STR_CONTAINS(resp.c_str(), "check all sensors");
}

// ── Rich Tool Results ──────────────────────────────────────────────────

TEST(rich_tool_with_multiple_content_items) {
    auto* s = makeServer();
    s->addRichTool("sensor_report", "Full sensor report",
        R"({"type":"object"})",
        [](const JsonObject&) -> MCPToolResult {
            MCPToolResult result;
            result.content.push_back(MCPContent::makeText("Temperature: 22.5°C"));
            result.content.push_back(MCPContent::makeText("Humidity: 65%"));
            result.isError = false;
            return result;
        });

    String resp = call(s, "sensor_report", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "22.5");
    ASSERT_STR_CONTAINS(resp.c_str(), "65%");
}

// ── Error Handling ─────────────────────────────────────────────────────

TEST(tool_handler_exception_returns_error) {
    auto* s = makeServer();
    s->addTool("crasher", "Tool that throws", R"({"type":"object"})",
        [](const JsonObject&) -> String {
            throw "boom";
            return "{}";
        });

    String resp = call(s, "crasher", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "Internal tool error");
}

TEST(tool_call_with_missing_arguments_key) {
    auto* s = makeServer();
    s->addTool("simple", "Simple tool", R"({"type":"object"})",
        [](const JsonObject&) -> String { return R"({"ok":true})"; });

    // No arguments key at all — should still work (args will be null/empty)
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"simple"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "ok");
}

// ── Batch Operations ───────────────────────────────────────────────────

TEST(batch_multiple_tool_calls) {
    auto* s = makeServer();
    s->addTool("double", "Double a number",
        R"({"type":"object","properties":{"n":{"type":"integer"}},"required":["n"]})",
        [](const JsonObject& args) -> String {
            int n = args["n"] | 0;
            return String(R"({"result":)") + (n * 2) + "}";
        });

    String req = R"([
        {"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"double","arguments":{"n":5}}},
        {"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"double","arguments":{"n":10}}}
    ])";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "10");
    ASSERT_STR_CONTAINS(resp.c_str(), "20");
}

// ── Session / Initialize Edge Cases ────────────────────────────────────

TEST(double_initialize_resets_session) {
    auto* s = makeServer();
    String req1 = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test","version":"1.0"}}})";
    String resp1 = s->_processJsonRpc(req1);
    ASSERT_STR_CONTAINS(resp1.c_str(), "protocolVersion");

    String req2 = R"({"jsonrpc":"2.0","id":2,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test","version":"1.0"}}})";
    String resp2 = s->_processJsonRpc(req2);
    ASSERT_STR_CONTAINS(resp2.c_str(), "protocolVersion");
}

TEST(ping_returns_empty_object) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":99,"method":"ping"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":99");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\":{}");
}

// ── SD Card Tool Tests ─────────────────────────────────────────────────

#include "../src/tools/MCPSDCardTool.h"

TEST(sd_mount_succeeds) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = false;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);
    String resp = call(s, "sd_mount", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "mounted");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
    ASSERT(mcpd::tools::SDCardTool::mounted());
}

TEST(sd_write_and_read_file) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);

    String wresp = call(s, "sd_write", R"({"path":"/test.txt","content":"hello world"})");
    ASSERT_STR_CONTAINS(wresp.c_str(), "bytes_written");
    ASSERT_STR_CONTAINS(wresp.c_str(), "11");

    String rresp = call(s, "sd_read", R"({"path":"/test.txt"})");
    ASSERT_STR_CONTAINS(rresp.c_str(), "hello world");
    ASSERT_STR_CONTAINS(rresp.c_str(), "bytes_read");
}

TEST(sd_append_creates_and_appends) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);

    call(s, "sd_append", R"({"path":"/log.csv","content":"a,b,c\n"})");
    call(s, "sd_append", R"({"path":"/log.csv","content":"1,2,3\n"})");

    String rresp = call(s, "sd_read", R"({"path":"/log.csv"})");
    ASSERT_STR_CONTAINS(rresp.c_str(), "a,b,c");
    ASSERT_STR_CONTAINS(rresp.c_str(), "1,2,3");
}

TEST(sd_read_not_mounted_error) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = false;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);
    String resp = call(s, "sd_read", R"({"path":"/test.txt"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not mounted");
}

TEST(sd_delete_file) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);

    call(s, "sd_write", R"({"path":"/temp.txt","content":"data"})");
    String dresp = call(s, "sd_delete", R"({"path":"/temp.txt"})");
    ASSERT_STR_CONTAINS(dresp.c_str(), "deleted");
    ASSERT_STR_CONTAINS(dresp.c_str(), "true");

    String rresp = call(s, "sd_read", R"({"path":"/temp.txt"})");
    ASSERT_STR_CONTAINS(rresp.c_str(), "not found");
}

TEST(sd_delete_nonexistent_error) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);
    String resp = call(s, "sd_delete", R"({"path":"/ghost.txt"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not found");
}

TEST(sd_list_shows_files) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);

    call(s, "sd_write", R"({"path":"/data.csv","content":"x"})");
    call(s, "sd_write", R"({"path":"/config.json","content":"y"})");

    String resp = call(s, "sd_list", R"({"path":"/"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "data.csv");
    ASSERT_STR_CONTAINS(resp.c_str(), "config.json");
}

TEST(sd_info_shows_stats) {
    auto* s = makeServer();
    mcpd::tools::SDCardTool::mounted() = true;
    mcpd::tools::SDCardTool::emulatedFS().clear();
    mcpd::tools::addSDCardTools(*s, 5);
    String resp = call(s, "sd_info", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "total_mb");
    ASSERT_STR_CONTAINS(resp.c_str(), "emulated");
}

// ── Battery Tool Tests ─────────────────────────────────────────────────

#include "../src/tools/MCPBatteryTool.h"

TEST(battery_read_returns_voltage) {
    auto* s = makeServer();
    mcpd::tools::BatteryTool::history().clear();
    mcpd::tools::addBatteryTools(*s, 34, 2.0f);
    String resp = call(s, "battery_read", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "voltage");
    ASSERT_STR_CONTAINS(resp.c_str(), "percentage");
    ASSERT_STR_CONTAINS(resp.c_str(), "level");
    ASSERT_STR_CONTAINS(resp.c_str(), "LiPo");
}

TEST(battery_status_shows_trend) {
    auto* s = makeServer();
    mcpd::tools::BatteryTool::history().clear();
    mcpd::tools::addBatteryTools(*s, 34, 2.0f);
    String resp = call(s, "battery_status", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "trend");
    ASSERT_STR_CONTAINS(resp.c_str(), "config");
    ASSERT_STR_CONTAINS(resp.c_str(), "v_full");
}

TEST(battery_calibrate_updates_config) {
    auto* s = makeServer();
    mcpd::tools::BatteryTool::history().clear();
    mcpd::tools::addBatteryTools(*s, 34, 2.0f);
    String resp = call(s, "battery_calibrate", R"({"v_full":3.6,"v_empty":2.5,"chemistry":"LiFePO4"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "calibrated");
    ASSERT_STR_CONTAINS(resp.c_str(), "LiFePO4");
    ASSERT_STR_CONTAINS(resp.c_str(), "3.60");
    ASSERT_STR_CONTAINS(resp.c_str(), "2.50");
}

TEST(battery_history_returns_readings) {
    auto* s = makeServer();
    mcpd::tools::BatteryTool::history().clear();
    mcpd::tools::addBatteryTools(*s, 34, 2.0f);
    // Generate a few readings
    call(s, "battery_read", R"({})");
    call(s, "battery_read", R"({})");
    call(s, "battery_read", R"({})");

    String resp = call(s, "battery_history", R"({"count":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "readings");
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
}

// ── Camera Tool Tests (non-ESP32 fallback) ─────────────────────────────

#include "../src/tools/MCPCameraTool.h"

TEST(camera_base64_encode_empty) {
    uint8_t data[] = {};
    String result = mcpd::_base64Encode(data, 0);
    ASSERT_EQ(result.length(), (unsigned int)0);
}

TEST(camera_base64_encode_hello) {
    const uint8_t data[] = {'H', 'e', 'l', 'l', 'o'};
    String result = mcpd::_base64Encode(data, 5);
    ASSERT(result == "SGVsbG8=");
}

TEST(camera_base64_encode_padding) {
    const uint8_t data[] = {'A', 'B'};
    String result = mcpd::_base64Encode(data, 2);
    ASSERT(result == "QUI=");
}

TEST(camera_base64_encode_no_padding) {
    const uint8_t data[] = {'A', 'B', 'C'};
    String result = mcpd::_base64Encode(data, 3);
    ASSERT(result == "QUJD");
}

TEST(camera_init_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String resp = call(s, "camera_init", R"({"resolution":"VGA"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(camera_capture_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String resp = call(s, "camera_capture", R"({"flash":false})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(camera_status_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String resp = call(s, "camera_status", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(camera_configure_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String resp = call(s, "camera_configure", R"({"brightness":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(camera_flash_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String resp = call(s, "camera_flash", R"({"on":true})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(camera_tools_register_all_five) {
    auto* s = makeServer();
    mcpd::addCameraTools(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "camera_init");
    ASSERT_STR_CONTAINS(resp.c_str(), "camera_capture");
    ASSERT_STR_CONTAINS(resp.c_str(), "camera_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "camera_configure");
    ASSERT_STR_CONTAINS(resp.c_str(), "camera_flash");
}

// ── ESP-NOW Tool Tests (non-ESP32 fallback) ────────────────────────────

#include "../src/tools/MCPESPNowTool.h"

TEST(espnow_mac_to_string) {
    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    String result = mcpd::_macToString(mac);
    ASSERT(result == "AA:BB:CC:DD:EE:FF");
}

TEST(espnow_parse_mac_valid) {
    uint8_t mac[6] = {};
    bool ok = mcpd::_parseMac("AA:BB:CC:DD:EE:FF", mac);
    ASSERT(ok);
    ASSERT_EQ(mac[0], (uint8_t)0xAA);
    ASSERT_EQ(mac[5], (uint8_t)0xFF);
}

TEST(espnow_parse_mac_invalid) {
    uint8_t mac[6] = {};
    bool ok = mcpd::_parseMac("not-a-mac", mac);
    ASSERT(!ok);
}

TEST(espnow_init_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_init", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_send_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_send", R"({"mac":"AA:BB:CC:DD:EE:FF","data":"hello"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_receive_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_receive", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_peers_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_peers", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_broadcast_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_broadcast", R"({"data":"hello all"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_add_peer_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String resp = call(s, "espnow_add_peer", R"({"mac":"AA:BB:CC:DD:EE:FF"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(espnow_tools_register_all_six) {
    auto* s = makeServer();
    mcpd::addESPNowTools(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_init");
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_add_peer");
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_send");
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_receive");
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_peers");
    ASSERT_STR_CONTAINS(resp.c_str(), "espnow_broadcast");
}

// ── LoRa Tool Tests ────────────────────────────────────────────────────

#include "../src/tools/MCPLoRaTool.h"

TEST(lora_init_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_init", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_send_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_send", R"({"data":"hello"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_receive_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_receive", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_configure_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_configure", R"({"txPower":10})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_status_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_status", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_sleep_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_sleep", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_cad_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String resp = call(s, "lora_cad", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(lora_tools_register_all_seven) {
    auto* s = makeServer();
    mcpd::addLoRaTools(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_init");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_send");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_receive");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_configure");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_sleep");
    ASSERT_STR_CONTAINS(resp.c_str(), "lora_cad");
}

// ── I2S Audio Tool Tests ───────────────────────────────────────────────

#include "../src/tools/MCPI2SAudioTool.h"

TEST(i2s_base64_encode_empty) {
    String result = mcpd::tools::_i2sBase64Encode(nullptr, 0);
    ASSERT(result.length() == 0);
}

TEST(i2s_base64_encode_hello) {
    const uint8_t data[] = { 'H', 'e', 'l', 'l', 'o' };
    String result = mcpd::tools::_i2sBase64Encode(data, 5);
    ASSERT(result == "SGVsbG8=");
}

TEST(i2s_base64_encode_3bytes) {
    const uint8_t data[] = { 0x00, 0x01, 0x02 };
    String result = mcpd::tools::_i2sBase64Encode(data, 3);
    ASSERT(result == "AAEC");
}

TEST(i2s_base64_encode_1byte) {
    const uint8_t data[] = { 0xFF };
    String result = mcpd::tools::_i2sBase64Encode(data, 1);
    ASSERT(result == "/w==");
}

TEST(i2s_init_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_init", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_record_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_record", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_play_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_play", R"({"data":"SGVsbG8="})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_volume_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_volume", R"({"volume":50})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_status_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_status", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_stop_non_esp32_returns_error) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String resp = call(s, "i2s_stop", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "only supported on ESP32");
}

TEST(i2s_tools_register_all_six) {
    auto* s = makeServer();
    mcpd::addI2SAudioTools(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_init");
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_record");
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_play");
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_volume");
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "i2s_stop");
}

// ── Modbus Tool Tests ──────────────────────────────────────────────────

#include "../src/tools/MCPModbusTool.h"
#include "../src/tools/MCPADCTool.h"
#include "../src/tools/MCPPWMTool.h"
#include "../src/tools/MCPDHTTool.h"
#include "../src/tools/MCPBuzzerTool.h"
#include "../src/tools/MCPNeoPixelTool.h"
#include "../src/tools/MCPWiFiTool.h"
#include "../src/tools/MCPEthernetTool.h"
#include "../src/tools/MCPServoTool.h"
#include "../src/tools/MCPSystemTool.h"
#include "../src/tools/MCPUltrasonicTool.h"
#include "../src/tools/MCPNVSTool.h"
#include "../src/tools/MCPRelayTool.h"
#include "../src/tools/MCPUARTTool.h"
#include "../src/tools/MCPTimerTool.h"
#include "../src/tools/MCPTouchTool.h"
#include "../src/tools/MCPEncoderTool.h"
#include "../src/tools/MCPPulseCounterTool.h"
#include "../src/tools/MCPAnalogWatchTool.h"

TEST(modbus_crc16_known_value) {
    // Standard Modbus CRC test: slave 1, FC 03, addr 0, count 1
    uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    uint16_t crc = mcpd::tools::_modbusCRC16(frame, 6);
    // Known CRC for this frame: 0x0A84
    ASSERT_EQ(crc, (uint16_t)0x0A84);
}

TEST(modbus_crc16_empty) {
    uint16_t crc = mcpd::tools::_modbusCRC16(nullptr, 0);
    ASSERT_EQ(crc, (uint16_t)0xFFFF);
}

TEST(modbus_crc16_single_byte) {
    uint8_t data[] = {0x01};
    uint16_t crc = mcpd::tools::_modbusCRC16(data, 1);
    // CRC of single byte 0x01 = 0x807E
    ASSERT_EQ(crc, (uint16_t)0x807E);
}

TEST(modbus_exception_names) {
    ASSERT_STR_CONTAINS(mcpd::tools::_modbusExceptionName(1), "ILLEGAL_FUNCTION");
    ASSERT_STR_CONTAINS(mcpd::tools::_modbusExceptionName(2), "ILLEGAL_DATA_ADDRESS");
    ASSERT_STR_CONTAINS(mcpd::tools::_modbusExceptionName(3), "ILLEGAL_DATA_VALUE");
    ASSERT_STR_CONTAINS(mcpd::tools::_modbusExceptionName(4), "SLAVE_DEVICE_FAILURE");
    ASSERT_STR_CONTAINS(mcpd::tools::_modbusExceptionName(99), "UNKNOWN");
}

TEST(modbus_init_rtu) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::NONE;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_init", R"({"mode":"rtu","baud":19200,"dePin":4})");
    ASSERT_STR_CONTAINS(resp.c_str(), "rtu");
    ASSERT_STR_CONTAINS(resp.c_str(), "19200");
    ASSERT_STR_CONTAINS(resp.c_str(), "ok");
}

TEST(modbus_init_tcp) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::NONE;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_init", R"({"mode":"tcp","host":"10.0.0.1","port":502})");
    ASSERT_STR_CONTAINS(resp.c_str(), "tcp");
    ASSERT_STR_CONTAINS(resp.c_str(), "10.0.0.1");
    ASSERT_STR_CONTAINS(resp.c_str(), "502");
}

TEST(modbus_init_invalid_mode) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::NONE;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_init", R"({"mode":"invalid"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "Invalid mode");
}

TEST(modbus_read_coils_not_initialized) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::NONE;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_read_coils", R"({"slaveId":1,"address":0,"count":8})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not initialized");
}

TEST(modbus_read_coils_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_read_coils", R"({"slaveId":1,"address":0,"count":8})");
    ASSERT_STR_CONTAINS(resp.c_str(), "fc");
    ASSERT_STR_CONTAINS(resp.c_str(), "coils");
    ASSERT_STR_CONTAINS(resp.c_str(), "slaveId");
}

TEST(modbus_read_coils_count_validation) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_read_coils", R"({"slaveId":1,"address":0,"count":5000})");
    ASSERT_STR_CONTAINS(resp.c_str(), "Count must be 1-2000");
}

TEST(modbus_read_holding_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_read_holding", R"({"slaveId":2,"address":100,"count":5,"format":"float32"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "fc");
    ASSERT_STR_CONTAINS(resp.c_str(), "registers");
    ASSERT_STR_CONTAINS(resp.c_str(), "float32");
}

TEST(modbus_read_holding_count_validation) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_read_holding", R"({"slaveId":1,"address":0,"count":200})");
    ASSERT_STR_CONTAINS(resp.c_str(), "Count must be 1-125");
}

TEST(modbus_write_coil_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_write_coil", R"({"slaveId":1,"address":0,"value":true})");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
    ASSERT_STR_CONTAINS(resp.c_str(), "fc");
}

TEST(modbus_write_register_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_write_register", R"({"slaveId":1,"address":40001,"value":1500})");
    ASSERT_STR_CONTAINS(resp.c_str(), "1500");
    ASSERT_STR_CONTAINS(resp.c_str(), "fc");
}

TEST(modbus_write_registers_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_write_registers", R"({"slaveId":1,"address":0,"values":[100,200,300]})");
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
    ASSERT_STR_CONTAINS(resp.c_str(), "3");
}

TEST(modbus_scan_success) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_scan", R"({"startAddr":1,"endAddr":10})");
    ASSERT_STR_CONTAINS(resp.c_str(), "scanning");
    ASSERT_STR_CONTAINS(resp.c_str(), "found");
}

TEST(modbus_scan_not_initialized) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::NONE;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_scan", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not initialized");
}

TEST(modbus_status_counters) {
    auto* s = makeServer();
    mcpd::tools::_modbusMode = mcpd::tools::ModbusMode::RTU;
    mcpd::tools::_modbusStats = mcpd::tools::ModbusStats{};
    mcpd::tools::_modbusStats.requests = 42;
    mcpd::tools::_modbusStats.responses = 40;
    mcpd::tools::_modbusStats.timeouts = 2;
    mcpd::tools::ModbusTool::attach(*s);
    String resp = call(s, "modbus_status", R"({})");
    ASSERT_STR_CONTAINS(resp.c_str(), "rtu");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
    ASSERT_STR_CONTAINS(resp.c_str(), "40");
    ASSERT_STR_CONTAINS(resp.c_str(), "timeouts");
}

TEST(modbus_tools_register_all_eleven) {
    auto* s = makeServer();
    mcpd::tools::ModbusTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_init");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_read_coils");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_read_discrete");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_read_holding");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_read_input");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_write_coil");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_write_register");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_write_coils");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_write_registers");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_scan");
    ASSERT_STR_CONTAINS(resp.c_str(), "modbus_status");
}

// ── ADC Tool Tests ─────────────────────────────────────────────────────

TEST(adc_read_single_sample) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read", R"({"pin":34})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "value");
    ASSERT_STR_CONTAINS(resp.c_str(), "samples");
}

TEST(adc_read_multi_samples) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read", R"({"pin":34,"samples":8})");
    ASSERT_STR_CONTAINS(resp.c_str(), "min");
    ASSERT_STR_CONTAINS(resp.c_str(), "max");
}

TEST(adc_read_voltage) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read_voltage", R"({"pin":34})");
    ASSERT_STR_CONTAINS(resp.c_str(), "voltage");
    ASSERT_STR_CONTAINS(resp.c_str(), "raw");
    ASSERT_STR_CONTAINS(resp.c_str(), "vref");
}

TEST(adc_read_voltage_custom_vref) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read_voltage", R"({"pin":34,"vref":5.0,"resolution":10})");
    ASSERT_STR_CONTAINS(resp.c_str(), "5.0");
    ASSERT_STR_CONTAINS(resp.c_str(), "resolution");
}

TEST(adc_read_multi_pins) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read_multi", R"({"pins":[34,35,36]})");
    ASSERT_STR_CONTAINS(resp.c_str(), "readings");
}

TEST(adc_read_multi_no_pins) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String resp = call(s, "adc_read_multi", R"({"pins":[]})");
    ASSERT_STR_CONTAINS(resp.c_str(), "No pins");
}

TEST(adc_tools_register) {
    auto* s = makeServer();
    mcpd::tools::ADCTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "adc_read");
    ASSERT_STR_CONTAINS(resp.c_str(), "adc_read_voltage");
    ASSERT_STR_CONTAINS(resp.c_str(), "adc_read_multi");
}

// ── PWM Tool Tests ─────────────────────────────────────────────────────

TEST(pwm_tools_register) {
    auto* s = makeServer();
    mcpd::tools::PWMTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pwm_write");
    ASSERT_STR_CONTAINS(resp.c_str(), "pwm_stop");
}

TEST(pwm_write_basic) {
    auto* s = makeServer();
    mcpd::tools::PWMTool::attach(*s);
    String resp = call(s, "pwm_write", R"({"pin":25,"duty":128})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "duty");
}

// ── DHT Tool Tests ─────────────────────────────────────────────────────

TEST(dht_tools_register) {
    auto* s = makeServer();
    DHT dht(4, DHT22);
    mcpd::tools::DHTTool::attach(*s, dht);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "dht_read");
}

TEST(dht_read_returns_temp_humidity) {
    auto* s = makeServer();
    DHT dht(4, DHT22);
    mcpd::tools::DHTTool::attach(*s, dht);
    String resp = call(s, "dht_read", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "temperature");
    ASSERT_STR_CONTAINS(resp.c_str(), "humidity");
}

// ── Buzzer Tool Tests ──────────────────────────────────────────────────

TEST(buzzer_tools_register) {
    auto* s = makeServer();
    mcpd::tools::BuzzerTool::attach(*s, 15);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "buzzer_tone");
    ASSERT_STR_CONTAINS(resp.c_str(), "buzzer_melody");
}

TEST(buzzer_tone_basic) {
    auto* s = makeServer();
    mcpd::tools::BuzzerTool::attach(*s, 15);
    String resp = call(s, "buzzer_tone", R"({"frequency":1000,"duration":500})");
    ASSERT_STR_CONTAINS(resp.c_str(), "frequency");
    ASSERT_STR_CONTAINS(resp.c_str(), "1000");
}

TEST(buzzer_melody_predefined) {
    auto* s = makeServer();
    mcpd::tools::BuzzerTool::attach(*s, 15);
    String resp = call(s, "buzzer_melody", R"({"name":"alert"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "notes_played");
}

// ── NeoPixel Tool Tests ────────────────────────────────────────────────

TEST(neopixel_tools_register) {
    auto* s = makeServer();
    Adafruit_NeoPixel strip(8, 16);
    mcpd::tools::NeoPixelTool::attach(*s, strip);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "neopixel_set");
    ASSERT_STR_CONTAINS(resp.c_str(), "neopixel_fill");
    ASSERT_STR_CONTAINS(resp.c_str(), "neopixel_clear");
    ASSERT_STR_CONTAINS(resp.c_str(), "neopixel_brightness");
}

TEST(neopixel_set_color) {
    auto* s = makeServer();
    Adafruit_NeoPixel strip(8, 16);
    mcpd::tools::NeoPixelTool::attach(*s, strip);
    String resp = call(s, "neopixel_set", R"({"index":0,"r":255,"g":0,"b":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "index");
}

// ── WiFi Tool Tests ────────────────────────────────────────────────────

TEST(wifi_tools_register) {
    auto* s = makeServer();
    mcpd::tools::WiFiTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "wifi_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "wifi_scan");
}

TEST(wifi_status_returns_info) {
    auto* s = makeServer();
    mcpd::tools::WiFiTool::attach(*s);
    String resp = call(s, "wifi_status", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "connected");
    ASSERT_STR_CONTAINS(resp.c_str(), "ip");
    ASSERT_STR_CONTAINS(resp.c_str(), "mac");
}

TEST(wifi_scan_returns_networks) {
    auto* s = makeServer();
    mcpd::tools::WiFiTool::attach(*s);
    String resp = call(s, "wifi_scan", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
    ASSERT_STR_CONTAINS(resp.c_str(), "networks");
}

// ── Ethernet Tool Tests ────────────────────────────────────────────────

TEST(ethernet_tools_register) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "ethernet_config");
    ASSERT_STR_CONTAINS(resp.c_str(), "ethernet_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "ethernet_ping");
    ASSERT_STR_CONTAINS(resp.c_str(), "ethernet_dns_lookup");
}

// Note: Ethernet tool uses static state, so tests must account for shared config.
// We test uninitialized state FIRST, then initialize and test subsequent operations.

TEST(ethernet_status_before_init) {
    // This must run before any ethernet_config call
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_status", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "not initialized");
}

TEST(ethernet_ping_before_init) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_ping", R"({"host":"192.168.1.1"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not initialized");
}

TEST(ethernet_dns_before_init) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_dns_lookup", R"({"hostname":"example.com"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "not initialized");
}

TEST(ethernet_config_w5500) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_config", R"({"chip":"w5500","cs_pin":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "initialized");
    ASSERT_STR_CONTAINS(resp.c_str(), "w5500");
}

TEST(ethernet_config_static_ip) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_config",
        R"({"chip":"enc28j60","cs_pin":10,"dhcp":false,"ip":"192.168.1.100","gateway":"192.168.1.1"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "enc28j60");
    ASSERT_STR_CONTAINS(resp.c_str(), "192.168.1.100");
}

TEST(ethernet_config_custom_mac) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_config",
        R"({"chip":"w5500","cs_pin":5,"mac":"AA:BB:CC:DD:EE:FF"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "AA:BB:CC:DD:EE:FF");
}

TEST(ethernet_status_after_init) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    // Already initialized from previous tests (static state)
    String resp = call(s, "ethernet_status", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "link_up");
    ASSERT_STR_CONTAINS(resp.c_str(), "rx_bytes");
    ASSERT_STR_CONTAINS(resp.c_str(), "tx_bytes");
    ASSERT_STR_CONTAINS(resp.c_str(), "uptime_ms");
}

TEST(ethernet_ping_after_init) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_ping", R"({"host":"192.168.1.1","count":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "192.168.1.1");
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
}

TEST(ethernet_dns_empty_hostname) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_dns_lookup", R"({"hostname":""})");
    ASSERT_STR_CONTAINS(resp.c_str(), "No hostname");
}

TEST(ethernet_dns_lookup) {
    auto* s = makeServer();
    mcpd::tools::addEthernetTools(*s);
    String resp = call(s, "ethernet_dns_lookup", R"({"hostname":"example.com"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "example.com");
}

// ── Servo Tool Tests ───────────────────────────────────────────────────

TEST(servo_tools_register) {
    auto* s = makeServer();
    mcpd::tools::ServoTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "servo_write");
}

TEST(servo_write_angle) {
    auto* s = makeServer();
    mcpd::tools::ServoTool::attach(*s);
    String resp = call(s, "servo_write", R"({"pin":18,"angle":90})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "angle");
}

// ── System Tool Tests ──────────────────────────────────────────────────

TEST(system_tools_register) {
    auto* s = makeServer();
    mcpd::tools::SystemTool::attach(*s);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "system_info");
}

TEST(system_info_returns_data) {
    auto* s = makeServer();
    mcpd::tools::SystemTool::attach(*s);
    String resp = call(s, "system_info", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "heap");
    ASSERT_STR_CONTAINS(resp.c_str(), "uptime");
}

// ── Ultrasonic Tool Tests ──────────────────────────────────────────────

TEST(ultrasonic_tools_register) {
    auto* s = makeServer();
    mcpd::tools::UltrasonicTool::attach(*s, 12, 14);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "distance_read");
    ASSERT_STR_CONTAINS(resp.c_str(), "distance_config");
}

TEST(ultrasonic_read_distance) {
    auto* s = makeServer();
    mcpd::tools::UltrasonicTool::attach(*s, 12, 14);
    String resp = call(s, "distance_read", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "distance");
}

// ── NVS Tool Tests ─────────────────────────────────────────────────────

TEST(nvs_tools_register) {
    auto* s = makeServer();
    mcpd::addNVSTools(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");

    ASSERT_STR_CONTAINS(resp.c_str(), "nvs_set");
    ASSERT_STR_CONTAINS(resp.c_str(), "nvs_get");
    ASSERT_STR_CONTAINS(resp.c_str(), "nvs_delete");
    ASSERT_STR_CONTAINS(resp.c_str(), "nvs_list");
    ASSERT_STR_CONTAINS(resp.c_str(), "nvs_status");
}

TEST(nvs_set_and_get_string) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::tools::NVSTool::totalOps() = 0;
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_set", R"({"key":"name","value":"test123"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "persisted");
    ASSERT_STR_CONTAINS(resp.c_str(), "test123");
    resp = call(s, "nvs_get", R"({"key":"name"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "found\\\":true");
    ASSERT_STR_CONTAINS(resp.c_str(), "test123");
}

TEST(nvs_set_and_get_int) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_set", R"({"key":"count","value":42})");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
    ASSERT_STR_CONTAINS(resp.c_str(), "int");
    resp = call(s, "nvs_get", R"({"key":"count"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
}

TEST(nvs_get_missing_key) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_get", R"({"key":"nonexistent"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "found\\\":false");
}

TEST(nvs_delete_key) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    call(s, "nvs_set", R"({"key":"temp","value":"hello"})");
    String resp = call(s, "nvs_delete", R"({"key":"temp"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "deleted\\\":true");
    resp = call(s, "nvs_get", R"({"key":"temp"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "found\\\":false");
}

TEST(nvs_delete_missing_key) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_delete", R"({"key":"nope"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "deleted\\\":false");
}

TEST(nvs_list_entries) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    call(s, "nvs_set", R"({"key":"a","value":"1"})");
    call(s, "nvs_set", R"({"key":"b","value":2})");
    String resp = call(s, "nvs_list", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "count\\\":2");
}

TEST(nvs_status) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::tools::NVSTool::totalOps() = 0;
    mcpd::addNVSTools(*s);
    call(s, "nvs_set", R"({"key":"x","value":"y"})");
    String resp = call(s, "nvs_status", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "used_entries\\\":1");
    ASSERT_STR_CONTAINS(resp.c_str(), "emulated");
}

TEST(nvs_key_too_long) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_set", R"({"key":"1234567890123456","value":"x"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "too long");
}

TEST(nvs_set_bool_and_float) {
    auto* s = makeServer();
    mcpd::tools::NVSTool::store().clear();
    mcpd::addNVSTools(*s);
    String resp = call(s, "nvs_set", R"({"key":"flag","value":true})");
    ASSERT_STR_CONTAINS(resp.c_str(), "bool");
    resp = call(s, "nvs_set", R"({"key":"temp","value":23.5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "float");
}

// ── Relay Tool Tests ───────────────────────────────────────────────────

TEST(relay_tools_register) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");

    ASSERT_STR_CONTAINS(resp.c_str(), "relay_set");
    ASSERT_STR_CONTAINS(resp.c_str(), "relay_get");
    ASSERT_STR_CONTAINS(resp.c_str(), "relay_toggle");
    ASSERT_STR_CONTAINS(resp.c_str(), "relay_status");
}

TEST(relay_add_channel_and_set) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    mcpd::addRelayChannel(2, "light", true);
    String resp = call(s, "relay_set", R"({"channel":0,"state":true})");
    ASSERT_STR_CONTAINS(resp.c_str(), "light");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
}

TEST(relay_toggle) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    mcpd::addRelayChannel(5, "fan", true);
    call(s, "relay_set", R"({"channel":0,"state":false})");
    String resp = call(s, "relay_toggle", R"({"channel":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
}

TEST(relay_get_state) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    mcpd::addRelayChannel(3, "pump", true);
    String resp = call(s, "relay_get", R"({"channel":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pump");
    ASSERT_STR_CONTAINS(resp.c_str(), "OFF"); // starts off
}

TEST(relay_invalid_channel) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    String resp = call(s, "relay_set", R"({"channel":99,"state":true})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(relay_status_overview) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    mcpd::addRelayChannel(2, "heater", true);
    mcpd::addRelayChannel(3, "cooler", true);
    String resp = call(s, "relay_status", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "heater");
    ASSERT_STR_CONTAINS(resp.c_str(), "cooler");
    ASSERT_STR_CONTAINS(resp.c_str(), "total_channels\\\":2");
}

TEST(relay_all_off) {
    auto* s = makeServer();
    mcpd::tools::RelayTool::channels().clear();
    mcpd::addRelayTools(*s);
    mcpd::addRelayChannel(2, "a", true);
    mcpd::addRelayChannel(3, "b", true);
    call(s, "relay_set", R"({"channel":0,"state":true})");
    call(s, "relay_set", R"({"channel":1,"state":true})");
    String resp = call(s, "relay_all_off", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "turned_off");
}

// ── UART Tool Tests ────────────────────────────────────────────────────

TEST(uart_tools_register) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");

    ASSERT_STR_CONTAINS(resp.c_str(), "uart_config");
    ASSERT_STR_CONTAINS(resp.c_str(), "uart_write");
    ASSERT_STR_CONTAINS(resp.c_str(), "uart_read");
    ASSERT_STR_CONTAINS(resp.c_str(), "uart_available");
}

TEST(uart_config_port1) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_config", R"({"port":1,"baud":9600})");
    ASSERT_STR_CONTAINS(resp.c_str(), "configured\\\":true");
}

TEST(uart_config_with_pins) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_config", R"({"port":1,"baud":115200,"rxPin":16,"txPin":17})");
    ASSERT_STR_CONTAINS(resp.c_str(), "configured\\\":true");
    ASSERT_STR_CONTAINS(resp.c_str(), "115200");
}

TEST(uart_config_invalid_port) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_config", R"({"port":5,"baud":9600})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(uart_write_data) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_write", R"({"port":1,"data":"hello"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "bytesWritten");
}

TEST(uart_write_hex) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_write", R"({"port":1,"hex":"FF01A0"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "bytesWritten\\\":3");
}

TEST(uart_write_no_data) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_write", R"({"port":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(uart_available) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    String resp = call(s, "uart_available", R"({"port":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "available");
}

TEST(uart_read_empty) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    Serial1._setRxBuffer("");
    String resp = call(s, "uart_read", R"({"port":1,"timeout":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "bytesRead\\\":0");
}

TEST(uart_read_with_data) {
    auto* s = makeServer();
    mcpd::tools::UARTTool::attach(*s);
    Serial1._setRxBuffer("ABC");
    String resp = call(s, "uart_read", R"({"port":1,"maxBytes":10,"timeout":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "bytesRead\\\":3");
    ASSERT_STR_CONTAINS(resp.c_str(), "ABC");
}

// ── Timer Tool Tests ───────────────────────────────────────────────────

TEST(timer_tools_register) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");

    ASSERT_STR_CONTAINS(resp.c_str(), "timer_start");
    ASSERT_STR_CONTAINS(resp.c_str(), "timer_stop");
    ASSERT_STR_CONTAINS(resp.c_str(), "timer_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "timer_millis");
    ASSERT_STR_CONTAINS(resp.c_str(), "timer_pulse_in");
}

TEST(timer_start_and_status) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    String resp = call(s, "timer_start", R"({"timer_id":0,"interval_us":1000})");
    ASSERT_STR_CONTAINS(resp.c_str(), "started");
    resp = call(s, "timer_status", R"({"timer_id":0})");
    ASSERT_STR_CONTAINS(resp.c_str(), "active\\\":true");
}

TEST(timer_stop) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    call(s, "timer_start", R"({"timer_id":1,"interval_us":500})");
    String resp = call(s, "timer_stop", R"({"timer_id":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "stopped");
}

TEST(timer_invalid_id) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    String resp = call(s, "timer_start", R"({"timer_id":5,"interval_us":1000})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(timer_millis_returns_timestamps) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    String resp = call(s, "timer_millis", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "millis");
    ASSERT_STR_CONTAINS(resp.c_str(), "micros");
}

TEST(timer_pulse_in) {
    auto* s = makeServer();
    mcpd::registerTimerTools(*s);
    String resp = call(s, "timer_pulse_in", R"({"pin":12,"level":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "pulse_us");
    // pulseIn mock returns 500, so HC-SR04 distance should be there
    ASSERT_STR_CONTAINS(resp.c_str(), "hc_sr04_cm");
}

// ── Analog Watch Tool Tests ────────────────────────────────────────────

TEST(analog_watch_tools_register) {
    auto* s = makeServer();
    mcpd::addAnalogWatchTools(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");

    ASSERT_STR_CONTAINS(resp.c_str(), "analog_watch_set");
    ASSERT_STR_CONTAINS(resp.c_str(), "analog_watch_status");
    ASSERT_STR_CONTAINS(resp.c_str(), "analog_watch_clear");
}

// ── EEPROM Tool Tests ──────────────────────────────────────────────────

#include "../../src/tools/MCPEEPROMTool.h"

TEST(eeprom_tools_register) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_read");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_write");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_read_int");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_write_int");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_read_string");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_write_string");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_clear");
    ASSERT_STR_CONTAINS(resp.c_str(), "eeprom_info");
}

TEST(eeprom_write_and_read) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_write", R"({"address":0,"bytes":[72,101,108,108,111]})");
    ASSERT_STR_CONTAINS(resp.c_str(), "written");
    ASSERT_STR_CONTAINS(resp.c_str(), ":5");

    resp = call(s, "eeprom_read", R"({"address":0,"length":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "72,101,108,108,111");
}

TEST(eeprom_read_out_of_range) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_read", R"({"address":5000,"length":1})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
    ASSERT_STR_CONTAINS(resp.c_str(), "out of range");
}

TEST(eeprom_write_int_and_read_int) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_write_int", R"({"address":100,"value":42})");
    ASSERT_STR_CONTAINS(resp.c_str(), "value");
    ASSERT_STR_CONTAINS(resp.c_str(), ":42");

    resp = call(s, "eeprom_read_int", R"({"address":100})");
    ASSERT_STR_CONTAINS(resp.c_str(), ":42");
}

TEST(eeprom_write_int_negative) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_write_int", R"({"address":200,"value":-12345})");
    ASSERT_STR_CONTAINS(resp.c_str(), ":-12345");

    resp = call(s, "eeprom_read_int", R"({"address":200})");
    ASSERT_STR_CONTAINS(resp.c_str(), ":-12345");
}

TEST(eeprom_write_string_and_read_string) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_write_string", R"({"address":300,"value":"Hello EEPROM"})");
    ASSERT_STR_CONTAINS(resp.c_str(), "written");
    ASSERT_STR_CONTAINS(resp.c_str(), ":13");

    resp = call(s, "eeprom_read_string", R"({"address":300})");
    ASSERT_STR_CONTAINS(resp.c_str(), "Hello EEPROM");
    ASSERT_STR_CONTAINS(resp.c_str(), "length");
}

TEST(eeprom_clear) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    call(s, "eeprom_write", R"({"address":0,"bytes":[1,2,3,4,5]})");
    String resp = call(s, "eeprom_clear", R"({"address":0,"length":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "cleared");
    ASSERT_STR_CONTAINS(resp.c_str(), ":5");
    resp = call(s, "eeprom_read", R"({"address":0,"length":5})");
    ASSERT_STR_CONTAINS(resp.c_str(), "0,0,0,0,0");
}

TEST(eeprom_info) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    call(s, "eeprom_write", R"({"address":0,"bytes":[1,2,3]})");
    String resp = call(s, "eeprom_info", "{}");
    ASSERT_STR_CONTAINS(resp.c_str(), "size");
    ASSERT_STR_CONTAINS(resp.c_str(), "4096");
    ASSERT_STR_CONTAINS(resp.c_str(), "used");
}

TEST(eeprom_write_exceeds_size) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_write", R"({"address":4090,"bytes":[1,2,3,4,5,6,7,8]})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
    ASSERT_STR_CONTAINS(resp.c_str(), "exceed");
}

TEST(eeprom_read_int_out_of_range) {
    auto* s = makeServer();
    mcpd::addEEPROMTools(*s);
    String resp = call(s, "eeprom_read_int", R"({"address":4094})");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — Built-in Tools Unit Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
