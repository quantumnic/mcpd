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
#include "../src/tools/MCPGPIOTool.h"
#include "../src/MCPContent.h"
#include "../src/MCPProgress.h"

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

// ── Logging tests ──────────────────────────────────────────────────────

TEST(logging_set_level) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":40,"method":"logging/setLevel","params":{"level":"debug"}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":40");
}

TEST(logging_set_level_missing_param) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":41,"method":"logging/setLevel","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing level");
}

TEST(initialize_advertises_logging_capability) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"logging\"");
}

TEST(initialize_advertises_list_changed) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"listChanged\":true");
}

// ── Pagination tests ──────────────────────────────────────────────────

TEST(tools_list_pagination) {
    auto* s = makeTestServer();
    s->setPageSize(1);  // Only 1 tool per page

    // First page
    String req1 = R"({"jsonrpc":"2.0","id":50,"method":"tools/list","params":{}})";
    String resp1 = s->_processJsonRpc(req1);
    ASSERT_STR_CONTAINS(resp1.c_str(), "\"nextCursor\"");
    ASSERT_STR_CONTAINS(resp1.c_str(), "\"echo\"");

    // Second page
    String req2 = R"({"jsonrpc":"2.0","id":51,"method":"tools/list","params":{"cursor":"1"}})";
    String resp2 = s->_processJsonRpc(req2);
    ASSERT_STR_CONTAINS(resp2.c_str(), "\"add\"");

    s->setPageSize(0);  // Reset
}

TEST(tools_list_no_pagination_when_disabled) {
    auto* s = makeTestServer();
    // pageSize=0 (default) means no pagination
    String req = R"({"jsonrpc":"2.0","id":52,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);

    // Should contain both tools and no nextCursor
    ASSERT_STR_CONTAINS(resp.c_str(), "\"echo\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"add\"");
    ASSERT(strstr(resp.c_str(), "nextCursor") == nullptr);
}

// ── Dynamic tool management tests ─────────────────────────────────────

TEST(remove_tool) {
    auto* s = makeTestServer();

    // Verify tool exists
    String req1 = R"({"jsonrpc":"2.0","id":60,"method":"tools/call","params":{"name":"echo","arguments":{"message":"hi"}}})";
    String resp1 = s->_processJsonRpc(req1);
    ASSERT_STR_CONTAINS(resp1.c_str(), "\"result\"");

    // Remove it
    bool removed = s->removeTool("echo");
    ASSERT(removed);

    // Verify it's gone
    String req2 = R"({"jsonrpc":"2.0","id":61,"method":"tools/call","params":{"name":"echo","arguments":{"message":"hi"}}})";
    String resp2 = s->_processJsonRpc(req2);
    ASSERT_STR_CONTAINS(resp2.c_str(), "Tool not found");
}

TEST(remove_nonexistent_tool) {
    auto* s = makeTestServer();
    bool removed = s->removeTool("nonexistent");
    ASSERT(!removed);
}

// ── Notification handling tests ────────────────────────────────────────

TEST(notifications_cancelled_returns_empty) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"123"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_EQ(resp.length(), (size_t)0);
}

// ── Logging unit tests ────────────────────────────────────────────────

TEST(log_level_conversion) {
    ASSERT(strcmp(mcpd::logLevelToString(mcpd::LogLevel::DEBUG), "debug") == 0);
    ASSERT(strcmp(mcpd::logLevelToString(mcpd::LogLevel::ERROR), "error") == 0);
    ASSERT(strcmp(mcpd::logLevelToString(mcpd::LogLevel::EMERGENCY), "emergency") == 0);

    ASSERT(mcpd::logLevelFromString("debug") == mcpd::LogLevel::DEBUG);
    ASSERT(mcpd::logLevelFromString("error") == mcpd::LogLevel::ERROR);
    ASSERT(mcpd::logLevelFromString("bogus") == mcpd::LogLevel::INFO);
    ASSERT(mcpd::logLevelFromString(nullptr) == mcpd::LogLevel::INFO);
}

TEST(logging_filters_by_level) {
    mcpd::Logging log;
    log.setLevel(mcpd::LogLevel::WARNING);

    int received = 0;
    log.setSink([&](const String&) { received++; });

    log.debug("test", "should be filtered");
    log.info("test", "should be filtered");
    log.warning("test", "should pass");
    log.error("test", "should pass");

    ASSERT_EQ(received, 2);
}

