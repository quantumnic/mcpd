/**
 * Tests for MCP 2025-11-25 spec features:
 *   - Icons on Tool, Resource, ResourceTemplate, Prompt, Server
 *   - title field on Tool, Resource, ResourceTemplate, Prompt
 *   - ResourceLink content type
 *   - Resource.size field
 *   - Annotations.lastModified field
 *   - Implementation.description and websiteUrl
 *   - Protocol version update
 */

#include "test_framework.h"
#include "../src/mcpd.h"
#include "../src/mcpd.cpp"

using namespace mcpd;

// ── Helper ─────────────────────────────────────────────────────────────

static Server* makeServer() {
    static Server s("test-2025-11-25");
    s = Server("test-2025-11-25");
    return &s;
}

static String dispatch(Server& s, const char* json) {
    return s._processJsonRpc(String(json));
}

static String initReq() {
    return R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}})";
}

// ── Protocol Version ───────────────────────────────────────────────────

TEST(protocol_version_2025_11_25) {
    ASSERT_EQ(String(MCPD_MCP_PROTOCOL_VERSION), String("2025-11-25"));
    ASSERT_EQ(String(MCPD_VERSION), String("0.43.0"));
}

TEST(initialize_returns_2025_11_25) {
    auto* s = makeServer();
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_CONTAINS(resp.c_str(), "2025-11-25");
    ASSERT_STR_CONTAINS(resp.c_str(), "0.43.0");
}

TEST(compat_version_defined) {
    ASSERT_EQ(String(MCPD_MCP_PROTOCOL_VERSION_COMPAT), String("2025-03-26"));
}

// ── Icon Serialization ─────────────────────────────────────────────────

TEST(icon_basic_serialization) {
    MCPIcon icon("https://example.com/icon.png", "image/png");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    icon.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"src\":\"https://example.com/icon.png\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"mimeType\":\"image/png\"");
}

TEST(icon_with_sizes_and_theme) {
    MCPIcon icon("data:image/svg+xml;base64,abc");
    icon.setMimeType("image/svg+xml").addSize("48x48").addSize("96x96").setTheme("dark");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    icon.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"sizes\":[\"48x48\",\"96x96\"]");
    ASSERT_STR_CONTAINS(json.c_str(), "\"theme\":\"dark\"");
}

TEST(icon_minimal) {
    MCPIcon icon("https://example.com/i.png");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    icon.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"src\":");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"mimeType\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"sizes\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"theme\"");
}

TEST(icons_to_json_empty) {
    std::vector<MCPIcon> icons;
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    iconsToJson(icons, obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "icons");
}

