/**
 * mcpd — Integration Tests
 *
 * Tests for Auth integration, Metrics integration, batch JSON-RPC,
 * and cross-module interactions.
 */

#include "arduino_mock.h"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

static struct _hdr_integ { _hdr_integ() {
    printf("\n  mcpd — Integration Tests\n  ════════════════════════════════════════\n\n");
} } _h_integ;

// ════════════════════════════════════════════════════════════════════════
// Auth Integration Tests
// ════════════════════════════════════════════════════════════════════════

TEST(auth_disabled_by_default) {
    Server server("test", 80);
    ASSERT(!server.auth().isEnabled());
}

TEST(auth_enable_with_api_key) {
    Server server("test", 80);
    server.auth().setApiKey("secret-key-123");
    ASSERT(server.auth().isEnabled());
}

TEST(auth_multiple_api_keys) {
    Server server("test", 80);
    server.auth().setApiKey("key1");
    server.auth().addApiKey("key2");
    ASSERT(server.auth().isEnabled());
}

TEST(auth_disable_after_enabling) {
    Server server("test", 80);
    server.auth().setApiKey("my-key");
    ASSERT(server.auth().isEnabled());
    server.auth().disable();
    ASSERT(!server.auth().isEnabled());
}

TEST(auth_custom_callback) {
    Server server("test", 80);
    server.auth().setAuthCallback([](const String& token) -> bool {
        return token == "custom-token";
    });
    ASSERT(server.auth().isEnabled());
}

TEST(auth_setApiKey_replaces_previous) {
    Auth auth;
    auth.setApiKey("key1");
    auth.setApiKey("key2");
    ASSERT(auth.isEnabled());
}

TEST(auth_addApiKey_accumulates) {
    Auth auth;
    auth.setApiKey("key1");
    auth.addApiKey("key2");
    auth.addApiKey("key3");
    ASSERT(auth.isEnabled());
}

TEST(auth_reenable_after_disable) {
    Auth auth;
    auth.setApiKey("key1");
    auth.disable();
    ASSERT(!auth.isEnabled());
    auth.setApiKey("key2");
    ASSERT(auth.isEnabled());
}

// ════════════════════════════════════════════════════════════════════════
// Metrics Integration Tests
// ════════════════════════════════════════════════════════════════════════

TEST(metrics_initial_state) {
    Server server("test", 80);
    ASSERT_EQ((int)server.metrics().totalRequests(), 0);
    ASSERT_EQ((int)server.metrics().totalErrors(), 0);
}

TEST(metrics_record_request) {
    Metrics metrics;
    ASSERT_EQ((int)metrics.totalRequests(), 0);
    metrics.recordRequest("tools/call", 5);
    ASSERT_EQ((int)metrics.totalRequests(), 1);
    metrics.recordRequest("tools/list", 2);
    ASSERT_EQ((int)metrics.totalRequests(), 2);
}

TEST(metrics_record_error) {
    Metrics metrics;
    ASSERT_EQ((int)metrics.totalErrors(), 0);
    metrics.recordError();
    ASSERT_EQ((int)metrics.totalErrors(), 1);
    metrics.recordError();
    ASSERT_EQ((int)metrics.totalErrors(), 2);
}

TEST(metrics_set_sse_clients) {
    Metrics metrics;
    metrics.setSSEClients(3);
    ASSERT_EQ((int)metrics.totalRequests(), 0);
}

TEST(metrics_recorded_via_dispatch_initialize) {
    Server server("test", 80);
    String init = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})";
    server._processJsonRpc(init);
    ASSERT_EQ((int)server.metrics().totalRequests(), 1);
}

TEST(metrics_recorded_via_dispatch_multiple) {
    Server server("test", 80);
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"ping"})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
    ASSERT_EQ((int)server.metrics().totalRequests(), 3);
}

TEST(metrics_error_on_method_not_found) {
    Server server("test", 80);
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"nonexistent/method"})");
    ASSERT_EQ((int)server.metrics().totalErrors(), 1);
    ASSERT_EQ((int)server.metrics().totalRequests(), 0);
}

TEST(metrics_notification_methods_recorded) {
    Server server("test", 80);
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    ASSERT_EQ((int)server.metrics().totalRequests(), 2);
}