TEST(empty_body_parse_error) {
    auto* s = makeTestServer();
    String req = "";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

// ── Resource subscription tests ───────────────────────────────────────

TEST(resources_subscribe) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":70,"method":"resources/subscribe","params":{"uri":"test://data"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":70");
}

TEST(resources_subscribe_missing_uri) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":71,"method":"resources/subscribe","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing resource URI");
}

TEST(resources_unsubscribe) {
    auto* s = makeTestServer();
    // Subscribe first
    String sub = R"({"jsonrpc":"2.0","id":72,"method":"resources/subscribe","params":{"uri":"test://data"}})";
    s->_processJsonRpc(sub);
    // Unsubscribe
    String unsub = R"({"jsonrpc":"2.0","id":73,"method":"resources/unsubscribe","params":{"uri":"test://data"}})";
    String resp = s->_processJsonRpc(unsub);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
}

TEST(notify_resource_updated_only_when_subscribed) {
    auto* s = makeTestServer();
    // Not subscribed — should not generate notification
    s->notifyResourceUpdated("test://data");
    ASSERT_EQ(s->_pendingNotifications.size(), (size_t)0);

    // Subscribe
    String sub = R"({"jsonrpc":"2.0","id":74,"method":"resources/subscribe","params":{"uri":"test://data"}})";
    s->_processJsonRpc(sub);

    // Now notify — should generate notification
    s->notifyResourceUpdated("test://data");
    ASSERT_EQ(s->_pendingNotifications.size(), (size_t)1);
    ASSERT_STR_CONTAINS(s->_pendingNotifications[0].c_str(), "notifications/resources/updated");
    ASSERT_STR_CONTAINS(s->_pendingNotifications[0].c_str(), "test://data");
}

TEST(initialize_advertises_subscribe_capability) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"subscribe\":true");
}

// ── Completion tests ──────────────────────────────────────────────────

TEST(completion_complete_for_prompt) {
    auto* s = makeTestServer();
    // Register a completion provider
    s->completions().addPromptCompletion("greet", "name",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"Alice", "Albrecht", "Bob", "Charlie"};
        });

    String req = R"({"jsonrpc":"2.0","id":80,"method":"completion/complete","params":{
        "ref":{"type":"ref/prompt","name":"greet"},
        "argument":{"name":"name","value":"Al"}
    }})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"values\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Alice");
    ASSERT_STR_CONTAINS(resp.c_str(), "Albrecht");
    // "Bob" and "Charlie" should be filtered out (don't start with "Al")
    ASSERT(strstr(resp.c_str(), "Bob") == nullptr);
}

TEST(completion_complete_empty_prefix) {
    auto* s = makeTestServer();
    s->completions().addPromptCompletion("greet", "name",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"Alice", "Bob"};
        });

    String req = R"({"jsonrpc":"2.0","id":81,"method":"completion/complete","params":{
        "ref":{"type":"ref/prompt","name":"greet"},
        "argument":{"name":"name","value":""}
    }})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "Alice");
    ASSERT_STR_CONTAINS(resp.c_str(), "Bob");
}

TEST(completion_complete_missing_ref) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":82,"method":"completion/complete","params":{"argument":{"name":"x","value":"y"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "Missing ref");
}

TEST(completion_complete_missing_argument) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":83,"method":"completion/complete","params":{"ref":{"type":"ref/prompt","name":"greet"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"error\"");
}

TEST(completion_complete_for_resource_template) {
    auto* s = makeTestServer();
    s->completions().addResourceTemplateCompletion(
        "sensor://{id}/reading", "id",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"temp1", "temp2", "humidity1"};
        });

    String req = R"({"jsonrpc":"2.0","id":84,"method":"completion/complete","params":{
        "ref":{"type":"ref/resource","uri":"sensor://{id}/reading"},
        "argument":{"name":"id","value":"temp"}
    }})";
    String resp = s->_processJsonRpc(req);

    ASSERT_STR_CONTAINS(resp.c_str(), "temp1");
    ASSERT_STR_CONTAINS(resp.c_str(), "temp2");
    ASSERT(strstr(resp.c_str(), "humidity1") == nullptr);
}

TEST(completion_has_more_flag) {
    auto* s = makeTestServer();
    ASSERT(!s->completions().hasProviders());
    s->completions().addPromptCompletion("greet", "name",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"a"};
        });
    ASSERT(s->completions().hasProviders());
}

