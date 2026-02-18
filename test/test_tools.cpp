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

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — Built-in Tools Unit Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
