/**
 * mcpd — Module-level tests
 *
 * Tests for: Progress/RequestTracker, Transport utilities,
 * MCPToolResult serialization, MCPTool builder, MCPPrompt,
 * MCPResource, MCPResourceTemplate, MCPElicitation, MCPSampling,
 * MCPRoot, Logging, and Server extended tests.
 *
 * 82 tests covering modules complementing test_infrastructure.cpp.
 */

#include "test_framework.h"
#include "arduino_mock.h"
#include "mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// Helper to create a test server
static Server* makeServer() {
    static Server* s = nullptr;
    if (s) delete s;
    s = new Server("module-test", 8080);
    return s;
}

// ═══════════════════════════════════════════════════════════════════════
// Progress / RequestTracker Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(progress_notification_to_json) {
    ProgressNotification pn;
    pn.progressToken = "tok-1";
    pn.progress = 50;
    pn.total = 100;
    pn.message = "Halfway there";

    String json = pn.toJsonRpc();
    ASSERT_STR_CONTAINS(json.c_str(), "\"jsonrpc\":\"2.0\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"notifications/progress\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"progressToken\":\"tok-1\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"Halfway there\"");
}

TEST(progress_notification_no_total) {
    ProgressNotification pn;
    pn.progressToken = "t1";
    pn.progress = 10;
    pn.total = 0;

    String json = pn.toJsonRpc();
    ASSERT(json.indexOf("\"total\"") < 0);
    ASSERT(json.indexOf("\"message\"") < 0);
}

TEST(progress_notification_no_message) {
    ProgressNotification pn;
    pn.progressToken = "t2";
    pn.progress = 75;
    pn.total = 100;

    String json = pn.toJsonRpc();
    ASSERT(json.indexOf("\"message\"") < 0);
}

TEST(progress_notification_fractional) {
    ProgressNotification pn;
    pn.progressToken = "t3";
    pn.progress = 33.3;
    pn.total = 100;
    pn.message = "One third";

    String json = pn.toJsonRpc();
    ASSERT_STR_CONTAINS(json.c_str(), "One third");
}

TEST(request_tracker_empty) {
    RequestTracker rt;
    ASSERT(!rt.hasInFlight());
    ASSERT_EQ((int)rt.inFlightCount(), 0);
}

TEST(request_tracker_track_and_complete) {
    RequestTracker rt;
    rt.trackRequest("req-1", "prog-1");
    ASSERT(rt.hasInFlight());
    ASSERT_EQ((int)rt.inFlightCount(), 1);
    rt.completeRequest("req-1");
    ASSERT(!rt.hasInFlight());
}

TEST(request_tracker_cancel) {
    RequestTracker rt;
    rt.trackRequest("req-1");
    ASSERT(rt.cancelRequest("req-1"));
    ASSERT(rt.isCancelled("req-1"));
    ASSERT(!rt.hasInFlight());
}

TEST(request_tracker_cancel_nonexistent) {
    RequestTracker rt;
    ASSERT(!rt.cancelRequest("nope"));
}

TEST(request_tracker_is_cancelled_false) {
    RequestTracker rt;
    ASSERT(!rt.isCancelled("req-1"));
}

TEST(request_tracker_clear_cancelled) {
    RequestTracker rt;
    rt.trackRequest("req-1");
    rt.cancelRequest("req-1");
    ASSERT(rt.isCancelled("req-1"));
    rt.clearCancelled("req-1");
    ASSERT(!rt.isCancelled("req-1"));
}

TEST(request_tracker_clear_cancelled_nonexistent) {
    RequestTracker rt;
    rt.clearCancelled("nope");
    ASSERT(!rt.isCancelled("nope"));
}

TEST(request_tracker_multiple_requests) {
    RequestTracker rt;
    rt.trackRequest("a");
    rt.trackRequest("b");
    rt.trackRequest("c");
    ASSERT_EQ((int)rt.inFlightCount(), 3);
    rt.completeRequest("b");
    ASSERT_EQ((int)rt.inFlightCount(), 2);
    rt.cancelRequest("a");
    ASSERT_EQ((int)rt.inFlightCount(), 1);
    ASSERT(rt.isCancelled("a"));
    ASSERT(!rt.isCancelled("b"));
    ASSERT(!rt.isCancelled("c"));
}