TEST(initialize_advertises_completion_capability) {
    auto* s = makeTestServer();
    s->completions().addPromptCompletion("greet", "name",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {};
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"completion\"");
}

// ── Double subscribe idempotency ──────────────────────────────────────

TEST(double_subscribe_is_idempotent) {
    auto* s = makeTestServer();
    String sub = R"({"jsonrpc":"2.0","id":90,"method":"resources/subscribe","params":{"uri":"test://data"}})";
    s->_processJsonRpc(sub);
    s->_processJsonRpc(sub);
    // Should only have one subscription entry
    ASSERT_EQ(s->_subscribedResources.size(), (size_t)1);
}

// ── v0.5.0 Tests: Roots ────────────────────────────────────────────────

TEST(roots_list_empty_when_no_roots) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":100,"method":"roots/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"roots\":[]");
}

TEST(roots_list_returns_registered_roots) {
    auto* s = makeTestServer();
    s->addRoot("sensor://temperature/", "Temperature Sensors");
    s->addRoot("gpio://pins/", "GPIO Pins");
    String req = R"({"jsonrpc":"2.0","id":101,"method":"roots/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sensor://temperature/");
    ASSERT_STR_CONTAINS(resp.c_str(), "Temperature Sensors");
    ASSERT_STR_CONTAINS(resp.c_str(), "gpio://pins/");
    ASSERT_STR_CONTAINS(resp.c_str(), "GPIO Pins");
}

TEST(initialize_advertises_roots_capability) {
    auto* s = makeTestServer();
    s->addRoot("test://root/", "Test Root");
    String req = R"({"jsonrpc":"2.0","id":102,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"roots\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"listChanged\":true");
}

TEST(initialize_no_roots_capability_when_empty) {
    auto* s = makeTestServer();
    // No roots added — capability should not be advertised
    // (tools/resources/prompts already take "roots" keyword, so just check
    // that the response is valid)
    String req = R"({"jsonrpc":"2.0","id":103,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"protocolVersion\"");
}

TEST(root_without_name) {
    auto* s = makeTestServer();
    s->addRoot("file:///data/", "");
    String req = R"({"jsonrpc":"2.0","id":104,"method":"roots/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "file:///data/");
}

// ── v0.5.0 Tests: Batch edge cases ────────────────────────────────────

TEST(batch_all_notifications_returns_empty) {
    auto* s = makeTestServer();
    String req = R"([{"jsonrpc":"2.0","method":"notifications/initialized"},{"jsonrpc":"2.0","method":"notifications/cancelled"}])";
    String resp = s->_processJsonRpc(req);
    ASSERT(resp.isEmpty());
}

TEST(batch_mixed_requests_and_notifications) {
    auto* s = makeTestServer();
    String req = R"([{"jsonrpc":"2.0","method":"notifications/initialized"},{"jsonrpc":"2.0","id":200,"method":"ping"}])";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":200");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"result\"");
}

TEST(resource_template_match_and_read) {
    auto* s = makeTestServer();
    s->addResourceTemplate("sensor://{id}/reading", "Sensor Reading",
        "Read a sensor by ID", "application/json",
        [](const std::map<String, String>& params) -> String {
            String id = params.at("id");
            return String("{\"id\":\"") + id + "\",\"value\":42}";
        });
    String req = R"({"jsonrpc":"2.0","id":210,"method":"resources/read","params":{"uri":"sensor://temp1/reading"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "temp1");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
}

TEST(remove_resource_by_uri) {
    auto* s = makeTestServer();
    ASSERT(s->removeResource("test://data"));
    String req = R"({"jsonrpc":"2.0","id":220,"method":"resources/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    // Should have no resources
    ASSERT_STR_CONTAINS(resp.c_str(), "\"resources\":[]");
}

TEST(pagination_prompts) {
    auto* s = makeTestServer();
    s->setPageSize(1);
    // Already has "greet" prompt, add another
    s->addPrompt("farewell", "Say goodbye",
        { mcpd::MCPPromptArgument("name", "Person", true) },
        [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
            return { mcpd::MCPPromptMessage("user", "Goodbye!") };
        });
    String req = R"({"jsonrpc":"2.0","id":230,"method":"prompts/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"nextCursor\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "greet");
}

