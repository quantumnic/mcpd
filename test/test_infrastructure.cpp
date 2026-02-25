/**
 * mcpd — Infrastructure Unit Tests
 *
 * Tests for core infrastructure: Auth, Session, RateLimit, HeapMonitor,
 * Logging, Content types, Tool annotations, Progress, Completion,
 * Sampling, Elicitation, and Server integration.
 *
 * Compile: g++ -std=c++17 -I../src -I. -Imock_includes -DMCPD_TEST test_infrastructure.cpp -o test_infrastructure && ./test_infrastructure
 */

#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ═══════════════════════════════════════════════════════════════════════
// Session Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(session_create) {
    SessionManager mgr;
    String sid = mgr.createSession("test-client");
    ASSERT(!sid.isEmpty());
    ASSERT_EQ(mgr.activeCount(), (size_t)1);
}

TEST(session_validate) {
    SessionManager mgr;
    String sid = mgr.createSession("client1");
    ASSERT(mgr.validateSession(sid));
    ASSERT(!mgr.validateSession("nonexistent"));
}

TEST(session_remove) {
    SessionManager mgr;
    String sid = mgr.createSession("client1");
    ASSERT_EQ(mgr.activeCount(), (size_t)1);
    ASSERT(mgr.removeSession(sid));
    ASSERT_EQ(mgr.activeCount(), (size_t)0);
    ASSERT(!mgr.removeSession(sid));
}

TEST(session_get_info) {
    SessionManager mgr;
    String sid = mgr.createSession("my-client");
    const Session* s = mgr.getSession(sid);
    ASSERT(s != nullptr);
    ASSERT_EQ(s->clientName, String("my-client"));
    ASSERT(s->initialized);
    ASSERT(s->id == sid);
}

TEST(session_get_nonexistent) {
    SessionManager mgr;
    const Session* s = mgr.getSession("nope");
    ASSERT(s == nullptr);
}

TEST(session_max_limit_default) {
    SessionManager mgr;
    ASSERT_EQ(mgr.maxSessions(), (size_t)4);
}

TEST(session_set_max) {
    SessionManager mgr;
    mgr.setMaxSessions(2);
    ASSERT_EQ(mgr.maxSessions(), (size_t)2);

    String s1 = mgr.createSession("c1");
    String s2 = mgr.createSession("c2");
    ASSERT(!s1.isEmpty());
    ASSERT(!s2.isEmpty());
    ASSERT_EQ(mgr.activeCount(), (size_t)2);

    // Third session should evict oldest idle
    String s3 = mgr.createSession("c3");
    ASSERT(!s3.isEmpty());
    ASSERT_EQ(mgr.activeCount(), (size_t)2);
}

TEST(session_unlimited) {
    SessionManager mgr;
    mgr.setMaxSessions(0);
    for (int i = 0; i < 20; i++) {
        String sid = mgr.createSession(String("client-") + String(i));
        ASSERT(!sid.isEmpty());
    }
    ASSERT_EQ(mgr.activeCount(), (size_t)20);
}

TEST(session_idle_timeout) {
    SessionManager mgr;
    mgr.setIdleTimeout(1);  // 1ms timeout
    mgr.createSession("client");
    delay(10);
    mgr.pruneExpired();
    ASSERT_EQ(mgr.activeCount(), (size_t)0);
}

TEST(session_no_timeout) {
    SessionManager mgr;
    mgr.setIdleTimeout(0);
    mgr.createSession("client");
    mgr.pruneExpired();
    ASSERT_EQ(mgr.activeCount(), (size_t)1);
}

TEST(session_summary_json) {
    SessionManager mgr;
    mgr.createSession("test-client");
    String summary = mgr.summary();
    ASSERT_STR_CONTAINS(summary.c_str(), "activeSessions");
    ASSERT_STR_CONTAINS(summary.c_str(), "maxSessions");
    ASSERT_STR_CONTAINS(summary.c_str(), "test-client");
}

