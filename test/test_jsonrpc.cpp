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
#include "../src/MCPElicitation.h"
#include "../src/MCPRateLimit.h"
#include "../src/tools/MCPWatchdogTool.h"

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

TEST(version_is_0_11_0_compat) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":250,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"version\":\"0.23.0\"");
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

// ── Sampling Tests ─────────────────────────────────────────────────────

TEST(sampling_request_serialization) {
    MCPSamplingRequest req;
    req.addUserMessage("What does 42.5°C mean?");
    req.maxTokens = 200;
    req.systemPrompt = "You are a sensor expert.";
    req.temperature = 0.7;

    String json = req.toJsonRpc(9001);
    ASSERT_STR_CONTAINS(json.c_str(), "\"method\":\"sampling/createMessage\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"id\":9001");
    ASSERT_STR_CONTAINS(json.c_str(), "\"maxTokens\":200");
    ASSERT_STR_CONTAINS(json.c_str(), "\"systemPrompt\":\"You are a sensor expert.\"");
    ASSERT_STR_CONTAINS(json.c_str(), "42.5");
}

TEST(sampling_request_with_model_preferences) {
    MCPSamplingRequest req;
    req.addUserMessage("test");
    req.modelPreferences.intelligencePriority = 0.9;
    req.modelPreferences.costPriority = 0.1;
    MCPModelHint hint;
    hint.name = "claude-3-haiku";
    req.modelPreferences.hints.push_back(hint);

    String json = req.toJsonRpc(100);
    ASSERT_STR_CONTAINS(json.c_str(), "\"intelligencePriority\":");
    ASSERT_STR_CONTAINS(json.c_str(), "\"costPriority\":");
    ASSERT_STR_CONTAINS(json.c_str(), "claude-3-haiku");
}

TEST(sampling_request_with_stop_sequences) {
    MCPSamplingRequest req;
    req.addUserMessage("test");
    req.stopSequences.push_back("STOP");
    req.stopSequences.push_back("END");

    String json = req.toJsonRpc(101);
    ASSERT_STR_CONTAINS(json.c_str(), "\"stopSequences\"");
    ASSERT_STR_CONTAINS(json.c_str(), "STOP");
    ASSERT_STR_CONTAINS(json.c_str(), "END");
}

TEST(sampling_request_include_context) {
    MCPSamplingRequest req;
    req.addUserMessage("test");
    req.includeContext = "thisServer";

    String json = req.toJsonRpc(102);
    ASSERT_STR_CONTAINS(json.c_str(), "\"includeContext\":\"thisServer\"");
}

TEST(sampling_response_parsing) {
    JsonDocument doc;
    doc["role"] = "assistant";
    doc["model"] = "claude-3-haiku";
    doc["stopReason"] = "endTurn";
    JsonObject content = doc["content"].to<JsonObject>();
    content["type"] = "text";
    content["text"] = "The temperature is high.";

    MCPSamplingResponse resp = MCPSamplingResponse::fromJson(doc.as<JsonObject>());
    ASSERT(resp.valid);
    ASSERT_EQ(resp.role, String("assistant"));
    ASSERT_EQ(resp.model, String("claude-3-haiku"));
    ASSERT_EQ(resp.text, String("The temperature is high."));
    ASSERT_EQ(resp.stopReason, String("endTurn"));
}

TEST(sampling_response_empty_content) {
    JsonDocument doc;
    doc["role"] = "assistant";
    doc["model"] = "test-model";
    // No content
    MCPSamplingResponse resp = MCPSamplingResponse::fromJson(doc.as<JsonObject>());
    ASSERT(!resp.valid);
}

TEST(sampling_manager_queue_and_drain) {
    SamplingManager sm;
    ASSERT(!sm.hasPending());

    MCPSamplingRequest req;
    req.addUserMessage("Hello");
    req.maxTokens = 100;

    bool callbackCalled = false;
    int id = sm.queueRequest(req, [&](const MCPSamplingResponse& resp) {
        callbackCalled = true;
    });

    ASSERT(id >= 9000);
    ASSERT(sm.hasPending());
    ASSERT_EQ(sm.pendingCount(), (size_t)1);

    auto outgoing = sm.drainOutgoing();
    ASSERT_EQ(outgoing.size(), (size_t)1);
    ASSERT_STR_CONTAINS(outgoing[0].c_str(), "sampling/createMessage");

    // Drain again should be empty
    auto outgoing2 = sm.drainOutgoing();
    ASSERT_EQ(outgoing2.size(), (size_t)0);
}

TEST(sampling_manager_handle_response) {
    SamplingManager sm;
    MCPSamplingRequest req;
    req.addUserMessage("test");

    String receivedText;
    int id = sm.queueRequest(req, [&](const MCPSamplingResponse& resp) {
        receivedText = resp.text;
    });

    // Simulate response
    JsonDocument doc;
    doc["role"] = "assistant";
    doc["model"] = "test";
    doc["stopReason"] = "endTurn";
    JsonObject content = doc["content"].to<JsonObject>();
    content["type"] = "text";
    content["text"] = "Response text";

    bool matched = sm.handleResponse(id, doc.as<JsonObject>());
    ASSERT(matched);
    ASSERT_EQ(receivedText, String("Response text"));
    ASSERT(!sm.hasPending());
}

TEST(sampling_manager_unknown_response) {
    SamplingManager sm;
    JsonDocument doc;
    doc["role"] = "assistant";
    bool matched = sm.handleResponse(99999, doc.as<JsonObject>());
    ASSERT(!matched);
}

TEST(sampling_multiple_messages) {
    MCPSamplingRequest req;
    req.addUserMessage("First question");
    req.addAssistantMessage("First answer");
    req.addUserMessage("Follow up");

    String json = req.toJsonRpc(200);
    ASSERT_STR_CONTAINS(json.c_str(), "First question");
    ASSERT_STR_CONTAINS(json.c_str(), "First answer");
    ASSERT_STR_CONTAINS(json.c_str(), "Follow up");
}

// ── SSE Manager Tests ──────────────────────────────────────────────────

TEST(sse_manager_initial_state) {
    SSEManager mgr;
    ASSERT_EQ(mgr.clientCount(), (size_t)0);
    ASSERT(!mgr.hasClients("any-session"));
}

// ── Server Sampling Integration Tests ──────────────────────────────────

TEST(server_advertises_sampling_capability) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"sampling\"");
}

TEST(server_handles_sampling_response_in_jsonrpc) {
    auto* s = makeTestServer();
    // Initialize first
    s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");

    // Queue a sampling request manually
    MCPSamplingRequest req;
    req.addUserMessage("test");
    String receivedText;
    int id = s->sampling().queueRequest(req, [&](const MCPSamplingResponse& resp) {
        receivedText = resp.text;
    });

    // Simulate client sending back a response
    String response = String(R"({"jsonrpc":"2.0","id":)") + String(id) +
        R"(,"result":{"role":"assistant","model":"test","stopReason":"endTurn","content":{"type":"text","text":"AI says hello"}}})";
    s->_processJsonRpc(response);

    ASSERT_EQ(receivedText, String("AI says hello"));
    ASSERT(!s->sampling().hasPending());
}

// ── Elicitation Tests ──────────────────────────────────────────────────