// ════════════════════════════════════════════════════════════════════════
// Batch JSON-RPC Tests
// ════════════════════════════════════════════════════════════════════════

TEST(batch_single_request_in_array) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])");
    ASSERT(result.startsWith("["));
    ASSERT(result.endsWith("]"));
}

TEST(batch_multiple_requests) {
    Server server("test", 80);
    String batch = R"([
        {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}},
        {"jsonrpc":"2.0","id":2,"method":"ping"}
    ])";
    String result = server._processJsonRpc(batch);
    ASSERT(result.startsWith("["));
    ASSERT(result.endsWith("]"));
    ASSERT(result.indexOf("\"id\":1") >= 0 || result.indexOf("\"id\": 1") >= 0);
    ASSERT(result.indexOf("\"id\":2") >= 0 || result.indexOf("\"id\": 2") >= 0);
}

TEST(batch_all_notifications_returns_empty) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"([{"jsonrpc":"2.0","method":"notifications/initialized"}])");
    ASSERT(result.isEmpty());
}

TEST(batch_mixed_requests_and_notifications) {
    Server server("test", 80);
    String batch = R"([
        {"jsonrpc":"2.0","method":"notifications/initialized"},
        {"jsonrpc":"2.0","id":1,"method":"ping"}
    ])";
    String result = server._processJsonRpc(batch);
    ASSERT(result.startsWith("["));
    ASSERT(result.indexOf("\"id\":1") >= 0 || result.indexOf("\"id\": 1") >= 0);
}

TEST(batch_empty_array) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"([])");
    ASSERT(result.isEmpty());
}

TEST(batch_metrics_count_all) {
    Server server("test", 80);
    String batch = R"([
        {"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}},
        {"jsonrpc":"2.0","id":2,"method":"ping"},
        {"jsonrpc":"2.0","id":3,"method":"tools/list"}
    ])";
    server._processJsonRpc(batch);
    ASSERT_EQ((int)server.metrics().totalRequests(), 3);
}

// ════════════════════════════════════════════════════════════════════════
// JSON-RPC Edge Cases
// ════════════════════════════════════════════════════════════════════════

TEST(jsonrpc_missing_version_field) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"id":1,"method":"ping"})");
    ASSERT(result.indexOf("Invalid Request") >= 0);
}

TEST(jsonrpc_wrong_version) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"1.0","id":1,"method":"ping"})");
    ASSERT(result.indexOf("Invalid Request") >= 0);
}

TEST(jsonrpc_parse_error) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"(not json at all)");
    ASSERT(result.indexOf("Parse error") >= 0);
}

TEST(jsonrpc_string_id_preserved) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":"abc-123","method":"ping"})");
    ASSERT(result.indexOf("abc-123") >= 0);
}

TEST(jsonrpc_numeric_id_preserved) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":42,"method":"ping"})");
    ASSERT(result.indexOf("42") >= 0);
}

TEST(jsonrpc_subscribe_missing_uri) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/subscribe","params":{}})");
    ASSERT(result.indexOf("Missing resource URI") >= 0);
}

TEST(jsonrpc_unsubscribe_missing_uri) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/unsubscribe","params":{}})");
    ASSERT(result.indexOf("Missing resource URI") >= 0);
}

TEST(jsonrpc_prompts_get_missing_name) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{}})");
    ASSERT(result.indexOf("Missing prompt name") >= 0);
}

TEST(jsonrpc_prompts_get_unknown) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"nonexistent"}})");
    ASSERT(result.indexOf("Prompt not found") >= 0);
}

TEST(jsonrpc_tools_call_missing_name) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{}})");
    ASSERT(result.indexOf("Missing tool name") >= 0);
}

TEST(jsonrpc_tools_call_unknown_tool) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"no_such_tool"}})");
    ASSERT(result.indexOf("Tool not found") >= 0);
}

TEST(jsonrpc_resources_read_missing_uri) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{}})");
    ASSERT(result.indexOf("Missing resource URI") >= 0);
}

TEST(jsonrpc_resources_read_unknown) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"unknown://x"}})");
    ASSERT(result.indexOf("Resource not found") >= 0);
}