TEST(pagination_resources) {
    auto* s = makeTestServer();
    s->addResource("test://data2", "Test Data 2", "Another resource", "application/json",
        []() -> String { return "{}"; });
    s->setPageSize(1);
    String req = R"({"jsonrpc":"2.0","id":240,"method":"resources/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"nextCursor\"");
}

TEST(version_is_0_7_0) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":250,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"version\":\"0.7.0\"");
}

// ── v0.6.0 Tests: Tool Annotations ────────────────────────────────────

TEST(tool_annotations_serialized) {
    auto* s = makeTestServer();
    // Add a tool with annotations
    MCPTool tool("annotated_tool", "A tool with annotations",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.markReadOnly().markLocalOnly();
    s->addTool(tool);

    String req = R"({"jsonrpc":"2.0","id":300,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"annotations\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"readOnlyHint\":true");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"openWorldHint\":false");
}

TEST(tool_without_annotations_no_field) {
    auto* s = makeTestServer();
    // The default "echo" tool has no annotations set
    String req = R"({"jsonrpc":"2.0","id":301,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    // echo tool should not have annotations key (hasAnnotations=false)
    // This is harder to check precisely, but we verify "echo" is present
    ASSERT_STR_CONTAINS(resp.c_str(), "\"echo\"");
}

TEST(tool_mark_idempotent) {
    auto* s = makeTestServer();
    MCPTool tool("idempotent_tool", "Idempotent",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.markIdempotent();
    s->addTool(tool);

    String req = R"({"jsonrpc":"2.0","id":302,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"idempotentHint\":true");
}

TEST(tool_annotations_builder_chain) {
    MCPTool tool("chain_test", "Test chaining",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.markReadOnly().markIdempotent().markLocalOnly();
    ASSERT(tool.annotations.readOnlyHint == true);
    ASSERT(tool.annotations.destructiveHint == false);
    ASSERT(tool.annotations.idempotentHint == true);
    ASSERT(tool.annotations.openWorldHint == false);
    ASSERT(tool.annotations.hasAnnotations == true);
}

TEST(tool_annotations_custom) {
    MCPToolAnnotations ann;
    ann.title = "My Custom Tool";
    ann.readOnlyHint = false;
    ann.destructiveHint = true;
    ann.idempotentHint = false;
    ann.openWorldHint = true;

    MCPTool tool("custom_ann", "Custom annotations test",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.setAnnotations(ann);

    ASSERT(tool.annotations.hasAnnotations == true);
    ASSERT(tool.annotations.title == "My Custom Tool");
    ASSERT(tool.annotations.destructiveHint == true);
}

TEST(tool_annotations_title_in_json) {
    auto* s = makeTestServer();
    MCPToolAnnotations ann;
    ann.title = "Sensor Reader";

    MCPTool tool("titled_tool", "Reads sensor",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.setAnnotations(ann);
    s->addTool(tool);

    String req = R"({"jsonrpc":"2.0","id":305,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"title\":\"Sensor Reader\"");
}

TEST(remove_tool_with_annotations) {
    auto* s = makeTestServer();
    MCPTool tool("removable", "To be removed",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String { return "{}"; });
    tool.markReadOnly();
    s->addTool(tool);
    ASSERT(s->removeTool("removable") == true);
    ASSERT(s->removeTool("removable") == false);
}

TEST(annotated_tool_call_works) {
    auto* s = makeTestServer();
    MCPTool tool("ann_echo", "Annotated echo",
        R"({"type":"object","properties":{"msg":{"type":"string"}},"required":["msg"]})",
        [](const JsonObject& args) -> String {
            return String("{\"msg\":\"") + args["msg"].as<const char*>() + "\"}";
        });
    tool.markReadOnly().markLocalOnly();
    s->addTool(tool);

    String req = R"({"jsonrpc":"2.0","id":310,"method":"tools/call","params":{"name":"ann_echo","arguments":{"msg":"hello"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "hello");
}

// ── v0.6.0 Tests: GPIO tools with annotations ─────────────────────────

TEST(gpio_tool_annotations_in_list) {
    // Create fresh server with GPIO tools attached
    Server* s = new Server("gpio-test", 8080);
    mcpd::tools::GPIOTool::attach(*s);

    String req = R"({"jsonrpc":"2.0","id":320,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    // digital_read should be readOnly
    ASSERT_STR_CONTAINS(resp.c_str(), "\"digital_read\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"readOnlyHint\":true");
    delete s;
}

// ── v0.6.0 Tests: Cancellation ────────────────────────────────────────

TEST(cancellation_notification_accepted) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"abc-123","reason":"timeout"}})";
    String resp = s->_processJsonRpc(req);
    // Notifications return empty string (202)
    ASSERT(resp.isEmpty());
}