TEST(icons_to_json_multiple) {
    std::vector<MCPIcon> icons;
    icons.push_back(MCPIcon("a.png", "image/png"));
    icons.push_back(MCPIcon("b.svg", "image/svg+xml"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    iconsToJson(icons, obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":[");
    ASSERT_STR_CONTAINS(json.c_str(), "a.png");
    ASSERT_STR_CONTAINS(json.c_str(), "b.svg");
}

// ── Tool: title + icons ────────────────────────────────────────────────

TEST(tool_title_serialized) {
    MCPTool tool("my_tool", "Does stuff", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    tool.setTitle("My Tool");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"My Tool\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"name\":\"my_tool\"");
}

TEST(tool_no_title_when_empty) {
    MCPTool tool("my_tool", "Does stuff", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"title\"");
}

TEST(tool_icons_serialized) {
    MCPTool tool("led", "LED control", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    tool.addIcon(MCPIcon("https://example.com/led.png", "image/png"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":[");
    ASSERT_STR_CONTAINS(json.c_str(), "led.png");
}

TEST(tool_no_icons_when_empty) {
    MCPTool tool("led", "LED control", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"icons\"");
}

TEST(tool_title_and_icons_combined) {
    MCPTool tool("gpio", "GPIO", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    tool.setTitle("GPIO Controller").addIcon(MCPIcon("gpio.svg")).markReadOnly();
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    tool.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"GPIO Controller\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":");
    ASSERT_STR_CONTAINS(json.c_str(), "\"readOnlyHint\":true");
}

// ── Resource: title + icons + size ─────────────────────────────────────

TEST(resource_title_serialized) {
    MCPResource r("sensor://temp", "temp", "Temperature", "text/plain", []() { return "22.5"; });
    r.setTitle("Temperature Sensor");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"Temperature Sensor\"");
}

TEST(resource_size_serialized) {
    MCPResource r("file://data.csv", "data", "CSV data", "text/csv", []() { return "a,b,c"; });
    r.setSize(1024);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"size\":1024");
}

TEST(resource_size_not_serialized_when_unset) {
    MCPResource r("file://x", "x", "X", "text/plain", []() { return ""; });
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"size\"");
}

TEST(resource_icons_serialized) {
    MCPResource r("sensor://t", "t", "T", "text/plain", []() { return ""; });
    r.addIcon(MCPIcon("temp.png", "image/png"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":");
    ASSERT_STR_CONTAINS(json.c_str(), "temp.png");
}

TEST(resource_all_new_fields) {
    MCPResource r("file://log", "log", "Log file", "text/plain", []() { return "..."; });
    r.setTitle("System Log").setSize(4096).addIcon(MCPIcon("log.svg"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"System Log\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"size\":4096");
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":");
}

// ── ResourceTemplate: title + icons ────────────────────────────────────

TEST(resource_template_title) {
    MCPResourceTemplate t("sensor://{id}/reading", "reading", "Reading", "text/plain",
                          [](const std::map<String, String>&) { return ""; });
    t.setTitle("Sensor Reading");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    t.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"Sensor Reading\"");
}

TEST(resource_template_icons) {
    MCPResourceTemplate t("sensor://{id}/reading", "reading", "Reading", "text/plain",
                          [](const std::map<String, String>&) { return ""; });
    t.addIcon(MCPIcon("sensor.png"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    t.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":");
    ASSERT_STR_CONTAINS(json.c_str(), "sensor.png");
}

// ── Prompt: title + icons ──────────────────────────────────────────────

TEST(prompt_title) {
    MCPPrompt p("greet", "Greeting",
                {MCPPromptArgument("name", "Name", true)},
                [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> { return {}; });
    p.setTitle("Greeting Prompt");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    p.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"title\":\"Greeting Prompt\"");
}

TEST(prompt_icons) {
    MCPPrompt p("greet", "Greeting", {},
                [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> { return {}; });
    p.addIcon(MCPIcon("chat.png", "image/png"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    p.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"icons\":");
    ASSERT_STR_CONTAINS(json.c_str(), "chat.png");
}

TEST(prompt_no_title_when_empty) {
    MCPPrompt p("greet", "Greeting", {},
                [](const std::map<String, String>&) -> std::vector<MCPPromptMessage> { return {}; });
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    p.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"title\"");
}

// ── ResourceLink Content Type ──────────────────────────────────────────

TEST(resource_link_content) {
    MCPContent c = MCPContent::makeResourceLink("file://data.csv", "Data File", "text/csv", "Raw sensor data");
    ASSERT(c.type == MCPContent::RESOURCE_LINK);
    ASSERT_EQ(c.uri, String("file://data.csv"));
    ASSERT_EQ(c.name, String("Data File"));
}

TEST(resource_link_serialization) {
    MCPContent c = MCPContent::makeResourceLink("file://log.txt", "Server Log", "text/plain", "Latest log entries");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"type\":\"resource_link\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"uri\":\"file://log.txt\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"name\":\"Server Log\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"mimeType\":\"text/plain\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"description\":\"Latest log entries\"");
}

TEST(resource_link_minimal) {
    MCPContent c = MCPContent::makeResourceLink("file://x", "X");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    c.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"type\":\"resource_link\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"name\":\"X\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"mimeType\"");
    ASSERT_STR_NOT_CONTAINS(json.c_str(), "\"description\"");
}

TEST(resource_link_in_tool_result) {
    MCPToolResult result;
    result.add(MCPContent::makeText("Found these files:"));
    result.add(MCPContent::makeResourceLink("file://data.csv", "Data", "text/csv"));
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    result.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"type\":\"text\"");
    ASSERT_STR_CONTAINS(json.c_str(), "\"type\":\"resource_link\"");
}

// ── Annotations.lastModified ───────────────────────────────────────────

TEST(annotations_last_modified) {
    MCPResourceAnnotations ann;
    ann.setLastModified("2025-11-25T12:00:00Z");
    ASSERT(ann.hasAnnotations);
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    ann.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"lastModified\":\"2025-11-25T12:00:00Z\"");
}

TEST(annotations_last_modified_on_resource) {
    MCPResource r("file://x", "x", "X", "text/plain", []() { return ""; });
    r.annotations.setLastModified("2025-01-01T00:00:00Z");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    r.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"lastModified\":\"2025-01-01T00:00:00Z\"");
}

TEST(annotations_all_fields) {
    MCPResourceAnnotations ann;
    ann.setAudience("user").setPriority(0.8f).setLastModified("2025-06-15T10:30:00Z");
    JsonDocument doc;
    JsonObject obj = doc.to<JsonObject>();
    ann.toJson(obj);
    String json;
    serializeJson(doc, json);
    ASSERT_STR_CONTAINS(json.c_str(), "\"audience\":[\"user\"]");
    ASSERT_STR_CONTAINS(json.c_str(), "\"priority\":");
    ASSERT_STR_CONTAINS(json.c_str(), "\"lastModified\":");
}

// ── Server Implementation Metadata ─────────────────────────────────────

TEST(server_description_in_initialize) {
    auto* s = makeServer();
    s->setDescription("A test MCP server for microcontrollers");
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_CONTAINS(resp.c_str(), "\"description\":\"A test MCP server for microcontrollers\"");
}