// ════════════════════════════════════════════════════════════════════════
// Cross-Module Integration Tests
// ════════════════════════════════════════════════════════════════════════

TEST(integ_initialize_advertises_capabilities) {
    Server server("test", 80);
    server.addTool("echo", "Echo", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "ok"; });
    server.addResource("test://s", "S", "Status", "text/plain",
        []() -> String { return "running"; });
    server.addPrompt("greet", "Greeting", {},
        [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> { return {}; });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"t"}}})");
    ASSERT(result.indexOf("tools") >= 0);
    ASSERT(result.indexOf("resources") >= 0);
    ASSERT(result.indexOf("prompts") >= 0);
}

TEST(integ_tool_call_returns_content) {
    Server server("test", 80);
    server.addTool("hello", "Say hello", R"({"type":"object","properties":{"name":{"type":"string"}}})",
        [](const JsonObject& params) -> String {
            String name = params["name"] | "world";
            return "Hello, " + name + "!";
        });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"hello","arguments":{"name":"mcpd"}}})");
    ASSERT(result.indexOf("Hello, mcpd!") >= 0);
    ASSERT(result.indexOf("\"type\":\"text\"") >= 0);
}

TEST(integ_resource_read) {
    Server server("test", 80);
    server.addResource("test://data", "Data", "Test", "application/json",
        []() -> String { return R"({"value":42})"; });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"test://data"}})");
    ASSERT(result.indexOf("42") >= 0);
    ASSERT(result.indexOf("test://data") >= 0);
}

TEST(integ_prompt_get_with_args) {
    Server server("test", 80);
    server.addPrompt("greet", "Greeting", {
        MCPPromptArgument{"name", "Person", true}
    }, [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
        auto it = args.find("name");
        String name = (it != args.end()) ? it->second : "world";
        return {MCPPromptMessage("user", ("Hello " + name).c_str())};
    });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"greet","arguments":{"name":"Alice"}}})");
    ASSERT(result.indexOf("Hello Alice") >= 0);
}

TEST(integ_prompt_missing_required_arg) {
    Server server("test", 80);
    server.addPrompt("greet", "Greeting", {
        MCPPromptArgument{"name", "Person", true}
    }, [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> { return {}; });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"greet","arguments":{}}})");
    ASSERT(result.indexOf("Missing required argument") >= 0);
}

TEST(integ_pagination) {
    Server server("test", 80);
    server.setPageSize(2);
    for (int i = 0; i < 5; i++) {
        String name = "tool" + String(i);
        server.addTool(name.c_str(), "Test", R"({"type":"object"})",
            [](const JsonObject&) -> String { return "ok"; });
    }

    String r1 = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})");
    ASSERT(r1.indexOf("nextCursor") >= 0);
    ASSERT(r1.indexOf("tool0") >= 0);
    ASSERT(r1.indexOf("tool1") >= 0);

    String r2 = server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{"cursor":"2"}})");
    ASSERT(r2.indexOf("tool2") >= 0);
    ASSERT(r2.indexOf("tool3") >= 0);
}

TEST(integ_resource_subscribe_unsubscribe) {
    Server server("test", 80);
    server.addResource("test://temp", "Temp", "Current", "text/plain",
        []() -> String { return "22.5"; });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/subscribe","params":{"uri":"test://temp"}})");
    server.notifyResourceUpdated("test://temp");
    ASSERT_EQ((int)server._pendingNotifications.size(), 1);
    ASSERT(server._pendingNotifications[0].indexOf("resources/updated") >= 0);

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"resources/unsubscribe","params":{"uri":"test://temp"}})");
    server._pendingNotifications.clear();
    server.notifyResourceUpdated("test://temp");
    ASSERT_EQ((int)server._pendingNotifications.size(), 0);
}

TEST(integ_dynamic_tool_add_remove) {
    Server server("test", 80);
    server.addTool("temp", "Temperature", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "22.5"; });

    String r1 = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    ASSERT(r1.indexOf("temp") >= 0);

    ASSERT(server.removeTool("temp"));
    server.notifyToolsChanged();
    ASSERT_EQ((int)server._pendingNotifications.size(), 1);

    String r2 = server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})");
    ASSERT(r2.indexOf("temp") < 0);
}