TEST(session_touch_updates_activity) {
    SessionManager mgr;
    String sid = mgr.createSession("client");
    const Session* s = mgr.getSession(sid);
    unsigned long initial = s->lastActivity;
    delay(5);
    mgr.validateSession(sid);
    s = mgr.getSession(sid);
    ASSERT(s->lastActivity >= initial);
}

// ═══════════════════════════════════════════════════════════════════════
// RateLimit Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(ratelimit_disabled_by_default) {
    RateLimiter rl;
    ASSERT(!rl.isEnabled());
    ASSERT(rl.tryAcquire());  // always allow when disabled
}

TEST(ratelimit_enable) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    ASSERT(rl.isEnabled());
    ASSERT_EQ(rl.burstCapacity(), (size_t)5);
}

TEST(ratelimit_burst_allows_initial) {
    RateLimiter rl;
    rl.configure(1.0f, 3);
    ASSERT(rl.tryAcquire());
    ASSERT(rl.tryAcquire());
    ASSERT(rl.tryAcquire());
}

TEST(ratelimit_stats_tracking) {
    RateLimiter rl;
    rl.configure(100.0f, 10);
    rl.tryAcquire();
    rl.tryAcquire();
    ASSERT_EQ(rl.totalAllowed(), (unsigned long)2);
    ASSERT_EQ(rl.totalDenied(), (unsigned long)0);
}

TEST(ratelimit_reset_stats) {
    RateLimiter rl;
    rl.configure(100.0f, 10);
    rl.tryAcquire();
    rl.resetStats();
    ASSERT_EQ(rl.totalAllowed(), (unsigned long)0);
    ASSERT_EQ(rl.totalDenied(), (unsigned long)0);
}

TEST(ratelimit_disable) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    ASSERT(rl.isEnabled());
    rl.disable();
    ASSERT(!rl.isEnabled());
}

TEST(ratelimit_available_tokens) {
    RateLimiter rl;
    rl.configure(10.0f, 5);
    ASSERT(rl.availableTokens() > 4.0f);
    rl.tryAcquire();
    ASSERT(rl.availableTokens() > 3.0f);
}

// ═══════════════════════════════════════════════════════════════════════
// HeapMonitor Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(heap_monitor_defaults) {
    HeapMonitor hm;
    ASSERT_EQ(hm.sampleCount(), (size_t)0);
    ASSERT_EQ(hm.lastFree(), (size_t)0);
    ASSERT_EQ(hm.lastTotal(), (size_t)0);
}

TEST(heap_monitor_sample_no_crash) {
    HeapMonitor hm;
    hm.sample();  // On non-ESP32, this is a no-op
}

TEST(heap_monitor_warning_threshold) {
    HeapMonitor hm;
    hm.setWarningThreshold(20480);
    // On non-ESP32, isLow() always returns false
    ASSERT(!hm.isLow());
}

TEST(heap_monitor_usage_percent_zero) {
    HeapMonitor hm;
    ASSERT(hm.usagePercent() == 0.0f);  // no total = 0%
}

// ═══════════════════════════════════════════════════════════════════════
// MCPContent Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(content_text_factory) {
    auto c = MCPContent::makeText("hello");
    ASSERT_EQ(c.type, MCPContent::TEXT);
    ASSERT_EQ(c.text, String("hello"));
}

TEST(content_image_factory) {
    auto c = MCPContent::makeImage("base64data", "image/png");
    ASSERT_EQ(c.type, MCPContent::IMAGE);
    ASSERT_EQ(c.data, String("base64data"));
    ASSERT_EQ(c.mimeType, String("image/png"));
}

TEST(content_audio_factory) {
    auto c = MCPContent::makeAudio("audiodata", "audio/wav");
    ASSERT_EQ(c.type, MCPContent::AUDIO);
    ASSERT_EQ(c.data, String("audiodata"));
    ASSERT_EQ(c.mimeType, String("audio/wav"));
}