TEST(server_website_url_in_initialize) {
    auto* s = makeServer();
    s->setWebsiteUrl("https://github.com/redbasecap-buiss/mcpd");
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_CONTAINS(resp.c_str(), "\"websiteUrl\":\"https://github.com/redbasecap-buiss/mcpd\"");
}

TEST(server_icons_in_initialize) {
    auto* s = makeServer();
    s->addIcon(MCPIcon("https://example.com/logo.png", "image/png"));
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_CONTAINS(resp.c_str(), "\"icons\":[");
    ASSERT_STR_CONTAINS(resp.c_str(), "logo.png");
}

TEST(server_no_metadata_when_unset) {
    auto* s = makeServer();
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_NOT_CONTAINS(resp.c_str(), "\"description\"");
    ASSERT_STR_NOT_CONTAINS(resp.c_str(), "\"websiteUrl\"");
    ASSERT_STR_NOT_CONTAINS(resp.c_str(), "\"icons\"");
}

TEST(server_all_implementation_fields) {
    auto* s = makeServer();
    s->setDescription("Full featured test");
    s->setWebsiteUrl("https://mcpd.dev");
    s->addIcon(MCPIcon("logo.svg", "image/svg+xml"));
    s->addIcon(MCPIcon("logo-dark.svg").setTheme("dark"));
    String resp = dispatch(*s, initReq().c_str());
    ASSERT_STR_CONTAINS(resp.c_str(), "\"description\":\"Full featured test\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"websiteUrl\":\"https://mcpd.dev\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "logo.svg");
    ASSERT_STR_CONTAINS(resp.c_str(), "logo-dark.svg");
}

// ── Tool with title appears in tools/list ──────────────────────────────

TEST(tools_list_includes_title) {
    auto* s = makeServer();
    MCPTool tool("read_temp", "Read temperature", "{\"type\":\"object\"}", [](const JsonObject&) { return "22"; });
    tool.setTitle("Read Temperature");
    s->addTool(tool);
    dispatch(*s, initReq().c_str());
    String resp = dispatch(*s, R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"title\":\"Read Temperature\"");
}

TEST(tools_list_includes_icons) {
    auto* s = makeServer();
    MCPTool tool("led", "LED", "{\"type\":\"object\"}", [](const JsonObject&) { return "ok"; });
    tool.addIcon(MCPIcon("led.png", "image/png"));
    s->addTool(tool);
    dispatch(*s, initReq().c_str());
    String resp = dispatch(*s, R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
    ASSERT_STR_CONTAINS(resp.c_str(), "led.png");
}

// ── Resource with new fields in resources/list ─────────────────────────

TEST(resources_list_includes_title_and_size) {
    auto* s = makeServer();
    MCPResource r("file://log", "log", "Log", "text/plain", []() { return "data"; });
    r.setTitle("System Log").setSize(2048);
    s->addResource(r);
    dispatch(*s, initReq().c_str());
    String resp = dispatch(*s, R"({"jsonrpc":"2.0","id":2,"method":"resources/list","params":{}})");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"title\":\"System Log\"");
    ASSERT_STR_CONTAINS(resp.c_str(), "\"size\":2048");
}

// ── Backward Compatibility ─────────────────────────────────────────────

TEST(old_client_can_initialize) {
    // Client sends 2025-03-26 — server should still work
    auto* s = makeServer();
    String resp = dispatch(*s, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"old-client"}}})");
    ASSERT_STR_CONTAINS(resp.c_str(), "protocolVersion");
    ASSERT_STR_CONTAINS(resp.c_str(), "serverInfo");
}

// ── Builder Chaining ───────────────────────────────────────────────────

TEST(tool_builder_chaining) {
    MCPTool tool("x", "X", "{\"type\":\"object\"}", [](const JsonObject&) { return ""; });
    tool.setTitle("X Tool")
        .addIcon(MCPIcon("x.png"))
        .markReadOnly()
        .markIdempotent()
        .setOutputSchema("{\"type\":\"object\",\"properties\":{\"val\":{\"type\":\"number\"}}}");
    ASSERT_EQ(tool.title, String("X Tool"));
    ASSERT(tool.icons.size() == 1);
    ASSERT(tool.annotations.readOnlyHint);
    ASSERT(tool.annotations.idempotentHint);
    ASSERT(!tool.outputSchemaJson.isEmpty());
}

TEST(resource_builder_chaining) {
    MCPResource r("s://x", "x", "X", "text/plain", []() { return ""; });
    r.setTitle("X Resource").setSize(512).addIcon(MCPIcon("x.png")).setAudience("user").setPriority(0.5f);
    ASSERT_EQ(r.title, String("X Resource"));
    ASSERT(r.size == 512);
    ASSERT(r.icons.size() == 1);
    ASSERT(r.annotations.hasAnnotations);
}

// ── Main ───────────────────────────────────────────────────────────────

int main() {
    printf("\n  mcpd — MCP 2025-11-25 Feature Tests\n");
    printf("  ════════════════════════════════════════\n\n");

    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