TEST(integ_rich_tool_handler) {
    Server server("test", 80);
    server.addRichTool("capture", "Photo", R"({"type":"object"})",
        [](const JsonObject&) -> MCPToolResult {
            MCPToolResult r;
            r.add(MCPContent::makeText("Photo captured"));
            r.add(MCPContent::makeImage("data:image/png;base64,iVBOR...", "image/png"));
            return r;
        });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"capture","arguments":{}}})");
    ASSERT(result.indexOf("Photo captured") >= 0);
    ASSERT(result.indexOf("image") >= 0);
}

TEST(integ_tool_exception_returns_error) {
    Server server("test", 80);
    server.addTool("crashy", "Might crash", R"({"type":"object"})",
        [](const JsonObject&) -> String {
            throw std::runtime_error("boom");
            return "never";
        });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"crashy","arguments":{}}})");
    ASSERT(result.indexOf("isError") >= 0 || result.indexOf("Internal tool error") >= 0);
}

TEST(integ_logging_set_level) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"logging/setLevel","params":{"level":"debug"}})");
    ASSERT(result.indexOf("error") < 0);
}

TEST(integ_logging_missing_param) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"logging/setLevel","params":{}})");
    ASSERT(result.indexOf("Missing level") >= 0);
}

TEST(integ_resource_template_list) {
    Server server("test", 80);
    server.addResourceTemplate("sensor://{id}/reading", "Sensor", "Read", "application/json",
        [](const std::map<String, String>&) -> String { return "{}"; });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/templates/list"})");
    ASSERT(result.indexOf("sensor://{id}/reading") >= 0);
}

TEST(integ_resource_template_read) {
    Server server("test", 80);
    server.addResourceTemplate("sensor://{id}/reading", "Sensor", "Read", "application/json",
        [](const std::map<String, String>& vars) -> String {
            auto it = vars.find("id");
            return R"({"id":")" + (it != vars.end() ? it->second : "?") + R"("})";
        });

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"sensor://temp1/reading"}})");
    ASSERT(result.indexOf("temp1") >= 0);
}

TEST(integ_server_stop_clears_state) {
    Server server("test", 80);
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    ASSERT(server._initialized);
    ASSERT(!server._sessionId.isEmpty());
    server.stop();
    ASSERT(!server._initialized);
    ASSERT(server._sessionId.isEmpty());
}

TEST(integ_name_and_port) {
    Server server("my-esp32", 8080);
    ASSERT_STR_EQ(server.getName(), "my-esp32");
    ASSERT_EQ(server.getPort(), 8080);
}

TEST(integ_set_endpoint) {
    Server server("test", 80);
    server.setEndpoint("/api/mcp");
    ASSERT_STR_EQ(server._endpoint, "/api/mcp");
}

TEST(integ_set_mdns) {
    Server server("test", 80);
    ASSERT(server._mdnsEnabled);
    server.setMDNS(false);
    ASSERT(!server._mdnsEnabled);
}

TEST(integ_version_constants) {
    ASSERT(strlen(MCPD_VERSION) > 0);
    ASSERT(strlen(MCPD_MCP_PROTOCOL_VERSION) > 0);
    ASSERT(strchr(MCPD_VERSION, '.') != nullptr);
    ASSERT(strchr(MCPD_MCP_PROTOCOL_VERSION, '-') != nullptr);
}

TEST(integ_roots_list) {
    Server server("test", 80);
    server.addRoot("file:///workspace", "Workspace");
    server.addRoot("file:///config", "Config");

    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"roots/list"})");
    ASSERT(result.indexOf("file:///workspace") >= 0);
    ASSERT(result.indexOf("file:///config") >= 0);
}

TEST(integ_completion_missing_ref) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{}})");
    ASSERT(result.indexOf("Missing ref") >= 0);
}

TEST(integ_completion_unknown_ref_type) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{"ref":{"type":"ref/unknown"},"argument":{"name":"x","value":"y"}}})");
    ASSERT(result.indexOf("Unknown ref type") >= 0);
}