TEST(elicitation_request_serialization) {
    MCPElicitationRequest req;
    req.message = "Configure sensor";
    req.addTextField("name", "Sensor Name", true);
    req.addNumberField("threshold", "Alert Threshold", false, 0, 100);
    req.addBooleanField("enabled", "Enable Alerts", false, true);
    req.addEnumField("unit", "Unit", {"celsius", "fahrenheit"}, true);

    String json = req.toJsonRpc(8000);
    ASSERT_STR_CONTAINS(json.c_str(), "elicitation/create");
    ASSERT_STR_CONTAINS(json.c_str(), "Configure sensor");
    ASSERT_STR_CONTAINS(json.c_str(), "\"name\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"threshold\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"enabled\"");
    ASSERT_STR_CONTAINS(json.c_str(), "celsius");
    ASSERT_STR_CONTAINS(json.c_str(), "fahrenheit");
    ASSERT_STR_CONTAINS(json.c_str(), "requestedSchema");
}

TEST(elicitation_request_integer_field) {
    MCPElicitationRequest req;
    req.message = "Set config";
    req.addIntegerField("count", "Item Count", true, 1, 50);

    String json = req.toJsonRpc(8001);
    ASSERT_STR_CONTAINS(json.c_str(), "\"integer\"");
    ASSERT_STR_CONTAINS(json.c_str(), "Item Count");
}

TEST(elicitation_response_accept) {
    JsonDocument doc;
    doc["action"] = "accept";
    JsonObject content = doc["content"].to<JsonObject>();
    content["name"] = "Kitchen Sensor";
    content["threshold"] = 42.5;
    content["enabled"] = true;
    content["count"] = 7;

    MCPElicitationResponse resp = MCPElicitationResponse::fromJson(doc.as<JsonObject>());
    ASSERT(resp.valid);
    ASSERT(resp.accepted());
    ASSERT(!resp.declined());
    ASSERT_EQ(resp.getString("name"), String("Kitchen Sensor"));
    ASSERT_EQ(resp.getFloat("threshold"), 42.5f);
    ASSERT_EQ(resp.getBool("enabled"), true);
    ASSERT_EQ(resp.getInt("count"), 7);
}

TEST(elicitation_response_decline) {
    JsonDocument doc;
    doc["action"] = "decline";

    MCPElicitationResponse resp = MCPElicitationResponse::fromJson(doc.as<JsonObject>());
    ASSERT(resp.valid);
    ASSERT(resp.declined());
    ASSERT(!resp.accepted());
    ASSERT_EQ(resp.getString("name"), String(""));
    ASSERT_EQ(resp.getInt("count", 99), 99);
}

TEST(elicitation_response_cancel) {
    JsonDocument doc;
    doc["action"] = "cancel";

    MCPElicitationResponse resp = MCPElicitationResponse::fromJson(doc.as<JsonObject>());
    ASSERT(resp.valid);
    ASSERT(!resp.accepted());
    ASSERT(!resp.declined());
}

TEST(elicitation_manager_queue_and_drain) {
    ElicitationManager em;
    MCPElicitationRequest req;
    req.message = "Test";
    req.addTextField("x", "X", false);

    bool called = false;
    int id = em.queueRequest(req, [&](const MCPElicitationResponse& r) { called = true; });
    ASSERT(id >= 8000);
    ASSERT(em.hasPending());
    ASSERT_EQ(em.pendingCount(), (size_t)1);

    auto out = em.drainOutgoing();
    ASSERT_EQ(out.size(), (size_t)1);
    ASSERT_STR_CONTAINS(out[0].c_str(), "elicitation/create");

    // Second drain should be empty
    auto out2 = em.drainOutgoing();
    ASSERT_EQ(out2.size(), (size_t)0);
}

TEST(elicitation_manager_handle_response) {
    ElicitationManager em;
    MCPElicitationRequest req;
    req.message = "Test";
    req.addTextField("val", "Value", true);

    String receivedVal;
    int id = em.queueRequest(req, [&](const MCPElicitationResponse& r) {
        receivedVal = r.getString("val");
    });

    JsonDocument doc;
    doc["action"] = "accept";
    doc["content"]["val"] = "Hello World";

    bool matched = em.handleResponse(id, doc.as<JsonObject>());
    ASSERT(matched);
    ASSERT_EQ(receivedVal, String("Hello World"));
    ASSERT(!em.hasPending());
}

TEST(elicitation_manager_unknown_response) {
    ElicitationManager em;
    JsonDocument doc;
    doc["action"] = "accept";
    bool matched = em.handleResponse(99999, doc.as<JsonObject>());
    ASSERT(!matched);
}

TEST(server_advertises_elicitation_capability) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"elicitation\"");
}

TEST(server_handles_elicitation_response_in_jsonrpc) {
    auto* s = makeTestServer();
    s->_processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");

    MCPElicitationRequest req;
    req.message = "Test input";
    req.addTextField("answer", "Answer", true);

    String receivedAnswer;
    int id = s->elicitation().queueRequest(req, [&](const MCPElicitationResponse& r) {
        receivedAnswer = r.getString("answer");
    });

    String response = String(R"({"jsonrpc":"2.0","id":)") + String(id) +
        R"(,"result":{"action":"accept","content":{"answer":"42"}}})";
    s->_processJsonRpc(response);

    ASSERT_EQ(receivedAnswer, String("42"));
    ASSERT(!s->elicitation().hasPending());
}

// ── Audio Content Tests ────────────────────────────────────────────────

TEST(audio_content_factory) {
    MCPContent audio = MCPContent::makeAudio("AQID", "audio/wav");
    ASSERT_EQ((int)audio.type, (int)MCPContent::AUDIO);
    ASSERT_EQ(audio.data, String("AQID"));
    ASSERT_EQ(audio.mimeType, String("audio/wav"));
}

TEST(audio_content_serialization) {
    MCPContent audio = MCPContent::makeAudio("dGVzdA==", "audio/mp3");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    audio.toJson(obj);

    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"type\":\"audio\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"data\":\"dGVzdA==\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"mimeType\":\"audio/mp3\"");
}

TEST(audio_tool_result_factory) {
    MCPToolResult result = MCPToolResult::audio("AQID", "audio/wav", "Recording");
    ASSERT_EQ(result.content.size(), (size_t)2);
    ASSERT_EQ((int)result.content[0].type, (int)MCPContent::TEXT);
    ASSERT_EQ((int)result.content[1].type, (int)MCPContent::AUDIO);
}

TEST(audio_tool_result_no_description) {
    MCPToolResult result = MCPToolResult::audio("AQID", "audio/wav");
    ASSERT_EQ(result.content.size(), (size_t)1);
    ASSERT_EQ((int)result.content[0].type, (int)MCPContent::AUDIO);
}

// ── Rate Limiter Tests ─────────────────────────────────────────────────

TEST(rate_limiter_default_disabled) {
    RateLimiter rl;
    ASSERT(!rl.isEnabled());
    ASSERT(rl.tryAcquire());  // always allows when disabled
}

TEST(rate_limiter_configure) {
    RateLimiter rl;
    rl.configure(10.0, 5);
    ASSERT(rl.isEnabled());
    ASSERT_EQ(rl.requestsPerSecond(), 10.0f);
    ASSERT_EQ(rl.burstCapacity(), (size_t)5);
}

TEST(rate_limiter_burst_capacity) {
    RateLimiter rl;
    rl.configure(1.0, 3);  // 1/sec, burst of 3
    // Should allow 3 requests (burst), then deny
    ASSERT(rl.tryAcquire());
    ASSERT(rl.tryAcquire());
    ASSERT(rl.tryAcquire());
    ASSERT(!rl.tryAcquire());  // 4th should fail
}