TEST(content_resource_text_factory) {
    auto c = MCPContent::makeResource("sensor://temp", "application/json", "{\"v\":42}");
    ASSERT_EQ(c.type, MCPContent::RESOURCE);
    ASSERT_EQ(c.uri, String("sensor://temp"));
    ASSERT_EQ(c.text, String("{\"v\":42}"));
}

TEST(content_resource_blob_factory) {
    auto c = MCPContent::makeResourceBlob("file://data", "application/octet-stream", "AQID");
    ASSERT_EQ(c.type, MCPContent::RESOURCE);
    ASSERT_EQ(c.blob, String("AQID"));
}

TEST(content_text_to_json) {
    auto c = MCPContent::makeText("test");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT_EQ(String(obj["type"].as<const char*>()), String("text"));
    ASSERT_EQ(String(obj["text"].as<const char*>()), String("test"));
}

TEST(content_image_to_json) {
    auto c = MCPContent::makeImage("imgdata", "image/jpeg");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT_EQ(String(obj["type"].as<const char*>()), String("image"));
    ASSERT_EQ(String(obj["data"].as<const char*>()), String("imgdata"));
    ASSERT_EQ(String(obj["mimeType"].as<const char*>()), String("image/jpeg"));
}

TEST(content_audio_to_json) {
    auto c = MCPContent::makeAudio("wavdata", "audio/wav");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT_EQ(String(obj["type"].as<const char*>()), String("audio"));
}

TEST(content_resource_text_to_json) {
    auto c = MCPContent::makeResource("res://x", "text/plain", "hello");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT_EQ(String(obj["type"].as<const char*>()), String("resource"));
    ASSERT(obj["resource"]["uri"].as<String>() == "res://x");
    ASSERT(obj["resource"]["text"].as<String>() == "hello");
}

TEST(content_resource_blob_to_json) {
    auto c = MCPContent::makeResourceBlob("res://bin", "application/octet-stream", "DEADBEEF");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    ASSERT(obj["resource"]["blob"].as<String>() == "DEADBEEF");
}

// ═══════════════════════════════════════════════════════════════════════
// MCPToolResult Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(tool_result_text) {
    auto r = MCPToolResult::text("result");
    ASSERT_EQ(r.content.size(), (size_t)1);
    ASSERT_EQ(r.content[0].type, MCPContent::TEXT);
    ASSERT(!r.isError);
}

TEST(tool_result_error) {
    auto r = MCPToolResult::error("something broke");
    ASSERT(r.isError);
    ASSERT_EQ(r.content[0].text, String("something broke"));
}

TEST(tool_result_image_with_alt) {
    auto r = MCPToolResult::image("base64", "image/png", "A photo");
    ASSERT_EQ(r.content.size(), (size_t)2);
    ASSERT_EQ(r.content[0].type, MCPContent::TEXT);
    ASSERT_EQ(r.content[1].type, MCPContent::IMAGE);
}

TEST(tool_result_image_no_alt) {
    auto r = MCPToolResult::image("base64", "image/png");
    ASSERT_EQ(r.content.size(), (size_t)1);
}

TEST(tool_result_audio_with_desc) {
    auto r = MCPToolResult::audio("wavdata", "audio/wav", "Recording");
    ASSERT_EQ(r.content.size(), (size_t)2);
    ASSERT_EQ(r.content[0].text, String("Recording"));
    ASSERT_EQ(r.content[1].type, MCPContent::AUDIO);
}

TEST(tool_result_add_chaining) {
    MCPToolResult r;
    r.add(MCPContent::makeText("first"))
     .add(MCPContent::makeText("second"))
     .add(MCPContent::makeImage("img", "image/png"));
    ASSERT_EQ(r.content.size(), (size_t)3);
}

TEST(tool_result_to_json) {
    auto r = MCPToolResult::text("hello");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT(obj["content"].is<JsonArray>());
    ASSERT_EQ(obj["content"].as<JsonArray>().size(), (size_t)1);
}

TEST(tool_result_error_to_json) {
    auto r = MCPToolResult::error("fail");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    ASSERT(obj["isError"].as<bool>());
}

