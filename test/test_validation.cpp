/**
 * mcpd — Input Validation Tests
 *
 * Tests for MCPValidation.h (standalone) and Server input validation integration.
 */

#include "test_framework.h"
#include "mcpd.h"
#include "mcpd.cpp"

using namespace mcpd;

// ═══════════════════════════════════════════════════════════════════════
// Unit tests for validateArguments()
// ═══════════════════════════════════════════════════════════════════════

TEST(validation_required_present) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"hello"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
    ASSERT_EQ((int)vr.errors.size(), 0);
}

TEST(validation_required_missing) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_EQ((int)vr.errors.size(), 1);
    ASSERT_STR_CONTAINS(vr.errors[0].field.c_str(), "name");
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "required");
}

TEST(validation_required_null) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":null})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "required");
}

TEST(validation_multiple_required_missing) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"a":{"type":"string"},"b":{"type":"integer"},"c":{"type":"boolean"}},"required":["a","b","c"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"b":42})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_EQ((int)vr.errors.size(), 2);
}

TEST(validation_type_string_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"hello"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_string_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":42})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "string");
}

TEST(validation_type_integer_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":13})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_integer_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":"thirteen"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "integer");
}

TEST(validation_type_number_accepts_int) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"temp":{"type":"number"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"temp":42})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_number_accepts_float) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"temp":{"type":"number"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"temp":23.5})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_boolean_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"enabled":{"type":"boolean"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"enabled":true})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_boolean_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"enabled":{"type":"boolean"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"enabled":"yes"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "boolean");
}

TEST(validation_type_array_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pins":{"type":"array"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pins":[1,2,3]})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_type_array_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pins":{"type":"array"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pins":"not-an-array"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
}

TEST(validation_type_object_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"config":{"type":"object"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"config":{"key":"val"}})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_enum_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"mode":{"type":"string","enum":["INPUT","OUTPUT","INPUT_PULLUP"]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"mode":"OUTPUT"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_enum_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"mode":{"type":"string","enum":["INPUT","OUTPUT","INPUT_PULLUP"]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"mode":"BANANA"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "must be one of");
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "OUTPUT");
}

TEST(validation_minimum_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer","minimum":0,"maximum":39}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":13})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_minimum_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer","minimum":0}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":-1})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), ">=");
}

TEST(validation_maximum_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer","minimum":0,"maximum":39}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":50})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "<=");
}

TEST(validation_minlength_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":32}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"hello"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_minlength_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string","minLength":3}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"ab"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "length");
}

TEST(validation_maxlength_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string","maxLength":5}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"toolongname"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "length");
}

TEST(validation_minitems_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pins":{"type":"array","minItems":1}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pins":[1,2]})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_minitems_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pins":{"type":"array","minItems":1}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pins":[]})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "items");
}

TEST(validation_maxitems_violation) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pins":{"type":"array","maxItems":2}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pins":[1,2,3]})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "items");
}

TEST(validation_nested_object) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"config":{"type":"object","properties":{"interval":{"type":"integer"}},"required":["interval"]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"config":{}})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].field.c_str(), "config.interval");
}

TEST(validation_nested_object_ok) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"config":{"type":"object","properties":{"interval":{"type":"integer"}},"required":["interval"]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"config":{"interval":1000}})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_optional_field_missing) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"},"desc":{"type":"string"}},"required":["name"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":"test"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_empty_schema) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object"})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"anything":"goes"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_no_required_array) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_toString) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    String s = vr.toString();
    ASSERT_STR_CONTAINS(s.c_str(), "Invalid arguments");
    ASSERT_STR_CONTAINS(s.c_str(), "pin");
}

TEST(validation_toJson) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    String json = vr.toJson();
    ASSERT_STR_CONTAINS(json.c_str(), "validationErrors");
    ASSERT_STR_CONTAINS(json.c_str(), "pin");
}

TEST(validation_valid_result_toString) {
    ValidationResult vr;
    ASSERT_TRUE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "OK");
}

TEST(validation_enum_integer) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"channel":{"type":"integer","enum":[0,1,2]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"channel":1})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_enum_integer_wrong) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"channel":{"type":"integer","enum":[0,1,2]}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"channel":5})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.errors[0].message.c_str(), "must be one of");
}

TEST(validation_null_optional_skipped) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"name":{"type":"string"}}})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"name":null})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validation_multiple_errors) {
    JsonDocument schemaDoc;
    deserializeJson(schemaDoc, R"({"type":"object","properties":{"pin":{"type":"integer","minimum":0,"maximum":39},"mode":{"type":"string","enum":["INPUT","OUTPUT"]}},"required":["pin","mode"]})");
    JsonDocument argsDoc;
    deserializeJson(argsDoc, R"({"pin":50,"mode":"BANANA"})");
    ValidationResult vr = validateArguments(argsDoc.as<JsonObject>(), schemaDoc.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_EQ((int)vr.errors.size(), 2);
}

// ═══════════════════════════════════════════════════════════════════════
// Server integration tests
// ═══════════════════════════════════════════════════════════════════════

TEST(server_validation_disabled_by_default) {
    Server mcp("test-val");
    ASSERT_FALSE(mcp.isInputValidationEnabled());
}

TEST(server_validation_enable) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    ASSERT_TRUE(mcp.isInputValidationEnabled());
    mcp.enableInputValidation(false);
    ASSERT_FALSE(mcp.isInputValidationEnabled());
}