TEST(request_tracker_complete_nonexistent) {
    RequestTracker rt;
    rt.completeRequest("nope");
    ASSERT(!rt.hasInFlight());
}

TEST(request_tracker_track_with_progress_token) {
    RequestTracker rt;
    rt.trackRequest("req-1", "progress-token-abc");
    ASSERT(rt.hasInFlight());
    ASSERT_EQ((int)rt.inFlightCount(), 1);
}

TEST(request_tracker_cancel_then_track_same_id) {
    RequestTracker rt;
    rt.trackRequest("req-1");
    rt.cancelRequest("req-1");
    ASSERT(rt.isCancelled("req-1"));
    rt.trackRequest("req-1");
    ASSERT(rt.hasInFlight());
}

// ═══════════════════════════════════════════════════════════════════════
// Transport Utilities Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(transport_content_type_json) {
    ASSERT_EQ(String(transport::CONTENT_TYPE_JSON), String("application/json"));
}

TEST(transport_content_type_sse) {
    ASSERT_EQ(String(transport::CONTENT_TYPE_SSE), String("text/event-stream"));
}

TEST(transport_header_session_id) {
    ASSERT_EQ(String(transport::HEADER_SESSION_ID), String("Mcp-Session-Id"));
}

TEST(transport_header_accept) {
    ASSERT_EQ(String(transport::HEADER_ACCEPT), String("Accept"));
}