TEST(integ_remove_nonexistent_tool) {
    Server server("test", 80);
    ASSERT(!server.removeTool("nonexistent"));
}

TEST(integ_remove_nonexistent_resource) {
    Server server("test", 80);
    ASSERT(!server.removeResource("nonexistent://x"));
}

TEST(integ_remove_nonexistent_prompt) {
    Server server("test", 80);
    ASSERT(!server.removePrompt("nonexistent"));
}

TEST(integ_remove_nonexistent_root) {
    Server server("test", 80);
    ASSERT(!server.removeRoot("nonexistent://x"));
}

TEST(integ_remove_nonexistent_template) {
    Server server("test", 80);
    ASSERT(!server.removeResourceTemplate("nonexistent://{x}"));
}

TEST(integ_notify_resources_changed) {
    Server server("test", 80);
    server.notifyResourcesChanged();
    ASSERT_EQ((int)server._pendingNotifications.size(), 1);
    ASSERT(server._pendingNotifications[0].indexOf("resources/list_changed") >= 0);
}

TEST(integ_notify_prompts_changed) {
    Server server("test", 80);
    server.notifyPromptsChanged();
    ASSERT_EQ((int)server._pendingNotifications.size(), 1);
    ASSERT(server._pendingNotifications[0].indexOf("prompts/list_changed") >= 0);
}

TEST(integ_empty_body_parse_error) {
    Server server("test", 80);
    String result = server._processJsonRpc("");
    // Empty string → parse error
    ASSERT(result.indexOf("Parse error") >= 0 || result.indexOf("error") >= 0);
}

TEST(integ_cancelled_notification) {
    Server server("test", 80);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":"req-42"}})");
    ASSERT(result.isEmpty());
}

TEST(integ_session_manager_defaults) {
    Server server("test", 80);
    server.setMaxSessions(8);
    server.setSessionTimeout(60000);
    // Should not crash
    ASSERT(true);
}

TEST(integ_rate_limiter_in_dispatch) {
    Server server("test", 80);
    server.setRateLimit(100.0f, 10);
    ASSERT(server.rateLimiter().isEnabled());
}

TEST(integ_heap_monitor_accessible) {
    Server server("test", 80);
    // Should not crash
    bool low = server.heap().isLow();
    (void)low;
    ASSERT(true);
}

TEST(integ_initialize_with_rate_limit_info) {
    Server server("test", 80);
    server.setRateLimit(10.0f, 5);
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    ASSERT(result.indexOf("rateLimit") >= 0);
}

TEST(integ_initialize_without_capabilities_when_empty) {
    Server server("test", 80);
    // No tools, resources, or prompts registered
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    // Should still have logging and sampling
    ASSERT(result.indexOf("logging") >= 0);
    ASSERT(result.indexOf("sampling") >= 0);
}

// ════════════════════════════════════════════════════════════════════════
// Tool Call Hooks Tests
// ════════════════════════════════════════════════════════════════════════

TEST(hook_before_tool_call_allows) {
    Server server("test", 80);
    server.addTool("my_tool", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "ok"; });

    bool hookCalled = false;
    server.onBeforeToolCall([&](const Server::ToolCallContext& ctx) -> bool {
        hookCalled = true;
        ASSERT_STR_EQ(ctx.toolName.c_str(), "my_tool");
        return true;  // allow
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"my_tool","arguments":{}}})");
    ASSERT(hookCalled);
    ASSERT(result.indexOf("ok") >= 0);
    ASSERT(result.indexOf("error") < 0);
}

TEST(hook_before_tool_call_rejects) {
    Server server("test", 80);
    server.addTool("restricted", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "should not reach"; });

    server.onBeforeToolCall([](const Server::ToolCallContext&) -> bool {
        return false;  // reject
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"restricted","arguments":{}}})");
    ASSERT(result.indexOf("Tool call rejected") >= 0);
    ASSERT(result.indexOf("should not reach") < 0);
}

TEST(hook_after_tool_call_receives_context) {
    Server server("test", 80);
    server.addTool("timed_tool", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "result"; });

    String capturedName;
    bool capturedIsError = true;
    server.onAfterToolCall([&](const Server::ToolCallContext& ctx) {
        capturedName = ctx.toolName;
        capturedIsError = ctx.isError;
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"timed_tool","arguments":{}}})");
    ASSERT_STR_EQ(capturedName.c_str(), "timed_tool");
    ASSERT(!capturedIsError);
}