// ═══════════════════════════════════════════════════════════════════════
// MCPTool Annotations Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(tool_annotations_defaults) {
    MCPToolAnnotations ann;
    ASSERT(!ann.readOnlyHint);
    ASSERT(ann.destructiveHint);
    ASSERT(!ann.idempotentHint);
    ASSERT(ann.openWorldHint);
    ASSERT(!ann.hasAnnotations);
}

TEST(tool_annotations_set_readonly) {
    MCPToolAnnotations ann;
    ann.setReadOnlyHint(true);
    ASSERT(ann.readOnlyHint);
    ASSERT(!ann.destructiveHint);
    ASSERT(ann.hasAnnotations);
}

TEST(tool_annotations_builder_chain) {
    MCPToolAnnotations ann;
    ann.setIdempotentHint(true).setOpenWorldHint(false);
    ASSERT(ann.idempotentHint);
    ASSERT(!ann.openWorldHint);
}

TEST(tool_annotations_to_json) {
    MCPToolAnnotations ann;
    ann.setReadOnlyHint(true);
    ann.title = "My Tool";
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    ann.toJson(obj);
    ASSERT_EQ(String(obj["title"].as<const char*>()), String("My Tool"));
    ASSERT(obj["readOnlyHint"].as<bool>());
    ASSERT(!obj["destructiveHint"].as<bool>());
}

TEST(tool_mark_readonly) {
    MCPTool t;
    t.name = "test";
    t.markReadOnly();
    ASSERT(t.annotations.readOnlyHint);
    ASSERT(!t.annotations.destructiveHint);
}

TEST(tool_mark_idempotent) {
    MCPTool t;
    t.markIdempotent();
    ASSERT(t.annotations.idempotentHint);
}

TEST(tool_mark_local_only) {
    MCPTool t;
    t.markLocalOnly();
    ASSERT(!t.annotations.openWorldHint);
}

TEST(tool_to_json_with_annotations) {
    MCPTool t;
    t.name = "sensor_read";
    t.description = "Read sensor";
    t.inputSchemaJson = R"({"type":"object","properties":{}})";
    t.markReadOnly();
    t.annotations.title = "Sensor Reader";

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    t.toJson(obj);

    ASSERT_EQ(obj["name"].as<String>(), String("sensor_read"));
    ASSERT(obj.containsKey("annotations"));
    ASSERT(obj["annotations"]["readOnlyHint"].as<bool>());
}

