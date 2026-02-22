/**
 * mcpd — Input & Output Validation Tests
 *
 * Tests for MCPValidation.h (standalone), Server input validation,
 * and Server output validation integration.
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

// Helper for server integration tests
static String dispatch(Server& s, const char* json) {
    return s._processJsonRpc(String(json));
}

// ═══════════════════════════════════════════════════════════════════════
// validateValue — Output Validation (standalone)
// ═══════════════════════════════════════════════════════════════════════

TEST(validateValue_object_valid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"object","properties":{"name":{"type":"string"},"age":{"type":"integer"}},"required":["name"]})");
    JsonDocument val;
    deserializeJson(val, R"({"name":"Alice","age":30})");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_object_missing_required) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"object","properties":{"name":{"type":"string"}},"required":["name"]})");
    JsonDocument val;
    deserializeJson(val, R"({"age":30})");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "name");
}

TEST(validateValue_wrong_root_type) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"object"})");
    JsonDocument val;
    deserializeJson(val, R"("hello")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "must be object");
}

TEST(validateValue_string_valid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"string","minLength":2,"maxLength":10})");
    JsonDocument val;
    deserializeJson(val, R"("hello")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_string_too_short) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"string","minLength":5})");
    JsonDocument val;
    deserializeJson(val, R"("hi")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "length");
}

TEST(validateValue_number_in_range) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"number","minimum":0,"maximum":100})");
    JsonDocument val;
    deserializeJson(val, "42");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_number_out_of_range) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"number","maximum":50})");
    JsonDocument val;
    deserializeJson(val, "75");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "<=");
}

TEST(validateValue_enum_valid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"string","enum":["on","off","standby"]})");
    JsonDocument val;
    deserializeJson(val, R"("on")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_enum_invalid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"string","enum":["on","off"]})");
    JsonDocument val;
    deserializeJson(val, R"("maybe")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "enum");
}

TEST(validateValue_array_valid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"array","minItems":1,"maxItems":5})");
    JsonDocument val;
    deserializeJson(val, R"([1,2,3])");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_array_too_many) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"array","maxItems":2})");
    JsonDocument val;
    deserializeJson(val, R"([1,2,3,4])");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "<=");
}

TEST(validateValue_integer_type_check) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"integer"})");
    JsonDocument val;
    deserializeJson(val, R"("notanumber")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "must be integer");
}

TEST(validateValue_boolean_type_check) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"boolean"})");
    JsonDocument val;
    deserializeJson(val, "true");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_no_schema_type) {
    // Schema without type constraint should pass anything
    JsonDocument schema;
    deserializeJson(schema, R"({})");
    JsonDocument val;
    deserializeJson(val, R"("anything")");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_nested_object) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"object","properties":{"status":{"type":"string"},"data":{"type":"object","properties":{"value":{"type":"number","minimum":0}},"required":["value"]}},"required":["status","data"]})");
    JsonDocument val;
    deserializeJson(val, R"({"status":"ok","data":{"value":42}})");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_TRUE(vr.valid);
}

TEST(validateValue_nested_object_invalid) {
    JsonDocument schema;
    deserializeJson(schema, R"({"type":"object","properties":{"data":{"type":"object","properties":{"value":{"type":"number","minimum":0}},"required":["value"]}},"required":["data"]})");
    JsonDocument val;
    deserializeJson(val, R"({"data":{}})");
    ValidationResult vr = validateValue(val.as<JsonVariant>(), schema.as<JsonObject>());
    ASSERT_FALSE(vr.valid);
    ASSERT_STR_CONTAINS(vr.toString().c_str(), "value");
}

// ═══════════════════════════════════════════════════════════════════════
// Server Output Validation Integration
// ═══════════════════════════════════════════════════════════════════════

TEST(server_output_validation_disabled_by_default) {
    Server s("test");
    ASSERT_FALSE(s.isOutputValidationEnabled());
}

TEST(server_output_validation_enable) {
    Server s("test");
    s.enableOutputValidation();
    ASSERT_TRUE(s.isOutputValidationEnabled());
    s.enableOutputValidation(false);
    ASSERT_FALSE(s.isOutputValidationEnabled());
}

TEST(server_output_validation_valid_output) {
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "get_status";
    tool.description = "Get device status";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"status":{"type":"string"},"uptime":{"type":"integer"}},"required":["status"]})";
    tool.handler = [](JsonObject) -> String {
        return R"({"status":"ok","uptime":12345})";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"get_status","arguments":{}},"id":1})");
    // Should succeed — valid output
    ASSERT_STR_NOT_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "structuredContent");
    ASSERT_STR_CONTAINS(r.c_str(), "\"status\":\"ok\"");
}

TEST(server_output_validation_invalid_output_type) {
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "bad_output";
    tool.description = "Returns wrong type";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"count":{"type":"integer"}},"required":["count"]})";
    tool.handler = [](JsonObject) -> String {
        return R"({"count":"notanumber"})";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"bad_output","arguments":{}},"id":1})");
    ASSERT_STR_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "isError");
}

TEST(server_output_validation_missing_required) {
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "missing_field";
    tool.description = "Misses required field";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"name":{"type":"string"},"value":{"type":"number"}},"required":["name","value"]})";
    tool.handler = [](JsonObject) -> String {
        return R"({"name":"sensor"})";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"missing_field","arguments":{}},"id":1})");
    ASSERT_STR_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "value");
    ASSERT_STR_CONTAINS(r.c_str(), "isError");
}

TEST(server_output_validation_non_json_output_passes) {
    // If handler returns non-JSON text, no structuredContent and no validation
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "text_tool";
    tool.description = "Returns plain text";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"x":{"type":"number"}}})";
    tool.handler = [](JsonObject) -> String {
        return "Just plain text, not JSON";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"text_tool","arguments":{}},"id":1})");
    // Non-JSON output → no structuredContent → no validation → should pass
    ASSERT_STR_NOT_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "Just plain text");
}

TEST(server_output_validation_disabled_allows_invalid) {
    // With output validation disabled, invalid output should pass through
    Server s("test");
    // Not calling enableOutputValidation()

    MCPTool tool;
    tool.name = "no_validate";
    tool.description = "Invalid output but no validation";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"x":{"type":"integer"}},"required":["x"]})";
    tool.handler = [](JsonObject) -> String {
        return R"({"x":"wrong_type"})";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"no_validate","arguments":{}},"id":1})");
    ASSERT_STR_NOT_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "structuredContent");
}

TEST(server_output_validation_range_violation) {
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "range_tool";
    tool.description = "Output with range constraint";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    tool.outputSchemaJson = R"({"type":"object","properties":{"percent":{"type":"number","minimum":0,"maximum":100}}})";
    tool.handler = [](JsonObject) -> String {
        return R"({"percent":150})";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"range_tool","arguments":{}},"id":1})");
    ASSERT_STR_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "isError");
}

TEST(server_output_validation_no_outputschema_skips) {
    // Tool without outputSchema should not be validated even if enabled
    Server s("test");
    s.enableOutputValidation();

    MCPTool tool;
    tool.name = "no_schema";
    tool.description = "No output schema";
    tool.inputSchemaJson = R"({"type":"object","properties":{}})";
    // No outputSchemaJson set
    tool.handler = [](JsonObject) -> String {
        return "anything goes";
    };
    s.addTool(tool);

    String r = dispatch(s, R"({"jsonrpc":"2.0","method":"tools/call","params":{"name":"no_schema","arguments":{}},"id":1})");
    ASSERT_STR_NOT_CONTAINS(r.c_str(), "Output validation failed");
    ASSERT_STR_CONTAINS(r.c_str(), "anything goes");
}

// ═══════════════════════════════════════════════════════════════════════

int main() {
    TEST_SUMMARY();
    return _tests_failed > 0 ? 1 : 0;
}