TEST(hook_after_tool_call_detects_error) {
    Server server("test", 80);
    server.addTool("bad_tool", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { throw 42; return ""; });

    bool capturedIsError = false;
    server.onAfterToolCall([&](const Server::ToolCallContext& ctx) {
        capturedIsError = ctx.isError;
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"bad_tool","arguments":{}}})");
    ASSERT(capturedIsError);
}

TEST(hook_before_and_after_both_called) {
    Server server("test", 80);
    server.addTool("dual_hook", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "ok"; });

    int callOrder = 0;
    int beforeOrder = 0, afterOrder = 0;

    server.onBeforeToolCall([&](const Server::ToolCallContext&) -> bool {
        beforeOrder = ++callOrder;
        return true;
    });
    server.onAfterToolCall([&](const Server::ToolCallContext&) {
        afterOrder = ++callOrder;
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"dual_hook","arguments":{}}})");
    ASSERT_EQ(beforeOrder, 1);
    ASSERT_EQ(afterOrder, 2);
}

TEST(hook_before_reject_skips_after) {
    Server server("test", 80);
    server.addTool("skip_after", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "ok"; });

    bool afterCalled = false;

    server.onBeforeToolCall([](const Server::ToolCallContext&) -> bool {
        return false;  // reject
    });
    server.onAfterToolCall([&](const Server::ToolCallContext&) {
        afterCalled = true;
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"skip_after","arguments":{}}})");
    ASSERT(!afterCalled);
}

TEST(hook_no_hooks_set_works_normally) {
    Server server("test", 80);
    server.addTool("normal_tool", "desc", R"({"type":"object"})",
        [](const JsonObject&) -> String { return "normal"; });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    String result = server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"normal_tool","arguments":{}}})");
    ASSERT(result.indexOf("normal") >= 0);
}

TEST(hook_before_receives_tool_args) {
    Server server("test", 80);
    server.addTool("args_tool", "desc", R"({"type":"object","properties":{"name":{"type":"string"}}})",
        [](const JsonObject& args) -> String { return args["name"].as<String>(); });

    String capturedArg;
    server.onBeforeToolCall([&](const Server::ToolCallContext& ctx) -> bool {
        if (ctx.args) {
            capturedArg = (*ctx.args)["name"].as<String>();
        }
        return true;
    });

    server._processJsonRpc(R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","clientInfo":{"name":"test"}}})");
    server._processJsonRpc(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"args_tool","arguments":{"name":"hello"}}})");
    ASSERT_STR_EQ(capturedArg.c_str(), "hello");
}

// ════════════════════════════════════════════════════════════════════════
// JSON-RPC Error With Data Tests
// ════════════════════════════════════════════════════════════════════════

TEST(error_with_data_includes_data_field) {
    Server server("test", 80);
    String err = server.jsonRpcErrorWithData(JsonVariant(), -32000, "Custom error",
        R"({"detail":"something broke","code":42})");
    ASSERT(err.indexOf("Custom error") >= 0);
    ASSERT(err.indexOf("something broke") >= 0);
    ASSERT(err.indexOf("42") >= 0);
}

TEST(error_with_data_empty_data) {
    Server server("test", 80);
    String err = server.jsonRpcErrorWithData(JsonVariant(), -32000, "No data", "");
    ASSERT(err.indexOf("No data") >= 0);
    // Should not have a data field
    ASSERT(err.indexOf("\"data\"") < 0);
}

TEST(error_with_data_preserves_id) {
    Server server("test", 80);
    JsonDocument doc;
    doc["id"] = 42;
    String err = server.jsonRpcErrorWithData(doc["id"], -32602, "Bad params",
        R"({"missing":"field_x"})");
    ASSERT(err.indexOf("42") >= 0);
    ASSERT(err.indexOf("Bad params") >= 0);
    ASSERT(err.indexOf("field_x") >= 0);
}

// ════════════════════════════════════════════════════════════════════════

int main() {
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