TEST(server_validation_rejects_missing_required) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    mcp.addTool("gpio_write", "Write GPIO",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"value":{"type":"integer"}},"required":["pin","value"]})",
        [](const JsonObject& args) -> String { return "ok"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gpio_write","arguments":{"pin":13}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "error");
    ASSERT_STR_CONTAINS(response.c_str(), "value");
    ASSERT_STR_CONTAINS(response.c_str(), "required");
}

TEST(server_validation_rejects_wrong_type) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    mcp.addTool("gpio_write", "Write GPIO",
        R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})",
        [](const JsonObject& args) -> String { return "ok"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gpio_write","arguments":{"pin":"not-a-number"}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "error");
    ASSERT_STR_CONTAINS(response.c_str(), "integer");
}

TEST(server_validation_passes_valid) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    mcp.addTool("gpio_write", "Write GPIO",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"value":{"type":"integer"}},"required":["pin","value"]})",
        [](const JsonObject& args) -> String { return "{\"ok\":true}"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gpio_write","arguments":{"pin":13,"value":1}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "result");
    ASSERT_STR_NOT_CONTAINS(response.c_str(), "\"error\"");
}

TEST(server_validation_off_allows_anything) {
    Server mcp("test-val");
    mcp.addTool("gpio_write", "Write GPIO",
        R"({"type":"object","properties":{"pin":{"type":"integer"}},"required":["pin"]})",
        [](const JsonObject& args) -> String { return "ok"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gpio_write","arguments":{}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "result");
}

TEST(server_validation_enum_rejection) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    mcp.addTool("gpio_mode", "Set GPIO mode",
        R"({"type":"object","properties":{"pin":{"type":"integer"},"mode":{"type":"string","enum":["INPUT","OUTPUT","INPUT_PULLUP"]}},"required":["pin","mode"]})",
        [](const JsonObject& args) -> String { return "ok"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"gpio_mode","arguments":{"pin":13,"mode":"INVALID"}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "error");
    ASSERT_STR_CONTAINS(response.c_str(), "must be one of");
}

TEST(server_validation_range_rejection) {
    Server mcp("test-val");
    mcp.enableInputValidation();
    mcp.addTool("pwm_write", "Write PWM",
        R"({"type":"object","properties":{"pin":{"type":"integer","minimum":0,"maximum":39},"duty":{"type":"integer","minimum":0,"maximum":255}},"required":["pin","duty"]})",
        [](const JsonObject& args) -> String { return "ok"; });
    String body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pwm_write","arguments":{"pin":50,"duty":128}}})";
    String response = mcp._processJsonRpc(body);
    ASSERT_STR_CONTAINS(response.c_str(), "error");
    ASSERT_STR_CONTAINS(response.c_str(), "<=");
}

// ═══════════════════════════════════════════════════════════════════════
// Helper function tests
// ═══════════════════════════════════════════════════════════════════════

TEST(validateType_string) {
    JsonDocument doc;
    deserializeJson(doc, R"({"v":"hello"})");
    ASSERT_TRUE(validateType(doc["v"], "string"));
    ASSERT_FALSE(validateType(doc["v"], "integer"));
}

TEST(validateType_number) {
    JsonDocument doc;
    deserializeJson(doc, R"({"v":3.14})");
    ASSERT_TRUE(validateType(doc["v"], "number"));
}

TEST(validateType_boolean) {
    JsonDocument doc;
    deserializeJson(doc, R"({"v":true})");
    ASSERT_TRUE(validateType(doc["v"], "boolean"));
    ASSERT_FALSE(validateType(doc["v"], "string"));
}

TEST(validateType_null) {
    JsonDocument doc;
    deserializeJson(doc, R"({"v":null})");
    ASSERT_TRUE(validateType(doc["v"], "null"));
}

TEST(validateType_unknown) {
    JsonDocument doc;
    deserializeJson(doc, R"({"v":42})");
    ASSERT_TRUE(validateType(doc["v"], "foobar"));
}

TEST(jsonTypeName_values) {
    JsonDocument doc;
    deserializeJson(doc, R"({"s":"hello","i":42,"b":true})");
    ASSERT_STR_CONTAINS(jsonTypeName(doc["s"]), "string");
    ASSERT_STR_CONTAINS(jsonTypeName(doc["i"]), "integer");
    ASSERT_STR_CONTAINS(jsonTypeName(doc["b"]), "boolean");
}

// ═══════════════════════════════════════════════════════════════════════

int main() {
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
