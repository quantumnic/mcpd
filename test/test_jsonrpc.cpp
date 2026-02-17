/**
 * mcpd — JSON-RPC Layer Unit Tests
 *
 * Tests the JSON-RPC processing logic: parsing, dispatch, error handling.
 * Runs on the host (not on MCU) using Arduino API mocks.
 *
 * Compile: g++ -std=c++17 -I../src -I. -DMCPD_TEST test_jsonrpc.cpp -o test_jsonrpc && ./test_jsonrpc
 */

// mock_includes/ provides Arduino.h, WiFi.h, etc. that redirect to our mocks
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ── Helper: create a server with a test tool ───────────────────────────

static Server* makeTestServer() {
    static Server* s = nullptr;
    if (s) { delete s; }
    s = new Server("test-server", 8080);

    s->addTool("echo", "Echoes back the input",
        R"({"type":"object","properties":{"message":{"type":"string"}},"required":["message"]})",
        [](const JsonObject& args) -> String {
            const char* msg = args["message"].as<const char*>();
            return String("{\"echo\":\"") + (msg ? msg : "") + "\"}";
        });

    s->addTool("add", "Add two numbers",
        R"({"type":"object","properties":{"a":{"type":"integer"},"b":{"type":"integer"}},"required":["a","b"]})",
        [](const JsonObject& args) -> String {
            int a = args["a"];
            int b = args["b"];
            return String("{\"result\":") + String(a + b) + "}";
        });

    s->addResource("test://data", "Test Data", "Test resource", "application/json",
        []() -> String { return "{\"value\":42}"; });

    s->addPrompt("greet", "Generate a greeting",
        {
            MCPPromptArgument("name", "Person to greet", true),
            MCPPromptArgument("style", "Greeting style", false)
        },
        [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
            String name = args.at("name");
            String style = "friendly";
            auto it = args.find("style");
            if (it != args.end()) style = it->second;
            String msg = String("Please greet ") + name + " in a " + style + " way.";
            return { MCPPromptMessage("user", msg.c_str()) };
        });

    return s;
}

// ── Tests ──────────────────────────────────────────────────────────────

TEST(initialize_returns_protocol_version) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"protocolVersion\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "2025-03-26");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":1");
}

TEST(initialize_returns_server_info) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"name\":\"test-server\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"version\":\"" MCPD_VERSION "\"");
}

TEST(initialize_advertises_capabilities) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"tools\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"resources\"");
}

TEST(ping_returns_empty_result) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":2,"method":"ping"})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":2");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
}

TEST(tools_list_returns_all_tools) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":3,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"echo\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"add\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"inputSchema\"");
}

TEST(tools_call_echo) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"echo","arguments":{"message":"hello"}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "echo");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"type\":\"text\"");
}

TEST(tools_call_add) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"add","arguments":{"a":3,"b":7}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "result");
}

TEST(tools_call_not_found) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"nonexistent","arguments":{}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Tool not found");
}

TEST(tools_call_missing_name) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"arguments":{}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing tool name");
}

TEST(resources_list) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":8,"method":"resources/list","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "test://data");
    ASSERT_STR_CONTAINS(resp.c_str(), "Test Data");
}

TEST(resources_read) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":9,"method":"resources/read","params":{"uri":"test://data"}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "test://data");
    ASSERT_STR_CONTAINS(resp.c_str(), "application/json");
}

TEST(resources_read_not_found) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":10,"method":"resources/read","params":{"uri":"test://nope"}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Resource not found");
}

TEST(method_not_found) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":11,"method":"bogus/method","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Method not found");
    ASSERT_STR_CONTAINS(resp.c_str(), "-32601");
}

TEST(invalid_json) {
    auto* s = makeTestServer();
    String req = "not json at all";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "-32700");
}

TEST(missing_jsonrpc_version) {
    auto* s = makeTestServer();
    String req = R"({"id":12,"method":"ping"})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "jsonrpc version");
}

TEST(wrong_jsonrpc_version) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"1.0","id":13,"method":"ping"})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
}

TEST(missing_method) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":14})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "missing method");
}

TEST(notification_returns_empty) {
    auto* s = makeTestServer();
    // No "id" field = notification
    String req = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
    String resp = s->_processJsonRpc(req);

    ASSERT_EQ(resp.length(), (size_t)0);
}

TEST(batch_request) {
    auto* s = makeTestServer();
    String req = R"([
        {"jsonrpc":"2.0","id":20,"method":"ping"},
        {"jsonrpc":"2.0","id":21,"method":"ping"}
    ])";
    String resp = s->_processJsonRpc(req);

    // Should be a JSON array
    ASSERT(resp.c_str()[0] == '[');
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":20");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":21");
}

TEST(initialize_advertises_prompts_capability) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"prompts\"");
}

TEST(prompts_list_returns_all_prompts) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":30,"method":"prompts/list","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"greet\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"name\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"required\":true");
}

TEST(prompts_get_returns_messages) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":31,"method":"prompts/get","params":{"name":"greet","arguments":{"name":"World"}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"role\":\"user\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "World");
    ASSERT_STR_CONTAINS(resp.c_str(), "friendly");
}

TEST(prompts_get_with_optional_arg) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":32,"method":"prompts/get","params":{"name":"greet","arguments":{"name":"Alice","style":"formal"}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "Alice");
    ASSERT_STR_CONTAINS(resp.c_str(), "formal");
}

TEST(prompts_get_missing_required_arg) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":33,"method":"prompts/get","params":{"name":"greet","arguments":{}}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing required argument");
}

TEST(prompts_get_not_found) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":34,"method":"prompts/get","params":{"name":"nonexistent"}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Prompt not found");
}

TEST(empty_body_parse_error) {
    auto* s = makeTestServer();
    String req = "";
    // Empty body would be caught at HTTP layer, but processJsonRpc handles it too
    String resp = s->_processJsonRpc(req);
    // ArduinoJson will fail to parse empty string
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — JSON-RPC Unit Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    // Tests run via static initialization above

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