TEST(transport_header_content_type) {
    ASSERT_EQ(String(transport::HEADER_CONTENT_TYPE), String("Content-Type"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPToolResult Serialization Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(tool_result_text) {
    auto r = MCPToolResult::text("hello");
    ASSERT_EQ((int)r.content.size(), 1);
    ASSERT(!r.isError);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT_EQ(String((const char*)obj["content"][0]["type"]), String("text"));
    ASSERT_EQ(String((const char*)obj["content"][0]["text"]), String("hello"));
}

TEST(tool_result_error) {
    auto r = MCPToolResult::error("broke");
    ASSERT(r.isError);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT(obj["isError"].as<bool>());
}

TEST(tool_result_image_with_alt) {
    auto r = MCPToolResult::image("AAAA", "image/png", "pic");
    ASSERT_EQ((int)r.content.size(), 2);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT_EQ(String((const char*)obj["content"][1]["type"]), String("image"));
    ASSERT_EQ(String((const char*)obj["content"][1]["data"]), String("AAAA"));
}

TEST(tool_result_image_no_alt) {
    auto r = MCPToolResult::image("BBBB", "image/jpeg");
    ASSERT_EQ((int)r.content.size(), 1);
}

TEST(tool_result_audio_with_desc) {
    auto r = MCPToolResult::audio("CCCC", "audio/wav", "sound");
    ASSERT_EQ((int)r.content.size(), 2);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT_EQ(String((const char*)obj["content"][1]["type"]), String("audio"));
}

TEST(tool_result_audio_no_desc) {
    auto r = MCPToolResult::audio("DDDD", "audio/mp3");
    ASSERT_EQ((int)r.content.size(), 1);
}

TEST(tool_result_add_multi) {
    MCPToolResult r;
    r.add(MCPContent::makeText("line 1"));
    r.add(MCPContent::makeText("line 2"));
    r.add(MCPContent::makeImage("img", "image/png"));
    ASSERT_EQ((int)r.content.size(), 3);
}

TEST(tool_result_not_error_by_default) {
    MCPToolResult r;
    ASSERT(!r.isError);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT(!obj.containsKey("isError"));
}

TEST(tool_result_empty_content) {
    MCPToolResult r;
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT(obj.containsKey("content"));
    ASSERT_EQ((int)obj["content"].size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════
// MCPTool Builder Pattern Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(tool_mark_readonly) {
    MCPTool tool("test", "desc", "{}", nullptr);
    tool.markReadOnly();
    ASSERT(tool.annotations.readOnlyHint);
    ASSERT(!tool.annotations.destructiveHint);
    ASSERT(tool.annotations.hasAnnotations);
}

TEST(tool_mark_idempotent) {
    MCPTool tool("test", "desc", "{}", nullptr);
    tool.markIdempotent();
    ASSERT(tool.annotations.idempotentHint);
}

TEST(tool_mark_local_only) {
    MCPTool tool("test", "desc", "{}", nullptr);
    tool.markLocalOnly();
    ASSERT(!tool.annotations.openWorldHint);
}

TEST(tool_builder_chain) {
    MCPTool tool("test", "desc", "{}", nullptr);
    tool.markReadOnly().markIdempotent().markLocalOnly();
    ASSERT(tool.annotations.readOnlyHint);
    ASSERT(!tool.annotations.destructiveHint);
    ASSERT(tool.annotations.idempotentHint);
    ASSERT(!tool.annotations.openWorldHint);
}

TEST(tool_annotate_method) {
    MCPTool tool("test", "desc", "{}", nullptr);
    MCPToolAnnotations ann;
    ann.title = "My Tool";
    ann.setReadOnlyHint(true);
    tool.annotate(ann);
    ASSERT_EQ(tool.annotations.title, String("My Tool"));
    ASSERT(tool.annotations.readOnlyHint);
}

TEST(tool_to_json_input_schema) {
    MCPTool tool("echo", "Echo tool",
        R"({"type":"object","properties":{"msg":{"type":"string"}}})", nullptr);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    ASSERT_EQ(String((const char*)obj["name"]), String("echo"));
    ASSERT_EQ(String((const char*)obj["inputSchema"]["type"]), String("object"));
}

TEST(tool_default_annotations_not_serialized) {
    MCPTool tool("test", "desc", "{}", nullptr);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    ASSERT(!obj.containsKey("annotations"));
}

TEST(tool_annotations_spec_defaults) {
    MCPToolAnnotations ann;
    ASSERT(!ann.readOnlyHint);
    ASSERT(ann.destructiveHint);  // default true per spec
    ASSERT(!ann.idempotentHint);
    ASSERT(ann.openWorldHint);    // default true per spec
    ASSERT(!ann.hasAnnotations);
}

// ═══════════════════════════════════════════════════════════════════════
// MCPResource Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(resource_construction) {
    MCPResource res;
    res.uri = "device://sensors/temp";
    res.name = "Temperature";
    res.description = "Current temperature";
    res.mimeType = "application/json";
    ASSERT_EQ(res.uri, String("device://sensors/temp"));
}

TEST(resource_to_json) {
    MCPResource res;
    res.uri = "device://info";
    res.name = "Info";
    res.description = "Device info";
    res.mimeType = "text/plain";
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    res.toJson(obj);
    ASSERT_EQ(String((const char*)obj["uri"]), String("device://info"));
    ASSERT_EQ(String((const char*)obj["name"]), String("Info"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPResourceTemplate Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(resource_template_construction) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://sensors/{sensorId}";
    tmpl.name = "Sensor";
    ASSERT_EQ(tmpl.uriTemplate, String("device://sensors/{sensorId}"));
}

TEST(resource_template_to_json) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://gpio/{pin}";
    tmpl.name = "GPIO";
    tmpl.description = "GPIO pin";
    tmpl.mimeType = "application/json";
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tmpl.toJson(obj);
    ASSERT_EQ(String((const char*)obj["uriTemplate"]), String("device://gpio/{pin}"));
}

TEST(resource_template_match_simple) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://sensors/{id}";
    std::map<String, String> vars;
    ASSERT(tmpl.match("device://sensors/temp1", vars));
    ASSERT_EQ(vars["id"], String("temp1"));
}

TEST(resource_template_match_multiple_vars) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://{type}/{id}";
    std::map<String, String> vars;
    ASSERT(tmpl.match("device://sensors/temp1", vars));
    ASSERT_EQ(vars["type"], String("sensors"));
    ASSERT_EQ(vars["id"], String("temp1"));
}

TEST(resource_template_no_match) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://sensors/{id}";
    std::map<String, String> vars;
    ASSERT(!tmpl.match("device://gpio/5", vars));
}

TEST(resource_template_exact_match_no_vars) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://info";
    std::map<String, String> vars;
    ASSERT(tmpl.match("device://info", vars));
    ASSERT(vars.empty());
}

// ═══════════════════════════════════════════════════════════════════════
// MCPPrompt Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(prompt_construction) {
    MCPPrompt prompt;
    prompt.name = "greet";
    prompt.description = "Greet someone";
    MCPPromptArgument arg;
    arg.name = "name";
    arg.description = "Person to greet";
    arg.required = true;
    prompt.arguments.push_back(arg);
    ASSERT_EQ(prompt.name, String("greet"));
    ASSERT_EQ((int)prompt.arguments.size(), 1);
    ASSERT(prompt.arguments[0].required);
}