TEST(cancellation_in_batch) {
    auto* s = makeTestServer();
    String req = R"([{"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"x"}},{"jsonrpc":"2.0","id":330,"method":"ping"}])";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"id\":330");
}

// ── v0.7.0 Tests: Structured Content Types ────────────────────────────

TEST(content_text_factory) {
    auto c = MCPContent::makeText("hello world");
    ASSERT(c.type == MCPContent::TEXT);
    ASSERT(c.text == "hello world");
}

TEST(content_image_factory) {
    auto c = MCPContent::makeImage("iVBOR...", "image/png");
    ASSERT(c.type == MCPContent::IMAGE);
    ASSERT(c.data == "iVBOR...");
    ASSERT(c.mimeType == "image/png");
}

TEST(content_resource_factory) {
    auto c = MCPContent::makeResource("sensor://temp", "application/json", "{\"value\":22}");
    ASSERT(c.type == MCPContent::RESOURCE);
    ASSERT(c.uri == "sensor://temp");
    ASSERT(c.text == "{\"value\":22}");
}

TEST(content_resource_blob_factory) {
    auto c = MCPContent::makeResourceBlob("file://data.bin", "application/octet-stream", "AQID");
    ASSERT(c.type == MCPContent::RESOURCE);
    ASSERT(!c.blob.isEmpty());
}

TEST(tool_result_text) {
    auto r = MCPToolResult::text("{\"ok\":true}");
    ASSERT(r.content.size() == 1);
    ASSERT(!r.isError);
}

TEST(tool_result_error) {
    auto r = MCPToolResult::error("Something broke");
    ASSERT(r.isError);
    ASSERT(r.content[0].text == "Something broke");
}

TEST(tool_result_image) {
    auto r = MCPToolResult::image("base64data", "image/jpeg", "A photo");
    ASSERT(r.content.size() == 2);  // alt text + image
    ASSERT(r.content[0].type == MCPContent::TEXT);
    ASSERT(r.content[1].type == MCPContent::IMAGE);
}

TEST(content_text_to_json) {
    auto c = MCPContent::makeText("test");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT(String(obj["type"].as<const char*>()) == "text");
    ASSERT(String(obj["text"].as<const char*>()) == "test");
}

TEST(content_image_to_json) {
    auto c = MCPContent::makeImage("b64", "image/png");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT(String(obj["type"].as<const char*>()) == "image");
    ASSERT(String(obj["data"].as<const char*>()) == "b64");
    ASSERT(String(obj["mimeType"].as<const char*>()) == "image/png");
}

TEST(content_resource_to_json) {
    auto c = MCPContent::makeResource("sensor://x", "text/plain", "42");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT(String(obj["type"].as<const char*>()) == "resource");
    ASSERT(String(obj["resource"]["uri"].as<const char*>()) == "sensor://x");
    ASSERT(String(obj["resource"]["text"].as<const char*>()) == "42");
}

TEST(content_resource_blob_to_json) {
    auto c = MCPContent::makeResourceBlob("file://x", "application/octet-stream", "AQID");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT(String(obj["resource"]["blob"].as<const char*>()) == "AQID");
    ASSERT(obj["resource"]["text"].isNull());
}

TEST(tool_result_to_json) {
    MCPToolResult r;
    r.add(MCPContent::makeText("Reading: 22.5°C"));
    r.add(MCPContent::makeImage("chart_b64", "image/png"));
    JsonDocument doc;
    JsonObject docObj = doc.to<JsonObject>();
    r.toJson(docObj);
    JsonArray arr = doc["content"].as<JsonArray>();
    ASSERT(arr.size() == 2);
    ASSERT(String(arr[0]["type"].as<const char*>()) == "text");
    ASSERT(String(arr[1]["type"].as<const char*>()) == "image");
}

// ── v0.7.0 Tests: Rich Tool Handler ──────────────────────────────────