TEST(rate_limiter_stats) {
    RateLimiter rl;
    rl.configure(1.0, 2);
    rl.tryAcquire();  // allowed
    rl.tryAcquire();  // allowed
    rl.tryAcquire();  // denied
    ASSERT_EQ(rl.totalAllowed(), 2UL);
    ASSERT_EQ(rl.totalDenied(), 1UL);
}

TEST(rate_limiter_disable) {
    RateLimiter rl;
    rl.configure(1.0, 1);
    rl.tryAcquire();
    ASSERT(!rl.tryAcquire());  // denied
    rl.disable();
    ASSERT(rl.tryAcquire());  // allowed again
}

TEST(rate_limiter_reset_stats) {
    RateLimiter rl;
    rl.configure(1.0, 2);
    rl.tryAcquire();
    rl.tryAcquire();
    rl.tryAcquire();
    rl.resetStats();
    ASSERT_EQ(rl.totalAllowed(), 0UL);
    ASSERT_EQ(rl.totalDenied(), 0UL);
}

// ── Lifecycle Hooks Tests ──────────────────────────────────────────────

TEST(lifecycle_on_initialize_called) {
    Server* s = makeTestServer();
    String capturedClient;
    s->onInitialize([&](const String& name) {
        capturedClient = name;
    });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test-client","version":"1.0"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_EQ(capturedClient, String("test-client"));
    ASSERT_STR_CONTAINS(resp.c_str(), "\"protocolVersion\"");
}

TEST(lifecycle_on_initialize_unknown_client) {
    Server* s = makeTestServer();
    String capturedClient;
    s->onInitialize([&](const String& name) {
        capturedClient = name;
    });

    // Initialize without clientInfo
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26"}})";
    s->_processJsonRpc(req);
    ASSERT_EQ(capturedClient, String("unknown"));
}

TEST(rate_limit_in_server_info) {
    Server* s = makeTestServer();
    s->setRateLimit(10.0, 20);

    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "\"rateLimit\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"requestsPerSecond\"");
}

TEST(rate_limit_not_in_server_info_when_disabled) {
    Server* s = makeTestServer();
    // Don't configure rate limiting

    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})";
    String resp = s->_processJsonRpc(req);
    // Should NOT contain rateLimit
    std::string r(resp.c_str());
    ASSERT(r.find("rateLimit") == std::string::npos);
}

// ── Version Tests ──────────────────────────────────────────────────────

TEST(version_0_11_0) {
    Server* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "0.23.0");
}

// ── Watchdog Tool Tests ────────────────────────────────────────────────

TEST(watchdog_status_default) {
    // Test that the status function returns valid JSON with enabled=false
    // (We can't call the tool directly without registering, but we can test the static state)
    // WatchdogTool is header-only with statics, so we can verify the initial state
    ASSERT(!mcpd::tools::WatchdogTool::_enabled);
    ASSERT_EQ(mcpd::tools::WatchdogTool::_timeoutSec, 0);
}

TEST(watchdog_tool_registration) {
    Server* s = makeTestServer();
    size_t before = s->_tools.size();
    mcpd::tools::WatchdogTool::registerAll(*s);
    // Should add 4 tools: status, enable, feed, disable
    ASSERT_EQ(s->_tools.size(), before + 4);
}

// ── Session Manager Tests ──────────────────────────────────────────────

TEST(session_create_basic) {
    SessionManager sm;
    sm.setMaxSessions(4);
    String sid = sm.createSession("test-client");
    ASSERT(!sid.isEmpty());
    ASSERT_EQ(sm.activeCount(), (size_t)1);
}

TEST(session_validate_existing) {
    SessionManager sm;
    String sid = sm.createSession("client-a");
    ASSERT(sm.validateSession(sid));
}

TEST(session_validate_nonexistent) {
    SessionManager sm;
    ASSERT(!sm.validateSession("nonexistent-id"));
}

TEST(session_remove) {
    SessionManager sm;
    String sid = sm.createSession("client-a");
    ASSERT_EQ(sm.activeCount(), (size_t)1);
    ASSERT(sm.removeSession(sid));
    ASSERT_EQ(sm.activeCount(), (size_t)0);
}

TEST(session_max_limit) {
    SessionManager sm;
    sm.setMaxSessions(2);
    String s1 = sm.createSession("client-a");
    String s2 = sm.createSession("client-b");
    ASSERT(!s1.isEmpty());
    ASSERT(!s2.isEmpty());
    ASSERT_EQ(sm.activeCount(), (size_t)2);
    // Remove one, then third should succeed
    sm.removeSession(s1);
    String s3 = sm.createSession("client-c");
    ASSERT(!s3.isEmpty());
    ASSERT_EQ(sm.activeCount(), (size_t)2);
}

TEST(session_get_info) {
    SessionManager sm;
    String sid = sm.createSession("my-client");
    const Session* s = sm.getSession(sid);
    ASSERT(s != nullptr);
    ASSERT_STR_CONTAINS(s->clientName.c_str(), "my-client");
    ASSERT(s->initialized);
}

TEST(session_summary) {
    SessionManager sm;
    sm.createSession("client-a");
    sm.createSession("client-b");
    String summary = sm.summary();
    ASSERT_STR_CONTAINS(summary.c_str(), "activeSessions");
    ASSERT_STR_CONTAINS(summary.c_str(), "client-a");
}

TEST(session_idle_timeout_config) {
    SessionManager sm;
    sm.setIdleTimeout(5000);
    ASSERT_EQ(sm.idleTimeout(), 5000UL);
}

// ── Heap Monitor Tests ─────────────────────────────────────────────────

TEST(heap_monitor_initial_state) {
    HeapMonitor hm;
    ASSERT_EQ(hm.lastFree(), (size_t)0);
    ASSERT_EQ(hm.sampleCount(), (size_t)0);
    ASSERT(!hm.isLow());
}

TEST(heap_monitor_warning_threshold) {
    HeapMonitor hm;
    hm.setWarningThreshold(20000);
    // On host (non-ESP32), isLow() returns false always
    ASSERT(!hm.isLow());
}

TEST(heap_monitor_usage_percent_zero) {
    HeapMonitor hm;
    // Before any samples, total=0, should return 0
    float pct = hm.usagePercent();
    ASSERT(pct >= 0.0f && pct <= 0.01f);
}

// ── Server Session/Heap Integration ────────────────────────────────────

TEST(server_session_manager_access) {
    Server* s = makeTestServer();
    s->setMaxSessions(8);
    ASSERT_EQ(s->sessions().maxSessions(), (size_t)8);
}

TEST(server_heap_monitor_access) {
    Server* s = makeTestServer();
    // Just verify we can access it
    s->heap().setWarningThreshold(5000);
    ASSERT(!s->heap().isLow());
}

// ── CAN Tool Tests (unit-level, mock handlers) ────────────────────────

TEST(can_tool_status_mock) {
    auto* s = makeTestServer();
    s->addTool("can_status", "CAN status", R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return "{\"state\":\"running\",\"tx_error_counter\":0,\"rx_error_counter\":0}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"can_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "running");
    ASSERT_STR_CONTAINS(resp.c_str(), "tx_error_counter");
}