TEST(tool_to_json_without_annotations) {
    MCPTool t;
    t.name = "test";
    t.description = "desc";
    t.inputSchemaJson = R"({"type":"object","properties":{}})";

    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    t.toJson(obj);

    ASSERT(!obj.containsKey("annotations"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPLogging Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(logging_default_level) {
    Logging log;
    ASSERT_EQ(log.getLevel(), LogLevel::WARNING);
}

TEST(logging_set_level) {
    Logging log;
    log.setLevel(LogLevel::DEBUG);
    ASSERT_EQ(log.getLevel(), LogLevel::DEBUG);
    log.setLevel(LogLevel::ERROR);
    ASSERT_EQ(log.getLevel(), LogLevel::ERROR);
}

TEST(logging_level_from_string) {
    ASSERT_EQ(logLevelFromString("debug"), LogLevel::DEBUG);
    ASSERT_EQ(logLevelFromString("info"), LogLevel::INFO);
    ASSERT_EQ(logLevelFromString("notice"), LogLevel::NOTICE);
    ASSERT_EQ(logLevelFromString("warning"), LogLevel::WARNING);
    ASSERT_EQ(logLevelFromString("error"), LogLevel::ERROR);
    ASSERT_EQ(logLevelFromString("critical"), LogLevel::CRITICAL);
    ASSERT_EQ(logLevelFromString("alert"), LogLevel::ALERT);
    ASSERT_EQ(logLevelFromString("emergency"), LogLevel::EMERGENCY);
    ASSERT_EQ(logLevelFromString("unknown"), LogLevel::INFO);  // default
    ASSERT_EQ(logLevelFromString(nullptr), LogLevel::INFO);
}

TEST(logging_level_to_string) {
    ASSERT_EQ(String(logLevelToString(LogLevel::DEBUG)), String("debug"));
    ASSERT_EQ(String(logLevelToString(LogLevel::INFO)), String("info"));
    ASSERT_EQ(String(logLevelToString(LogLevel::NOTICE)), String("notice"));
    ASSERT_EQ(String(logLevelToString(LogLevel::WARNING)), String("warning"));
    ASSERT_EQ(String(logLevelToString(LogLevel::ERROR)), String("error"));
    ASSERT_EQ(String(logLevelToString(LogLevel::CRITICAL)), String("critical"));
    ASSERT_EQ(String(logLevelToString(LogLevel::ALERT)), String("alert"));
    ASSERT_EQ(String(logLevelToString(LogLevel::EMERGENCY)), String("emergency"));
}

TEST(logging_convenience_methods) {
    Logging log;
    log.setLevel(LogLevel::DEBUG);
    log.debug("test", "debug message");
    log.info("test", "info message");
    log.warning("test", "warning message");
    log.error("test", "error message");
    log.critical("test", "critical message");
}

TEST(logging_sink_callback) {
    Logging log;
    bool called = false;
    log.setSink([&called](const String& notification) {
        called = true;
        // Should be a JSON-RPC notification
    });
    log.setLevel(LogLevel::DEBUG);
    log.info("test", "sink test");
    ASSERT(called);
}

TEST(logging_level_filtering_via_sink) {
    Logging log;
    int count = 0;
    log.setSink([&count](const String& notification) {
        count++;
    });
    log.setLevel(LogLevel::WARNING);
    log.debug("test", "should be filtered");
    log.info("test", "should be filtered");
    log.warning("test", "should pass");
    log.error("test", "should pass");
    ASSERT_EQ(count, 2);
}

TEST(logging_sink_notification_format) {
    Logging log;
    String captured;
    log.setSink([&captured](const String& notification) {
        captured = notification;
    });
    log.setLevel(LogLevel::DEBUG);
    log.info("sensor", "temp=22");
    ASSERT_STR_CONTAINS(captured.c_str(), "notifications/message");
    ASSERT_STR_CONTAINS(captured.c_str(), "sensor");
}

// ═══════════════════════════════════════════════════════════════════════
// MCPPrompt Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(prompt_argument_required) {
    MCPPromptArgument arg("name", "The name", true);
    ASSERT_EQ(arg.name, String("name"));
    ASSERT_EQ(arg.description, String("The name"));
    ASSERT(arg.required);
}

TEST(prompt_argument_optional) {
    MCPPromptArgument arg("style", "Greeting style", false);
    ASSERT(!arg.required);
}

TEST(prompt_message_construction) {
    MCPPromptMessage msg("user", "Hello world");
    ASSERT_EQ(msg.role, String("user"));
    ASSERT_EQ(msg.text, String("Hello world"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPResource Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(resource_fields) {
    MCPResource res;
    res.uri = "sensor://temp";
    res.name = "Temperature";
    res.description = "Current temperature";
    res.mimeType = "application/json";
    ASSERT_EQ(res.uri, String("sensor://temp"));
    ASSERT_EQ(res.name, String("Temperature"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPResourceTemplate Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(resource_template_fields) {
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "sensor://{sensor_id}/reading";
    tmpl.name = "Sensor Reading";
    tmpl.mimeType = "application/json";
    ASSERT_EQ(tmpl.uriTemplate, String("sensor://{sensor_id}/reading"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPRoot Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(root_construction) {
    MCPRoot root("file:///workspace", "Workspace");
    ASSERT_EQ(root.uri, String("file:///workspace"));
    ASSERT_EQ(root.name, String("Workspace"));
}

// ═══════════════════════════════════════════════════════════════════════
// MCPElicitation Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(elicitation_field_construction) {
    MCPElicitationField f;
    f.name = "username";
    f.title = "Username";
    f.description = "Your name";
    f.type = "string";
    f.required = true;
    ASSERT_EQ(f.name, String("username"));
    ASSERT(f.required);
}

TEST(elicitation_field_with_enum) {
    MCPElicitationField f;
    f.name = "unit";
    f.type = "string";
    f.enumValues = {"celsius", "fahrenheit", "kelvin"};
    ASSERT_EQ(f.enumValues.size(), (size_t)3);
}

TEST(elicitation_field_with_range) {
    MCPElicitationField f;
    f.name = "threshold";
    f.type = "number";
    f.minimum = 0;
    f.maximum = 100;
    f.hasMinimum = true;
    f.hasMaximum = true;
    ASSERT(f.hasMinimum);
    ASSERT(f.hasMaximum);
}

TEST(elicitation_field_to_json_schema) {
    MCPElicitationField f;
    f.name = "temp";
    f.title = "Temperature";
    f.type = "number";
    f.required = true;
    f.minimum = -40;
    f.maximum = 125;
    f.hasMinimum = true;
    f.hasMaximum = true;

    JsonDocument doc;
    JsonObject properties = doc["properties"].to<JsonObject>();
    JsonArray required = doc["required"].to<JsonArray>();
    f.toJsonSchema(properties, required);

    ASSERT(properties.containsKey("temp"));
    ASSERT_EQ(properties["temp"]["type"].as<String>(), String("number"));
    ASSERT_EQ(properties["temp"]["title"].as<String>(), String("Temperature"));
    ASSERT_EQ(required.size(), (size_t)1);
}

// ═══════════════════════════════════════════════════════════════════════
// MCPSampling Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(sampling_request_construction) {
    MCPSamplingRequest req;
    req.addUserMessage("What is 2+2?");
    req.maxTokens = 100;
    req.temperature = 0.7f;
    ASSERT_EQ(req.messages.size(), (size_t)1);
    ASSERT_EQ(req.maxTokens, 100);
}

// ═══════════════════════════════════════════════════════════════════════
// Completion Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(completion_manager_prompt) {
    CompletionManager cm;
    cm.addPromptCompletion("greet", "name", [](const String& argName, const String& value) -> std::vector<String> {
        return {"Alice", "Bob", "Charlie"};
    });
    ASSERT(cm.hasProviders());

    bool hasMore = false;
    auto results = cm.completePrompt("greet", "name", "", hasMore, 10);
    ASSERT_EQ(results.size(), (size_t)3);
    ASSERT(!hasMore);
}

TEST(completion_manager_resource_template) {
    CompletionManager cm;
    cm.addResourceTemplateCompletion("sensor://{id}/value", "id",
        [](const String& argName, const String& value) -> std::vector<String> {
            return {"temp1", "temp2", "humidity1"};
        });

    bool hasMore = false;
    auto results = cm.completeResourceTemplate("sensor://{id}/value", "id", "temp", hasMore, 10);
    ASSERT_EQ(results.size(), (size_t)2);  // temp1, temp2
}

TEST(completion_manager_truncation) {
    CompletionManager cm;
    cm.addPromptCompletion("test", "arg", [](const String&, const String&) -> std::vector<String> {
        std::vector<String> v;
        for (int i = 0; i < 10; i++) v.push_back(String("opt") + String(i));
        return v;
    });

    bool hasMore = false;
    auto results = cm.completePrompt("test", "arg", "", hasMore, 5);
    ASSERT_EQ(results.size(), (size_t)5);
    ASSERT(hasMore);
}

TEST(completion_manager_no_providers) {
    CompletionManager cm;
    ASSERT(!cm.hasProviders());
    bool hasMore = false;
    auto results = cm.completePrompt("nope", "arg", "", hasMore, 10);
    ASSERT_EQ(results.size(), (size_t)0);
}

// ═══════════════════════════════════════════════════════════════════════
// Version Constants Tests
// ═══════════════════════════════════════════════════════════════════════

TEST(version_constants) {
    ASSERT_EQ(String(MCPD_VERSION), String("0.48.0"));
    ASSERT_EQ(String(MCPD_MCP_PROTOCOL_VERSION), String("2025-11-25"));
}

// ═══════════════════════════════════════════════════════════════════════
// Server Integration Tests
// ═══════════════════════════════════════════════════════════════════════

static Server* makeServer() {
    static Server* s = nullptr;
    if (s) { delete s; }
    s = new Server("infra-test", 8080);
    return s;
}

TEST(server_name_and_port) {
    auto* s = makeServer();
    ASSERT_EQ(String(s->getName()), String("infra-test"));
    ASSERT_EQ(s->getPort(), (uint16_t)8080);
}

TEST(server_add_and_remove_tool) {
    auto* s = makeServer();
    s->addTool("temp", "Read temp",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String { return "{}"; });

    String listReq = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})";
    String resp = s->_processJsonRpc(listReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "temp");

    ASSERT(s->removeTool("temp"));
}

TEST(server_remove_nonexistent_tool) {
    auto* s = makeServer();
    ASSERT(!s->removeTool("nonexistent"));
}

TEST(server_add_resource_and_read) {
    auto* s = makeServer();
    s->addResource("test://data", "Test Data", "A test resource", "text/plain",
        []() -> String { return "Hello Resource"; });

    String listReq = R"({"jsonrpc":"2.0","id":1,"method":"resources/list","params":{}})";
    String resp = s->_processJsonRpc(listReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "test://data");

    String readReq = R"({"jsonrpc":"2.0","id":2,"method":"resources/read","params":{"uri":"test://data"}})";
    resp = s->_processJsonRpc(readReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "Hello Resource");
}

TEST(server_remove_resource) {
    auto* s = makeServer();
    s->addResource("rm://test", "To Remove", "Will be removed", "text/plain",
        []() -> String { return "bye"; });
    ASSERT(s->removeResource("rm://test"));
    ASSERT(!s->removeResource("rm://test"));
}

TEST(server_add_prompt) {
    auto* s = makeServer();
    s->addPrompt("greet", "Generate greeting",
        { MCPPromptArgument("name", "Name", true) },
        [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
            String name = args.at("name");
            return { MCPPromptMessage("user", name.c_str()) };
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"prompts/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "greet");
}

TEST(server_add_root) {
    auto* s = makeServer();
    s->addRoot("file:///data", "Data Root");

    String req = R"({"jsonrpc":"2.0","id":1,"method":"roots/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "file:///data");
}

TEST(server_set_endpoint) {
    auto* s = makeServer();
    s->setEndpoint("/api/mcp");
}

TEST(server_set_mdns) {
    auto* s = makeServer();
    s->setMDNS(false);
    s->setMDNS(true);
}

TEST(server_session_management_config) {
    auto* s = makeServer();
    s->setMaxSessions(8);
    s->setSessionTimeout(60000);
    ASSERT_EQ(s->sessions().maxSessions(), (size_t)8);
    ASSERT_EQ(s->sessions().idleTimeout(), (unsigned long)60000);
}

TEST(server_rate_limiting_config) {
    auto* s = makeServer();
    s->setRateLimit(100.0f, 50);
    ASSERT(s->rateLimiter().isEnabled());
}

TEST(server_page_size) {
    auto* s = makeServer();
    s->setPageSize(10);
}

TEST(server_rich_tool) {
    auto* s = makeServer();
    s->addRichTool("rich_test", "Rich tool test",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> MCPToolResult {
            MCPToolResult r;
            r.add(MCPContent::makeText("Hello"));
            r.add(MCPContent::makeImage("base64img", "image/png"));
            return r;
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rich_test","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "Hello");
    ASSERT_STR_CONTAINS(resp.c_str(), "image");
}

TEST(server_rich_tool_error) {
    auto* s = makeServer();
    s->addRichTool("rich_err", "Rich error test",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> MCPToolResult {
            return MCPToolResult::error("something failed");
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"rich_err","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "something failed");
    ASSERT_STR_CONTAINS(resp.c_str(), "isError");
}

TEST(server_resource_template_list) {
    auto* s = makeServer();
    MCPResourceTemplate tmpl;
    tmpl.uriTemplate = "sensor://{id}/value";
    tmpl.name = "Sensor Value";
    tmpl.description = "Read sensor by ID";
    tmpl.mimeType = "application/json";
    tmpl.handler = [](const std::map<String, String>&) -> String { return "{\"value\":42}"; };
    s->addResourceTemplate(tmpl);

    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/templates/list","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "sensor://{id}/value");
}

TEST(server_ping) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":"ping-1","method":"ping"})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "ping-1");
    ASSERT_STR_CONTAINS(resp.c_str(), "result");
}

TEST(server_initialize) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "protocolVersion");
    ASSERT_STR_CONTAINS(resp.c_str(), "serverInfo");
    ASSERT_STR_CONTAINS(resp.c_str(), MCPD_VERSION);
}

TEST(server_invalid_method) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"nonexistent/method","params":{}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
    ASSERT_STR_CONTAINS(resp.c_str(), "-32601");
}

TEST(server_invalid_json) {
    auto* s = makeServer();
    String resp = s->_processJsonRpc("not json at all");
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
    ASSERT_STR_CONTAINS(resp.c_str(), "-32700");
}

TEST(server_missing_method) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(server_tool_not_found) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"ghost_tool","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(server_resource_not_found) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"resources/read","params":{"uri":"nope://nothing"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

TEST(server_logging_set_level_via_rpc) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"logging/setLevel","params":{"level":"debug"}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "result");
}

TEST(server_resource_subscribe) {
    auto* s = makeServer();
    s->addResource("sub://test", "Subtest", "Subscribable", "text/plain",
        []() -> String { return "data"; });

    String subReq = R"({"jsonrpc":"2.0","id":1,"method":"resources/subscribe","params":{"uri":"sub://test"}})";
    String resp = s->_processJsonRpc(subReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "result");
}

TEST(server_resource_unsubscribe) {
    auto* s = makeServer();
    String unsubReq = R"({"jsonrpc":"2.0","id":2,"method":"resources/unsubscribe","params":{"uri":"sub://test"}})";
    String resp = s->_processJsonRpc(unsubReq);
    ASSERT_STR_CONTAINS(resp.c_str(), "result");
}

TEST(server_batch_request) {
    auto* s = makeServer();
    String req = R"([{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","id":2,"method":"ping"}])";
    String resp = s->_processJsonRpc(req);
    ASSERT(resp[0] == '[');
}