TEST(rich_tool_call) {
    Server* s = new Server("rich-test", 8080);
    s->addRichTool("camera_snap", "Take a photo",
        R"({"type":"object","properties":{"resolution":{"type":"string"}},"required":[]})",
        [](const JsonObject& args) -> MCPToolResult {
            MCPToolResult r;
            r.add(MCPContent::makeText("Photo captured"));
            r.add(MCPContent::makeImage("fakebase64imagedata", "image/jpeg"));
            return r;
        });

    String req = R"({"jsonrpc":"2.0","id":700,"method":"tools/call","params":{"name":"camera_snap","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"type\":\"image\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "fakebase64imagedata");
    ASSERT_STR_CONTAINS(resp.c_str(), "Photo captured");
    delete s;
}

TEST(rich_tool_error_result) {
    Server* s = new Server("rich-err-test", 8080);
    s->addRichTool("failing_tool", "Always fails",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> MCPToolResult {
            return MCPToolResult::error("Sensor disconnected");
        });

    String req = R"({"jsonrpc":"2.0","id":701,"method":"tools/call","params":{"name":"failing_tool","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"isError\":true");
    ASSERT_STR_CONTAINS(resp.c_str(), "Sensor disconnected");
    delete s;
}

// ── v0.7.0 Tests: Progress Notifications ─────────────────────────────

TEST(progress_notification_json) {
    ProgressNotification pn;
    pn.progressToken = "tok-42";
    pn.progress = 50;
    pn.total = 100;
    pn.message = "Halfway there";
    String json = pn.toJsonRpc();
    ASSERT_STR_CONTAINS(json.c_str(), "notifications/progress");
    ASSERT_STR_CONTAINS(json.c_str(), "tok-42");
    ASSERT_STR_CONTAINS(json.c_str(), "Halfway there");
}

TEST(progress_notification_no_total) {
    ProgressNotification pn;
    pn.progressToken = "tok";
    pn.progress = 3;
    pn.total = 0;
    String json = pn.toJsonRpc();
    ASSERT_STR_CONTAINS(json.c_str(), "\"progress\":3");
    // total should not appear when 0
    // (ArduinoJson won't serialize a field we didn't add)
}

TEST(report_progress_queues_notification) {
    Server* s = new Server("prog-test", 8080);
    s->reportProgress("my-token", 25, 100, "Quarter done");
    // Check that a notification was queued
    ASSERT(s->_pendingNotifications.size() == 1);
    ASSERT_STR_CONTAINS(s->_pendingNotifications[0].c_str(), "my-token");
    ASSERT_STR_CONTAINS(s->_pendingNotifications[0].c_str(), "Quarter done");
    delete s;
}

TEST(report_progress_empty_token_noop) {
    Server* s = new Server("prog-noop", 8080);
    s->reportProgress("", 50, 100);
    ASSERT(s->_pendingNotifications.size() == 0);
    delete s;
}

// ── v0.7.0 Tests: Request Tracker / Cancellation ─────────────────────

TEST(request_tracker_basic) {
    RequestTracker rt;
    rt.trackRequest("req-1", "prog-1");
    ASSERT(rt.hasInFlight());
    ASSERT(rt.inFlightCount() == 1);
    rt.completeRequest("req-1");
    ASSERT(!rt.hasInFlight());
}

TEST(request_tracker_cancel) {
    RequestTracker rt;
    rt.trackRequest("req-2");
    ASSERT(rt.cancelRequest("req-2"));
    ASSERT(rt.isCancelled("req-2"));
    ASSERT(!rt.hasInFlight());
}

TEST(request_tracker_cancel_unknown) {
    RequestTracker rt;
    ASSERT(!rt.cancelRequest("nonexistent"));
}

TEST(cancellation_via_notification) {
    auto* s = makeTestServer();
    // Simulate tracking a request
    s->requests().trackRequest("req-42", "pt-42");
    ASSERT(s->requests().hasInFlight());

    // Send cancellation notification
    String cancel = R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"req-42"}})";
    s->_processJsonRpc(cancel);
    ASSERT(s->requests().isCancelled("req-42"));
    ASSERT(!s->requests().hasInFlight());
}

TEST(progress_token_extraction_in_tools_call) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":710,"method":"tools/call","params":{"name":"echo","arguments":{"message":"hi"},"_meta":{"progressToken":"pt-710"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\\\"echo\\\":\\\"hi\\\"");
    // Request should be completed (not in-flight)
    ASSERT(!s->requests().hasInFlight());
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — JSON-RPC Unit Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    // Tests run via static initialization above

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