TEST(prompt_to_json) {
    MCPPrompt prompt;
    prompt.name = "analyze";
    prompt.description = "Analyze data";
    MCPPromptArgument arg;
    arg.name = "data";
    arg.description = "Data to analyze";
    arg.required = false;
    prompt.arguments.push_back(arg);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    prompt.toJson(obj);
    ASSERT_EQ(String((const char*)obj["name"]), String("analyze"));
    ASSERT_EQ((int)obj["arguments"].size(), 1);
}

TEST(prompt_message_text) {
    MCPPromptMessage msg("user", "Hello!");
    ASSERT_EQ(msg.role, String("user"));
    ASSERT_EQ(msg.text, String("Hello!"));
}

TEST(prompt_no_arguments) {
    MCPPrompt prompt;
    prompt.name = "default";
    prompt.description = "Default prompt";
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    prompt.toJson(obj);
    ASSERT_EQ(String((const char*)obj["name"]), String("default"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPRoot Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(root_construction) {
    MCPRoot root("file:///data", "Data directory");
    ASSERT_EQ(root.uri, String("file:///data"));
    ASSERT_EQ(root.name, String("Data directory"));
}

TEST(root_to_json) {
    MCPRoot root("file:///sensors", "Sensor data");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    root.toJson(obj);
    ASSERT_EQ(String((const char*)obj["uri"]), String("file:///sensors"));
    ASSERT_EQ(String((const char*)obj["name"]), String("Sensor data"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPElicitation Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(elicitation_field_basic) {
    MCPElicitationField f;
    f.name = "username";
    f.title = "Username";
    f.description = "Enter username";
    f.type = "string";
    ASSERT_EQ(f.name, String("username"));
    ASSERT_EQ(f.type, String("string"));
}

TEST(elicitation_field_number_range) {
    MCPElicitationField f;
    f.name = "temp";
    f.type = "number";
    f.minimum = 15.0;
    f.maximum = 30.0;
    f.hasMinimum = true;
    f.hasMaximum = true;
    ASSERT_EQ(f.minimum, 15.0);
    ASSERT_EQ(f.maximum, 30.0);
}

TEST(elicitation_field_enum_options) {
    MCPElicitationField f;
    f.name = "color";
    f.type = "string";
    f.enumValues.push_back("red");
    f.enumValues.push_back("green");
    f.enumValues.push_back("blue");
    ASSERT_EQ((int)f.enumValues.size(), 3);
}

TEST(elicitation_field_to_json_schema) {
    MCPElicitationField f;
    f.name = "mode";
    f.title = "Mode";
    f.type = "string";
    f.enumValues.push_back("auto");
    f.enumValues.push_back("manual");

    JsonDocument doc;
    JsonObject props = doc["properties"].to<JsonObject>();
    JsonArray req = doc["required"].to<JsonArray>();
    f.toJsonSchema(props, req);

    ASSERT(props.containsKey("mode"));
    ASSERT_EQ(String((const char*)props["mode"]["type"]), String("string"));
}

TEST(elicitation_field_required) {
    MCPElicitationField f;
    f.name = "required_field";
    f.type = "string";
    f.required = true;

    JsonDocument doc;
    JsonObject props = doc["properties"].to<JsonObject>();
    JsonArray req = doc["required"].to<JsonArray>();
    f.toJsonSchema(props, req);
    ASSERT_EQ((int)req.size(), 1);
}

TEST(elicitation_field_boolean) {
    MCPElicitationField f;
    f.name = "confirm";
    f.type = "boolean";
    f.title = "Confirm?";

    JsonDocument doc;
    JsonObject props = doc["properties"].to<JsonObject>();
    JsonArray req = doc["required"].to<JsonArray>();
    f.toJsonSchema(props, req);
    ASSERT_EQ(String((const char*)props["confirm"]["type"]), String("boolean"));
}

TEST(elicitation_request_construction) {
    MCPElicitationRequest er;
    er.message = "Please fill in the form";
    MCPElicitationField f;
    f.name = "name";
    f.type = "string";
    er.fields.push_back(f);
    ASSERT_EQ((int)er.fields.size(), 1);
    ASSERT_EQ(er.message, String("Please fill in the form"));
}

TEST(elicitation_response_action) {
    MCPElicitationResponse resp;
    resp.action = "decline";
    ASSERT_EQ(resp.action, String("decline"));
}

TEST(elicitation_response_getString) {
    MCPElicitationResponse resp;
    resp.action = "accept";
    resp.contentDoc["name"] = "Alice";
    ASSERT_EQ(resp.getString("name"), String("Alice"));
}

TEST(elicitation_response_getString_declined) {
    MCPElicitationResponse resp;
    resp.action = "decline";
    ASSERT_EQ(resp.getString("name"), String(""));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPSampling Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(sampling_request_basic) {
    MCPSamplingRequest req;
    req.addUserMessage("What is the temperature?");
    req.maxTokens = 100;
    ASSERT_EQ((int)req.messages.size(), 1);
    ASSERT_EQ(req.maxTokens, 100);
}

TEST(sampling_request_system_prompt) {
    MCPSamplingRequest req;
    req.systemPrompt = "You are a sensor assistant";
    req.addUserMessage("Read sensor");
    ASSERT_EQ(req.systemPrompt, String("You are a sensor assistant"));
}

TEST(sampling_request_model_hints) {
    MCPSamplingRequest req;
    req.addUserMessage("Hello");
    MCPModelHint hint;
    hint.name = "claude-3-5-sonnet";
    req.modelPreferences.hints.push_back(hint);
    ASSERT_EQ((int)req.modelPreferences.hints.size(), 1);
}

TEST(sampling_request_to_json) {
    MCPSamplingRequest req;
    req.addUserMessage("Test");
    req.maxTokens = 50;
    req.systemPrompt = "Be helpful";

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    req.toJson(obj);

    ASSERT(obj.containsKey("messages"));
    ASSERT_EQ((int)obj["maxTokens"], 50);
    ASSERT_EQ(String((const char*)obj["systemPrompt"]), String("Be helpful"));
}

TEST(sampling_request_add_assistant_message) {
    MCPSamplingRequest req;
    req.addUserMessage("Hello");
    req.addAssistantMessage("Hi there!");
    ASSERT_EQ((int)req.messages.size(), 2);
    ASSERT_EQ(req.messages[0].role, String("user"));
    ASSERT_EQ(req.messages[1].role, String("assistant"));
}

TEST(sampling_message_construction) {
    MCPSamplingMessage msg("user", "test content");
    ASSERT_EQ(msg.role, String("user"));
    ASSERT_EQ(msg.text, String("test content"));
}

TEST(sampling_response_construction) {
    MCPSamplingResponse resp;
    resp.role = "assistant";
    resp.text = "The temperature is 22C";
    resp.model = "claude-3-5-sonnet";
    ASSERT_EQ(resp.role, String("assistant"));
    ASSERT_EQ(resp.text, String("The temperature is 22C"));
}

TEST(sampling_request_stop_sequences) {
    MCPSamplingRequest req;
    req.addUserMessage("test");
    req.stopSequences.push_back("END");
    req.stopSequences.push_back("STOP");
    ASSERT_EQ((int)req.stopSequences.size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════
// Version & Constants Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(version_format_semver) {
    String ver = MCPD_VERSION;
    int dotCount = 0;
    for (size_t i = 0; i < ver.length(); i++) {
        if (ver[i] == '.') dotCount++;
    }
    ASSERT_EQ(dotCount, 2);
}

TEST(protocol_version_date_format) {
    String pv = MCPD_MCP_PROTOCOL_VERSION;
    ASSERT_EQ((int)pv.length(), 10);
    ASSERT_EQ(pv[4], '-');
    ASSERT_EQ(pv[7], '-');
}

// ═══════════════════════════════════════════════════════════════════════
// MCPLogging Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(logging_level_roundtrip) {
    LogLevel levels[] = {LogLevel::DEBUG, LogLevel::INFO, LogLevel::NOTICE,
                         LogLevel::WARNING, LogLevel::ERROR, LogLevel::CRITICAL,
                         LogLevel::ALERT, LogLevel::EMERGENCY};
    for (auto lv : levels) {
        ASSERT_EQ(logLevelFromString(logLevelToString(lv)), lv);
    }
}

TEST(logging_set_and_get_level) {
    Logging log;
    log.setLevel(LogLevel::DEBUG);
    ASSERT_EQ(log.getLevel(), LogLevel::DEBUG);
    log.setLevel(LogLevel::EMERGENCY);
    ASSERT_EQ(log.getLevel(), LogLevel::EMERGENCY);
}

TEST(logging_sink_receives) {
    Logging log;
    log.setLevel(LogLevel::DEBUG);
    String captured;
    log.setSink([&captured](const String& n) { captured = n; });
    log.info("test", "hello");
    ASSERT_STR_CONTAINS(captured.c_str(), "hello");
}

TEST(logging_default_level_is_warning) {
    Logging log;
    ASSERT_EQ(log.getLevel(), LogLevel::WARNING);
}

// ═══════════════════════════════════════════════════════════════════════
// Server Extended Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(server_get_name) {
    auto* s = makeServer();
    ASSERT_EQ(String(s->getName()), String("module-test"));
}

TEST(server_get_port) {
    Server s("test-device", 9090);
    ASSERT_EQ((int)s.getPort(), 9090);
}

TEST(server_default_port) {
    Server s("test-device");
    ASSERT_EQ((int)s.getPort(), 80);
}

TEST(server_add_tool_with_handler) {
    auto* s = makeServer();
    s->addTool("test_tool", "A test tool",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String { return "ok"; });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "test_tool");
}

TEST(server_remove_tool) {
    auto* s = makeServer();
    s->addTool("temp", "temp tool", "{}", nullptr);
    s->removeTool("temp");
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT(resp.indexOf("\"temp\"") < 0);
}

TEST(server_add_resource_template) {
    auto* s = makeServer();
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "device://gpio/{pin}";
    tmpl.name = "GPIO Pin";
    tmpl.description = "Access GPIO pin";
    tmpl.mimeType = "application/json";
    tmpl.handler = [](const std::map<String, String>&) -> String { return "{}"; };
    s->addResourceTemplate(tmpl);
    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/templates/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "device://gpio/{pin}");
}

TEST(server_add_root) {
    auto* s = makeServer();
    s->addRoot("file:///data", "Data");
    String req = R"({"jsonrpc":"2.0","id":1,"method":"roots/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "file:///data");
}

TEST(server_on_initialize_callback) {
    auto* s = makeServer();
    bool initCalled = false;
    s->onInitialize([&initCalled](const String&) { initCalled = true; });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
    s->_processJsonRpc(req);
    ASSERT(initCalled);
}

TEST(server_pagination_tools) {
    auto* s = makeServer();
    s->setPageSize(2);
    for (int i = 0; i < 5; i++) {
        String name = "tool_" + String(i);
        s->addTool(name.c_str(), "desc", "{}", nullptr);
    }
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "nextCursor");
}

TEST(server_completion_via_jsonrpc) {
    auto* s = makeServer();
    s->addPrompt("greet", "Greet someone",
        {{"name", "Person name", true}},
        [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> {
            return {};
        });
    s->completions().addPromptCompletion("greet", "name",
        [](const String&, const String&) -> std::vector<String> {
            return {"Alice", "Bob"};
        });
    String req = R"({"jsonrpc":"2.0","id":1,"method":"completion/complete","params":{"ref":{"type":"ref/prompt","name":"greet"},"argument":{"name":"name","value":"A"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Alice");
}

TEST(server_multiple_resources) {
    auto* s = makeServer();
    s->addResource("device://temp", "Temperature", "Temp sensor", "application/json",
        []() -> String { return "{\"temp\":22}"; });
    s->addResource("device://hum", "Humidity", "Hum sensor", "application/json",
        []() -> String { return "{\"hum\":55}"; });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/list"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "device://temp");
    ASSERT_STR_CONTAINS(resp.c_str(), "device://hum");
}

TEST(server_resource_read) {
    auto* s = makeServer();
    s->addResource("device://readtest", "ReadTest", "Test", "text/plain",
        []() -> String { return "sensor_value_42"; });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"device://readtest"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sensor_value_42");
}

// ═══════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════

int main() {
    printf("\n══════════════════════════════════════════\n");
    printf(" mcpd Module Tests\n");
    printf("══════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