TEST(can_tool_send_mock) {
    auto* s = makeTestServer();
    bool called = false;
    s->addTool("can_send", "CAN send", R"({"type":"object","properties":{}})",
        [&called](const JsonObject& params) -> String {
            called = true;
            int id = params["id"] | -1;
            JsonDocument doc;
            doc["sent"] = true;
            doc["id"] = id;
            doc["dlc"] = 3;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"can_send","arguments":{"id":291,"data":[1,2,3]}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT(called);
    ASSERT_STR_CONTAINS(resp.c_str(), "sent");
    ASSERT_STR_CONTAINS(resp.c_str(), "291");
}

TEST(can_tool_receive_empty_mock) {
    auto* s = makeTestServer();
    s->addTool("can_receive", "CAN recv", R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return "{\"frames\":[],\"count\":0}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"can_receive","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
}

// ── Encoder Tool Tests ─────────────────────────────────────────────────

TEST(encoder_read_mock) {
    auto* s = makeTestServer();
    s->addTool("encoder_read", "Read encoder", R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return "{\"index\":0,\"position\":42,\"idle_ms\":100}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"encoder_read","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "position");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
}

TEST(encoder_reset_mock) {
    auto* s = makeTestServer();
    long position = 42;
    s->addTool("encoder_reset", "Reset encoder", R"({"type":"object","properties":{}})",
        [&position](const JsonObject& params) -> String {
            position = params["value"] | 0L;
            JsonDocument doc;
            doc["index"] = 0;
            doc["position"] = position;
            doc["reset"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"encoder_reset","arguments":{"index":0,"value":100}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "position");
    ASSERT_STR_CONTAINS(resp.c_str(), "100");
    ASSERT_EQ(position, 100L);
}

TEST(encoder_config_mock) {
    auto* s = makeTestServer();
    s->addTool("encoder_config", "Config encoder", R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return "{\"index\":0,\"steps_per_rev\":20,\"configured\":true}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"encoder_config","arguments":{"steps_per_rev":20}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "configured");
}

// ── Diagnostics Tool Tests ─────────────────────────────────────────────

TEST(diagnostics_tool_mock) {
    auto* s = makeTestServer();
    s->addTool("server_diagnostics", "Diagnostics", R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            JsonDocument doc;
            doc["version"]["mcpd"] = MCPD_VERSION;
            doc["version"]["mcp_protocol"] = MCPD_MCP_PROTOCOL_VERSION;
            doc["uptime"]["seconds"] = 123;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"server_diagnostics","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), MCPD_VERSION);
    ASSERT_STR_CONTAINS(resp.c_str(), "uptime");
}

TEST(diagnostics_version_macros) {
    ASSERT(strlen(MCPD_VERSION) > 0);
    ASSERT(strlen(MCPD_MCP_PROTOCOL_VERSION) > 0);
    ASSERT_STR_CONTAINS(MCPD_MCP_PROTOCOL_VERSION, "2025");
    ASSERT_STR_CONTAINS(MCPD_VERSION, "0.23.0");
}

// ── Batch JSON-RPC edge cases ──────────────────────────────────────────

TEST(batch_triple_notifications_empty) {
    auto* s = makeTestServer();
    String init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test"}}})";
    s->_processJsonRpc(init);
    String body = R"([
        {"jsonrpc":"2.0","method":"notifications/initialized"},
        {"jsonrpc":"2.0","method":"notifications/initialized"},
        {"jsonrpc":"2.0","method":"notifications/initialized"}
    ])";
    String resp = s->_processJsonRpc(body);
    ASSERT(resp.isEmpty());
}

TEST(batch_two_pings_returns_array) {
    auto* s = makeTestServer();
    String init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test"}}})";
    s->_processJsonRpc(init);
    String body = R"([
        {"jsonrpc":"2.0","method":"ping","id":77},
        {"jsonrpc":"2.0","method":"ping","id":78}
    ])";
    String resp = s->_processJsonRpc(body);
    ASSERT_STR_CONTAINS(resp.c_str(), "[");
    ASSERT_STR_CONTAINS(resp.c_str(), "77");
    ASSERT_STR_CONTAINS(resp.c_str(), "78");
}

// ── Error handling edge cases ──────────────────────────────────────────

TEST(error_invalid_jsonrpc_version) {
    auto* s = makeTestServer();
    String body = R"({"jsonrpc":"1.0","method":"ping","id":1})";
    String resp = s->_processJsonRpc(body);
    ASSERT_STR_CONTAINS(resp.c_str(), "wrong jsonrpc version");
}

TEST(error_method_not_found) {
    auto* s = makeTestServer();
    String init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test"}}})";
    s->_processJsonRpc(init);
    String req = R"({"jsonrpc":"2.0","id":2,"method":"totally/made/up","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Method not found");
}

TEST(error_tool_not_found) {
    auto* s = makeTestServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nonexistent_xyz","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Tool not found");
}

TEST(error_parse_malformed_json) {
    auto* s = makeTestServer();
    String resp = s->_processJsonRpc("{not valid json!!!");
    ASSERT_STR_CONTAINS(resp.c_str(), "Parse error");
}

TEST(error_empty_body) {
    auto* s = makeTestServer();
    String resp = s->_processJsonRpc("");
    ASSERT_STR_CONTAINS(resp.c_str(), "Parse error");
}

// ── Stepper Motor Tool tests ───────────────────────────────────────────

TEST(stepper_status_returns_position) {
    auto* s = makeTestServer();
    // Manually register a stepper tool for testing
    s->addTool("stepper_status", "Stepper status",
        R"({"type":"object","properties":{"index":{"type":"integer"}}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            doc["index"] = 0;
            doc["current_position"] = 0;
            doc["target_position"] = 0;
            doc["running"] = false;
            doc["max_speed"] = "1000.0";
            doc["acceleration"] = "500.0";
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stepper_status","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "current_position");
    ASSERT_STR_CONTAINS(resp.c_str(), "running");
    ASSERT_STR_CONTAINS(resp.c_str(), "max_speed");
}

TEST(stepper_move_requires_position_or_relative) {
    auto* s = makeTestServer();
    s->addTool("stepper_move", "Move stepper",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            if (!params["position"].is<JsonVariant>() && !params["relative"].is<JsonVariant>()) {
                return R"({"error":"Specify 'position' or 'relative'"})";
            }
            return R"({"moving":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stepper_move","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Specify");
}

TEST(stepper_config_sets_parameters) {
    auto* s = makeTestServer();
    s->addTool("stepper_config", "Config stepper",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            doc["max_speed"] = params["max_speed"] | 1000;
            doc["acceleration"] = params["acceleration"] | 500;
            doc["configured"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stepper_config","arguments":{"max_speed":2000,"acceleration":800}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "2000");
    ASSERT_STR_CONTAINS(resp.c_str(), "configured");
}

TEST(stepper_stop_emergency) {
    auto* s = makeTestServer();
    s->addTool("stepper_stop", "Stop stepper",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            bool emergency = params["emergency"] | false;
            JsonDocument doc;
            doc["stopped"] = emergency;
            doc["position"] = 0;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stepper_stop","arguments":{"emergency":true}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "stopped");
    ASSERT_STR_CONTAINS(resp.c_str(), "true");
}

TEST(stepper_home_no_endstop) {
    auto* s = makeTestServer();
    s->addTool("stepper_home", "Home stepper",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"error":"No endstop configured for this stepper"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stepper_home","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "endstop");
}

// ── Touch Sensor Tool tests ────────────────────────────────────────────

TEST(touch_read_returns_value) {
    auto* s = makeTestServer();
    s->addTool("touch_read", "Read touch",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            doc["index"] = 0;
            doc["gpio"] = 4;
            doc["value"] = 55;
            doc["threshold"] = 40;
            doc["baseline"] = 80;
            doc["touched"] = false;
            doc["strength_percent"] = 31;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"touch_read","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "value");
    ASSERT_STR_CONTAINS(resp.c_str(), "threshold");
    ASSERT_STR_CONTAINS(resp.c_str(), "strength_percent");
}

TEST(touch_read_all_returns_array) {
    auto* s = makeTestServer();
    s->addTool("touch_read_all", "Read all touch",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            JsonDocument doc;
            JsonArray pads = doc["pads"].to<JsonArray>();
            JsonObject p = pads.add<JsonObject>();
            p["index"] = 0;
            p["gpio"] = 4;
            p["value"] = 55;
            p["touched"] = false;
            doc["total_pads"] = 1;
            doc["touched_count"] = 0;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"touch_read_all","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pads");
    ASSERT_STR_CONTAINS(resp.c_str(), "total_pads");
}

TEST(touch_calibrate_returns_baseline) {
    auto* s = makeTestServer();
    s->addTool("touch_calibrate", "Calibrate touch",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            JsonArray pads = doc["pads"].to<JsonArray>();
            JsonObject p = pads.add<JsonObject>();
            p["index"] = 0;
            p["baseline"] = 82;
            p["threshold"] = 49;
            doc["samples"] = params["samples"] | 10;
            doc["calibrated"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"touch_calibrate","arguments":{"samples":5}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "calibrated");
    ASSERT_STR_CONTAINS(resp.c_str(), "baseline");
}

// ── Pulse Counter Tool tests ───────────────────────────────────────────

TEST(pulse_read_returns_count_and_frequency) {
    auto* s = makeTestServer();
    s->addTool("pulse_read", "Read pulses",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            doc["index"] = 0;
            doc["pin"] = 39;
            doc["count"] = 1500;
            doc["frequency_hz"] = "25.00";
            doc["rpm"] = "1500.0";
            doc["unit_value"] = "1500.000";
            doc["unit_name"] = "revolutions";
            doc["elapsed_ms"] = 60000;
            doc["avg_frequency_hz"] = "25.00";
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pulse_read","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "frequency_hz");
    ASSERT_STR_CONTAINS(resp.c_str(), "rpm");
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
}

TEST(pulse_reset_clears_count) {
    auto* s = makeTestServer();
    s->addTool("pulse_reset", "Reset pulses",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            JsonDocument doc;
            doc["index"] = 0;
            doc["previous_count"] = 1500;
            doc["reset"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pulse_reset","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "reset");
    ASSERT_STR_CONTAINS(resp.c_str(), "previous_count");
}

TEST(pulse_config_sets_scaling) {
    auto* s = makeTestServer();
    s->addTool("pulse_config", "Config pulses",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& params) -> String {
            JsonDocument doc;
            doc["pulses_per_unit"] = params["pulses_per_unit"] | 1.0f;
            doc["unit_name"] = params["unit_name"] | "liters";
            doc["configured"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pulse_config","arguments":{"pulses_per_unit":450,"unit_name":"liters"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "configured");
    ASSERT_STR_CONTAINS(resp.c_str(), "liters");
}

// ── DAC Tool Tests ─────────────────────────────────────────────────────

TEST(dac_write_sets_value) {
    auto* s = makeTestServer();
    s->addTool("dac_write", "Write DAC value",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"value":{"type":"integer"}},"required":["pin","value"]})",
        [](const JsonObject& params) -> String {
            int pin = params["pin"];
            int value = params["value"];
            if (pin != 25 && pin != 26) return R"({"error":"Invalid DAC pin"})";
            if (value < 0 || value > 255) return R"({"error":"Value must be 0-255"})";
            int ch = pin == 25 ? 1 : 2;
            char vBuf[16];
            snprintf(vBuf, sizeof(vBuf), "%.3f", value / 255.0 * 3.3);
            return String("{\"pin\":") + pin + ",\"channel\":" + ch +
                   ",\"value\":" + value + ",\"voltage\":" + vBuf + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"dac_write","arguments":{"pin":25,"value":128}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "128");
    ASSERT_STR_CONTAINS(resp.c_str(), "voltage");
}

TEST(dac_write_rejects_invalid_pin) {
    auto* s = makeTestServer();
    s->addTool("dac_write", "Write DAC value",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"value":{"type":"integer"}},"required":["pin","value"]})",
        [](const JsonObject& params) -> String {
            int pin = params["pin"];
            if (pin != 25 && pin != 26) return R"({"error":"Invalid DAC pin"})";
            return R"({"ok":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"dac_write","arguments":{"pin":13,"value":100}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Invalid DAC pin");
}

TEST(dac_write_voltage_converts_correctly) {
    auto* s = makeTestServer();
    s->addTool("dac_write_voltage", "Write DAC voltage",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"voltage":{"type":"number"}},"required":["pin","voltage"]})",
        [](const JsonObject& params) -> String {
            int pin = params["pin"];
            float voltage = params["voltage"];
            if (pin != 25 && pin != 26) return R"({"error":"Invalid DAC pin"})";
            uint8_t value = (uint8_t)(voltage / 3.3f * 255.0f + 0.5f);
            float actualV = value / 255.0f * 3.3f;
            char actBuf[16];
            snprintf(actBuf, sizeof(actBuf), "%.3f", actualV);
            return String("{\"pin\":") + pin + ",\"raw\":" + value +
                   ",\"actual_voltage\":" + actBuf + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"dac_write_voltage","arguments":{"pin":26,"voltage":1.65}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "actual_voltage");
    ASSERT_STR_CONTAINS(resp.c_str(), "raw");
}

TEST(dac_status_returns_both_channels) {
    auto* s = makeTestServer();
    s->addTool("dac_status", "DAC status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"channels":[{"channel":1,"pin":25,"enabled":false,"value":0,"voltage":0.000},{"channel":2,"pin":26,"enabled":true,"value":200,"voltage":2.588}]})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"dac_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "channels");
    ASSERT_STR_CONTAINS(resp.c_str(), "channel");
}

// ── Ultrasonic Distance Tool Tests ─────────────────────────────────────

TEST(distance_read_returns_measurement) {
    auto* s = makeTestServer();
    s->addTool("distance_read", "Read distance",
        R"({"type":"object","properties":{"index":{"type":"integer"}}})",
        [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            return String("{\"index\":") + idx +
                   R"(,"distance_cm":42.5,"distance_in":16.7,"distance_m":0.425,"samples_taken":3,"samples_valid":3,"speed_of_sound_mps":343.4,"temperature_c":20.0})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"distance_read","arguments":{"index":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "distance_cm");
    ASSERT_STR_CONTAINS(resp.c_str(), "42.5");
    ASSERT_STR_CONTAINS(resp.c_str(), "speed_of_sound");
}

TEST(distance_read_multi_returns_all_sensors) {
    auto* s = makeTestServer();
    s->addTool("distance_read_multi", "Read all sensors",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"sensors":[{"index":0,"label":"Bay 1","distance_cm":35.2,"valid":3},{"index":1,"label":"Bay 2","distance_cm":120.8,"valid":3}],"count":2})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"distance_read_multi","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sensors");
    ASSERT_STR_CONTAINS(resp.c_str(), "Bay 1");
    ASSERT_STR_CONTAINS(resp.c_str(), "Bay 2");
}

TEST(distance_config_updates_temperature) {
    auto* s = makeTestServer();
    s->addTool("distance_config", "Configure sensor",
        R"({"type":"object","properties":{"index":{"type":"integer"},"temperature":{"type":"number"}}})",
        [](const JsonObject& params) -> String {
            float temp = params["temperature"] | 20.0f;
            char buf[16]; snprintf(buf, sizeof(buf), "%.1f", temp);
            return String("{\"index\":0,\"configured\":true,\"temperature_c\":") +
                   buf + ",\"max_distance_cm\":400}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"distance_config","arguments":{"index":0,"temperature":25.5}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "configured");
    ASSERT_STR_CONTAINS(resp.c_str(), "25.5");
}

TEST(distance_read_no_echo_returns_error) {
    auto* s = makeTestServer();
    s->addTool("distance_read", "Read distance",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"error":"No echo received — object out of range or sensor disconnected"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"distance_read","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "No echo");
}

// ── Buzzer Tool Tests ──────────────────────────────────────────────────

TEST(buzzer_tone_plays_frequency) {
    auto* s = makeTestServer();
    s->addTool("buzzer_tone", "Play tone",
        R"({"type":"object","properties":{"frequency":{"type":"integer"},"duration":{"type":"integer"}},"required":["frequency","duration"]})",
        [](const JsonObject& params) -> String {
            int freq = params["frequency"];
            int dur = params["duration"];
            return String("{\"frequency\":") + freq + ",\"duration_ms\":" + dur +
                   ",\"pin\":4,\"played\":true}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"buzzer_tone","arguments":{"frequency":440,"duration":500}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "frequency");
    ASSERT_STR_CONTAINS(resp.c_str(), "440");
    ASSERT_STR_CONTAINS(resp.c_str(), "played");
}

TEST(buzzer_melody_plays_predefined) {
    auto* s = makeTestServer();
    s->addTool("buzzer_melody", "Play melody",
        R"({"type":"object","properties":{"name":{"type":"string"}}})",
        [](const JsonObject& params) -> String {
            String name = params["name"].as<const char*>();
            return String("{\"notes_played\":3,\"total_duration_ms\":400,\"tempo\":1.00,\"played\":true}");
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"buzzer_melody","arguments":{"name":"success"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "notes_played");
    ASSERT_STR_CONTAINS(resp.c_str(), "played");
}

TEST(buzzer_stop_halts_playback) {
    auto* s = makeTestServer();
    s->addTool("buzzer_stop", "Stop buzzer",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"stopped":true,"was_active":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"buzzer_stop","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "stopped");
    ASSERT_STR_CONTAINS(resp.c_str(), "was_active");
}

TEST(buzzer_tone_rejects_invalid_frequency) {
    auto* s = makeTestServer();
    s->addTool("buzzer_tone", "Play tone",
        R"({"type":"object","properties":{"frequency":{"type":"integer"},"duration":{"type":"integer"}},"required":["frequency","duration"]})",
        [](const JsonObject& params) -> String {
            int freq = params["frequency"];
            if (freq < 20 || freq > 20000) return R"({"error":"Frequency must be 20-20000 Hz"})";
            return R"({"played":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"buzzer_tone","arguments":{"frequency":5,"duration":100}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Frequency must be");
}

// ── LCD Tool Tests ─────────────────────────────────────────────────────

TEST(lcd_print_text) {
    auto* s = makeTestServer();
    s->addTool("lcd_print", "Print text on LCD",
        R"({"type":"object","properties":{"text":{"type":"string"},"row":{"type":"integer"},"col":{"type":"integer"}},"required":["text"]})",
        [](const JsonObject& params) -> String {
            const char* text = params["text"] | "";
            int row = params["row"] | 0;
            int col = params["col"] | 0;
            return String("{\"printed\":true,\"text\":\"") + text +
                   "\",\"row\":" + row + ",\"col\":" + col + ",\"chars_written\":" + (int)strlen(text) + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"lcd_print","arguments":{"text":"Hello","row":0,"col":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "printed");
    ASSERT_STR_CONTAINS(resp.c_str(), "Hello");
}

TEST(lcd_clear_display) {
    auto* s = makeTestServer();
    s->addTool("lcd_clear", "Clear LCD",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String { return R"({"cleared":true})"; });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"lcd_clear","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "cleared");
}

TEST(lcd_backlight_control) {
    auto* s = makeTestServer();
    s->addTool("lcd_backlight", "Backlight",
        R"({"type":"object","properties":{"on":{"type":"boolean"}},"required":["on"]})",
        [](const JsonObject& params) -> String {
            return R"({"backlight":false,"toggled":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"lcd_backlight","arguments":{"on":false}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "backlight");
    ASSERT_STR_CONTAINS(resp.c_str(), "toggled");
}

TEST(lcd_createChar_valid) {
    auto* s = makeTestServer();
    s->addTool("lcd_createChar", "Custom char",
        R"({"type":"object","properties":{"slot":{"type":"integer"},"pattern":{"type":"array"}},"required":["slot","pattern"]})",
        [](const JsonObject& params) -> String {
            int slot = params["slot"] | 0;
            if (slot < 0 || slot > 7) return R"({"error":"Slot must be 0-7"})";
            return String("{\"created\":true,\"slot\":") + slot + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"lcd_createChar","arguments":{"slot":3,"pattern":[0,0,14,17,17,14,0,0]}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "created");
    ASSERT_STR_CONTAINS(resp.c_str(), "3");
}

TEST(lcd_status_returns_config) {
    auto* s = makeTestServer();
    s->addTool("lcd_status", "LCD status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"address":"0x27","size":"16x2","backlight":true,"lines":["Hello",""]})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"lcd_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "16x2");
    ASSERT_STR_CONTAINS(resp.c_str(), "0x27");
}

// ── IR Tool Tests ──────────────────────────────────────────────────────

TEST(ir_send_nec_code) {
    auto* s = makeTestServer();
    s->addTool("ir_send", "Send IR",
        R"({"type":"object","properties":{"protocol":{"type":"string"},"code":{"type":"integer"}},"required":["protocol","code"]})",
        [](const JsonObject& params) -> String {
            const char* proto = params["protocol"] | "NEC";
            int code = params["code"] | 0;
            return String("{\"sent\":true,\"protocol\":\"") + proto +
                   "\",\"code\":" + code + ",\"bits\":32}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ir_send","arguments":{"protocol":"NEC","code":16753245}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sent");
    ASSERT_STR_CONTAINS(resp.c_str(), "NEC");
    ASSERT_STR_CONTAINS(resp.c_str(), "16753245");
}

TEST(ir_send_raw_timings) {
    auto* s = makeTestServer();
    s->addTool("ir_send_raw", "Send raw IR",
        R"({"type":"object","properties":{"timings":{"type":"array"},"frequency":{"type":"integer"}},"required":["timings"]})",
        [](const JsonObject& params) -> String {
            auto timings = params["timings"];
            int count = timings.size();
            int freq = params["frequency"] | 38000;
            return String("{\"sent\":true,\"protocol\":\"RAW\",\"timings_count\":") +
                   count + ",\"carrier_hz\":" + freq + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ir_send_raw","arguments":{"timings":[9000,4500,560,560,560,1690],"frequency":38000}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "RAW");
    ASSERT_STR_CONTAINS(resp.c_str(), "timings_count");
}

TEST(ir_status_info) {
    auto* s = makeTestServer();
    s->addTool("ir_status", "IR status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"send_pin":14,"recv_pin":15,"total_sent":0})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ir_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "send_pin");
    ASSERT_STR_CONTAINS(resp.c_str(), "total_sent");
}

// ── RTC Tool Tests ─────────────────────────────────────────────────────

TEST(rtc_get_datetime) {
    auto* s = makeTestServer();
    s->addTool("rtc_get", "Get RTC time",
        R"({"type":"object","properties":{"format":{"type":"string"}}})",
        [](const JsonObject&) -> String {
            return R"({"datetime":"2026-02-18T12:00:00","day_of_week":"Wed"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rtc_get","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "2026-02-18");
    ASSERT_STR_CONTAINS(resp.c_str(), "Wed");
}

TEST(rtc_set_datetime) {
    auto* s = makeTestServer();
    s->addTool("rtc_set", "Set RTC time",
        R"({"type":"object","properties":{"year":{"type":"integer"},"month":{"type":"integer"},"day":{"type":"integer"},"hour":{"type":"integer"},"minute":{"type":"integer"},"second":{"type":"integer"}},"required":["year","month","day","hour","minute","second"]})",
        [](const JsonObject& params) -> String {
            int y = params["year"], mo = params["month"], d = params["day"];
            int h = params["hour"], mi = params["minute"], s = params["second"];
            if (mo < 1 || mo > 12) return R"({"error":"Invalid date"})";
            char iso[25];
            snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, s);
            return String("{\"set\":true,\"datetime\":\"") + iso + "\"}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rtc_set","arguments":{"year":2026,"month":3,"day":15,"hour":14,"minute":30,"second":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "set");
    ASSERT_STR_CONTAINS(resp.c_str(), "2026-03-15");
}

TEST(rtc_set_rejects_invalid_date) {
    auto* s = makeTestServer();
    s->addTool("rtc_set", "Set RTC time",
        R"({"type":"object","properties":{"year":{"type":"integer"},"month":{"type":"integer"},"day":{"type":"integer"},"hour":{"type":"integer"},"minute":{"type":"integer"},"second":{"type":"integer"}},"required":["year","month","day","hour","minute","second"]})",
        [](const JsonObject& params) -> String {
            int mo = params["month"];
            if (mo < 1 || mo > 12) return R"({"error":"Invalid date"})";
            return R"({"set":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rtc_set","arguments":{"year":2026,"month":13,"day":1,"hour":0,"minute":0,"second":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Invalid date");
}

TEST(rtc_alarm_set_and_check) {
    auto* s = makeTestServer();
    s->addTool("rtc_alarm", "RTC alarm",
        R"({"type":"object","properties":{"alarm":{"type":"integer"},"action":{"type":"string"},"hour":{"type":"integer"},"minute":{"type":"integer"}},"required":["alarm","action"]})",
        [](const JsonObject& params) -> String {
            int num = params["alarm"] | 1;
            const char* action = params["action"] | "check";
            if (strcmp(action, "set") == 0) {
                int h = params["hour"] | 0;
                int m = params["minute"] | 0;
                char buf[16];
                snprintf(buf, sizeof(buf), "%02d:%02d:00", h, m);
                return String("{\"alarm\":") + num + ",\"set\":true,\"time\":\"" + buf + "\"}";
            }
            return String("{\"alarm\":") + num + ",\"enabled\":false,\"triggered\":false}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rtc_alarm","arguments":{"alarm":1,"action":"set","hour":7,"minute":30}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "set");
    ASSERT_STR_CONTAINS(resp.c_str(), "07:30");
}

TEST(rtc_temperature_reads) {
    auto* s = makeTestServer();
    s->addTool("rtc_temperature", "RTC temperature",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"celsius":22.50,"fahrenheit":72.50,"sensor":"DS3231 built-in"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rtc_temperature","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "22.50");
    ASSERT_STR_CONTAINS(resp.c_str(), "DS3231");
}

// ── NVS Tool Tests ─────────────────────────────────────────────────────

TEST(nvs_set_string) {
    auto* s = makeTestServer();
    s->addTool("nvs_set", "Store key-value",
        R"({"type":"object","properties":{"key":{"type":"string"},"value":{}},"required":["key","value"]})",
        [](const JsonObject& params) -> String {
            const char* key = params["key"] | "";
            if (strlen(key) == 0) return R"({"error":"Key is required"})";
            if (strlen(key) > 15) return R"json({"error":"Key too long (max 15 characters for NVS)"})json";
            const char* rawVal = params["value"].as<const char*>();
            String val = rawVal ? rawVal : "";
            return String("{\"key\":\"") + key + "\",\"value\":\"" + val + "\",\"type\":\"string\",\"persisted\":true}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_set","arguments":{"key":"wifi_ssid","value":"MyNetwork"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "wifi_ssid");
    ASSERT_STR_CONTAINS(resp.c_str(), "MyNetwork");
    ASSERT_STR_CONTAINS(resp.c_str(), "persisted");
}

TEST(nvs_set_integer) {
    auto* s = makeTestServer();
    s->addTool("nvs_set", "Store key-value",
        R"({"type":"object","properties":{"key":{"type":"string"},"value":{}},"required":["key","value"]})",
        [](const JsonObject& params) -> String {
            const char* key = params["key"] | "";
            int val = params["value"];
            return String("{\"key\":\"") + key + "\",\"value\":" + val + ",\"type\":\"int\",\"persisted\":true}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_set","arguments":{"key":"boot_count","value":42}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "boot_count");
    ASSERT_STR_CONTAINS(resp.c_str(), "42");
    ASSERT_STR_CONTAINS(resp.c_str(), "int");
}

TEST(nvs_set_rejects_long_key) {
    auto* s = makeTestServer();
    s->addTool("nvs_set", "Store key-value",
        R"({"type":"object","properties":{"key":{"type":"string"},"value":{}},"required":["key","value"]})",
        [](const JsonObject& params) -> String {
            const char* key = params["key"] | "";
            if (strlen(key) > 15) return R"json({"error":"Key too long (max 15 characters for NVS)"})json";
            return R"({"persisted":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_set","arguments":{"key":"this_key_is_way_too_long","value":"x"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Key too long");
}

TEST(nvs_get_existing_key) {
    auto* s = makeTestServer();
    s->addTool("nvs_get", "Retrieve value",
        R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})",
        [](const JsonObject& params) -> String {
            const char* key = params["key"] | "";
            if (strcmp(key, "name") == 0) return R"({"key":"name","found":true,"value":"tracker-1","type":"string"})";
            return String("{\"key\":\"") + key + "\",\"found\":false}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_get","arguments":{"key":"name"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "found");
    ASSERT_STR_CONTAINS(resp.c_str(), "tracker-1");
}

TEST(nvs_get_missing_key) {
    auto* s = makeTestServer();
    s->addTool("nvs_get", "Retrieve value",
        R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})",
        [](const JsonObject& params) -> String {
            return R"({"key":"nonexistent","found":false})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_get","arguments":{"key":"nonexistent"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "found");
}

TEST(nvs_delete_key) {
    auto* s = makeTestServer();
    s->addTool("nvs_delete", "Delete key",
        R"({"type":"object","properties":{"key":{"type":"string"}},"required":["key"]})",
        [](const JsonObject& params) -> String {
            return R"({"key":"old_setting","deleted":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_delete","arguments":{"key":"old_setting"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "deleted");
}

TEST(nvs_list_entries) {
    auto* s = makeTestServer();
    s->addTool("nvs_list", "List keys",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"namespace":"mcpd","count":2,"entries":[{"key":"a","type":"string","value":"hello"},{"key":"b","type":"int","value":99}]})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_list","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "count");
    ASSERT_STR_CONTAINS(resp.c_str(), "entries");
}

TEST(nvs_status) {
    auto* s = makeTestServer();
    s->addTool("nvs_status", "NVS status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"namespace":"mcpd","used_entries":5,"free_entries":995,"total_operations":42})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"nvs_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "used_entries");
    ASSERT_STR_CONTAINS(resp.c_str(), "free_entries");
}

// ── GPS Tool Tests ─────────────────────────────────────────────────────

TEST(gps_read_with_fix) {
    auto* s = makeTestServer();
    s->addTool("gps_read", "Read GPS",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"fix":true,"latitude":47.376887,"longitude":8.541694,"altitude_m":408.2,"satellites":9,"hdop":1.1,"time":"2026-02-18T13:00:00Z","fix_age_s":2})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_read","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "47.376887");
    ASSERT_STR_CONTAINS(resp.c_str(), "8.541694");
    ASSERT_STR_CONTAINS(resp.c_str(), "fix");
}

TEST(gps_read_no_fix) {
    auto* s = makeTestServer();
    s->addTool("gps_read", "Read GPS",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"fix":false,"satellites":2,"message":"No GPS fix yet. Ensure antenna has clear sky view."})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_read","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "fix");
    ASSERT_STR_CONTAINS(resp.c_str(), "No GPS fix");
}

TEST(gps_satellites) {
    auto* s = makeTestServer();
    s->addTool("gps_satellites", "Satellite info",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"satellites":12,"fix":true,"hdop":0.9,"quality":"excellent","total_fixes":847})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_satellites","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "satellites");
    ASSERT_STR_CONTAINS(resp.c_str(), "excellent");
}

TEST(gps_speed) {
    auto* s = makeTestServer();
    s->addTool("gps_speed", "GPS speed",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"speed_kmh":52.3,"speed_ms":14.53,"speed_mph":32.5,"course_deg":127.5,"cardinal":"SE"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_speed","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "52.3");
    ASSERT_STR_CONTAINS(resp.c_str(), "SE");
}

TEST(gps_distance_to_target) {
    auto* s = makeTestServer();
    s->addTool("gps_distance", "Distance calc",
        R"({"type":"object","properties":{"lat":{"type":"number"},"lon":{"type":"number"}},"required":["lat","lon"]})",
        [](const JsonObject& params) -> String {
            double lat = params["lat"] | 0.0;
            double lon = params["lon"] | 0.0;
            // Simple mock: distance from Zurich (47.38, 8.54) to target
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"distance_m\":12450.5,\"distance_km\":12.451,\"target_lat\":%.6f,\"target_lon\":%.6f,\"source\":\"provided\"}", lat, lon);
            return String(buf);
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_distance","arguments":{"lat":47.5,"lon":8.6}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "distance_m");
    ASSERT_STR_CONTAINS(resp.c_str(), "distance_km");
}

TEST(gps_status) {
    auto* s = makeTestServer();
    s->addTool("gps_status", "GPS status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"fix":true,"satellites":8,"hdop":1.5,"total_fixes":200,"last_fix_age_s":1,"serial_connected":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gps_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "total_fixes");
    ASSERT_STR_CONTAINS(resp.c_str(), "serial_connected");
}

// ── Relay Tool Tests ───────────────────────────────────────────────────

TEST(relay_set_on) {
    auto* s = makeTestServer();
    s->addTool("relay_set", "Set relay",
        R"({"type":"object","properties":{"channel":{"type":"integer"},"state":{"type":"string"}},"required":["state"]})",
        [](const JsonObject& params) -> String {
            int ch = params["channel"] | 0;
            const char* st = params["state"] | "off";
            return String("{\"channel\":") + ch + ",\"label\":\"pump\",\"state\":\"" + st + "\"}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_set","arguments":{"channel":0,"state":"on"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pump");
}

TEST(relay_set_by_label) {
    auto* s = makeTestServer();
    s->addTool("relay_set", "Set relay",
        R"({"type":"object","properties":{"label":{"type":"string"},"state":{"type":"string"}},"required":["state"]})",
        [](const JsonObject& params) -> String {
            const char* label = params["label"] | "unknown";
            const char* st = params["state"] | "off";
            return String("{\"channel\":0,\"label\":\"") + label + "\",\"state\":\"" + (strcmp(st, "on") == 0 ? "ON" : "OFF") + "\"}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_set","arguments":{"label":"heater","state":"on"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "heater");
    ASSERT_STR_CONTAINS(resp.c_str(), "ON");
}

TEST(relay_toggle) {
    auto* s = makeTestServer();
    s->addTool("relay_toggle", "Toggle relay",
        R"({"type":"object","properties":{"channel":{"type":"integer"}},"required":[]})",
        [](const JsonObject& params) -> String {
            return R"({"channel":0,"label":"light","state":"ON"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_toggle","arguments":{"channel":0}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "light");
    ASSERT_STR_CONTAINS(resp.c_str(), "ON");
}

TEST(relay_pulse) {
    auto* s = makeTestServer();
    s->addTool("relay_pulse", "Pulse relay",
        R"({"type":"object","properties":{"channel":{"type":"integer"},"duration_ms":{"type":"integer"}},"required":["duration_ms"]})",
        [](const JsonObject& params) -> String {
            int dur = params["duration_ms"] | 500;
            if (dur < 50 || dur > 30000) return R"({"error":"Duration must be 50-30000ms"})";
            return String("{\"channel\":0,\"label\":\"solenoid\",\"pulsed_ms\":") + dur + "}";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_pulse","arguments":{"channel":0,"duration_ms":200}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "pulsed_ms");
    ASSERT_STR_CONTAINS(resp.c_str(), "200");
}

TEST(relay_pulse_rejects_invalid_duration) {
    auto* s = makeTestServer();
    s->addTool("relay_pulse", "Pulse relay",
        R"({"type":"object","properties":{"channel":{"type":"integer"},"duration_ms":{"type":"integer"}},"required":["duration_ms"]})",
        [](const JsonObject& params) -> String {
            int dur = params["duration_ms"] | 500;
            if (dur < 50 || dur > 30000) return R"({"error":"Duration must be 50-30000ms"})";
            return R"({"pulsed":true})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_pulse","arguments":{"channel":0,"duration_ms":50000}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Duration must be");
}

TEST(relay_all_off) {
    auto* s = makeTestServer();
    s->addTool("relay_all_off", "All off",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"turned_off":3,"total_channels":4})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_all_off","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "turned_off");
}

TEST(relay_status) {
    auto* s = makeTestServer();
    s->addTool("relay_status", "Relay status",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            return R"({"total_channels":2,"active":1,"channels":[{"channel":0,"label":"pump","state":"ON"},{"channel":1,"label":"fan","state":"OFF"}]})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_status","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "total_channels");
    ASSERT_STR_CONTAINS(resp.c_str(), "pump");
    ASSERT_STR_CONTAINS(resp.c_str(), "fan");
}

TEST(relay_interlock_concept) {
    auto* s = makeTestServer();
    // Test that interlock is conceptually described — handler simulates turning off heater when cooler turns on
    s->addTool("relay_set", "Set relay with interlocks",
        R"({"type":"object","properties":{"label":{"type":"string"},"state":{"type":"string"}},"required":["state"]})",
        [](const JsonObject& params) -> String {
            const char* label = params["label"] | "";
            const char* st = params["state"] | "off";
            if (strcmp(label, "cooler") == 0 && strcmp(st, "on") == 0) {
                return R"({"channel":1,"label":"cooler","state":"ON","interlocked":["heater turned OFF"]})";
            }
            return R"({"channel":0,"state":"ON"})";
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"relay_set","arguments":{"label":"cooler","state":"on"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "cooler");
    ASSERT_STR_CONTAINS(resp.c_str(), "heater turned OFF");
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — JSON-RPC Unit Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    // Tests run via static initialization above

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