TEST(server_notification_initialized) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
    String resp = s->_processJsonRpc(req);
    // Notifications may return empty
}

TEST(server_lifecycle_hooks) {
    auto* s = makeServer();
    bool initCalled = false;
    bool connectCalled = false;
    bool disconnectCalled = false;
    s->onInitialize([&initCalled](const String& client) { initCalled = true; });
    s->onConnect([&connectCalled]() { connectCalled = true; });
    s->onDisconnect([&disconnectCalled]() { disconnectCalled = true; });
    // Hooks registered without crash
}

TEST(server_prompts_get) {
    auto* s = makeServer();
    s->addPrompt("hello", "Say hello",
        { MCPPromptArgument("name", "Name", true) },
        [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
            return { MCPPromptMessage("user", "Hello") };
        });

    String req = R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"hello","arguments":{"name":"World"}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "messages");
}

TEST(server_prompts_get_not_found) {
    auto* s = makeServer();
    String req = R"({"jsonrpc":"2.0","id":1,"method":"prompts/get","params":{"name":"nonexistent","arguments":{}}})";
    String resp = s->_processJsonRpc(req);
    ASSERT_STR_CONTAINS(resp.c_str(), "error");
}

// ═══════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════

int main() {
    printf("\n══════════════════════════════════════════\n");
    printf(" mcpd Infrastructure Tests\n");
    printf("══════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
