/**
 * mcpd — Tool Input Validation
 *
 * Lightweight JSON Schema validation for tool call arguments.
 * Validates required fields and basic type constraints against the
 * declared inputSchema before invoking the tool handler.
 *
 * Designed for MCU environments: no heap-heavy schema libraries,
 * just practical checks that catch common caller mistakes early.
 *
 * Supported checks:
 *   - required: all required fields must be present
 *   - type: string, number, integer, boolean, array, object, null
 *   - enum: value must be one of the listed options (string enums)
 *   - minimum/maximum: numeric range constraints
 *   - minLength/maxLength: string length constraints
 *   - minItems/maxItems: array length constraints
 *   - pattern: not supported (too heavy for MCU)
 */

#ifndef MCPD_VALIDATION_H
#define MCPD_VALIDATION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

namespace mcpd {

struct ValidationError {
    String field;    // Field path (e.g. "temperature" or "config.mode")
    String message;  // Human-readable error description

    ValidationError() = default;
    ValidationError(const String& f, const String& m) : field(f), message(m) {}
};

struct ValidationResult {
    bool valid = true;
    std::vector<ValidationError> errors;

    void addError(const String& field, const String& message) {
        valid = false;
        errors.emplace_back(field, message);
    }

    /**
     * Format all errors as a single string for JSON-RPC error messages.
     */
    String toString() const {
        if (valid) return "OK";
        String result = "Invalid arguments: ";
        for (size_t i = 0; i < errors.size(); i++) {
            if (i > 0) result += "; ";
            if (!errors[i].field.isEmpty()) {
                result += "'" + errors[i].field + "' ";
            }
            result += errors[i].message;
        }
        return result;
    }

    /**
     * Serialize errors to JSON for structured error data.
     */
    String toJson() const {
        JsonDocument doc;
        JsonArray arr = doc["validationErrors"].to<JsonArray>();
        for (const auto& err : errors) {
            JsonObject obj = arr.add<JsonObject>();
            obj["field"] = err.field;
            obj["message"] = err.message;
        }
        String result;
        serializeJson(doc, result);
        return result;
    }
};

/**
 * Validate a JSON value against a type string.
 * Returns true if the value matches the expected type.
 */
inline bool validateType(JsonVariant value, const char* expectedType) {
    if (!expectedType) return true;

    if (strcmp(expectedType, "string") == 0)  return value.is<const char*>();
    if (strcmp(expectedType, "number") == 0)  return value.is<float>() || value.is<double>() || value.is<int>() || value.is<long>();
    if (strcmp(expectedType, "integer") == 0) return value.is<int>() || value.is<long>();
    if (strcmp(expectedType, "boolean") == 0) return value.is<bool>();
    if (strcmp(expectedType, "array") == 0)   return value.is<JsonArray>();
    if (strcmp(expectedType, "object") == 0)  return value.is<JsonObject>();
    if (strcmp(expectedType, "null") == 0)    return value.isNull();

    return true; // Unknown type → pass
}

/**
 * Get a human-readable type name for a JSON value.
 */
inline const char* jsonTypeName(JsonVariant value) {
    if (value.isNull())           return "null";
    if (value.is<bool>())         return "boolean";
    if (value.is<const char*>())  return "string";
    if (value.is<JsonArray>())    return "array";
    if (value.is<JsonObject>())   return "object";
    if (value.is<int>() || value.is<long>()) return "integer";
    if (value.is<float>() || value.is<double>()) return "number";
    return "unknown";
}

/**
 * Validate tool arguments against a parsed JSON Schema.
 *
 * @param args     The arguments object from tools/call
 * @param schema   The parsed inputSchema (must be type: "object")
 * @param prefix   Field path prefix for nested validation
 * @return ValidationResult with any errors found
 */
inline ValidationResult validateArguments(JsonObject args, JsonObject schema,
                                           const String& prefix = "") {
    ValidationResult result;

    // Check required fields
    if (schema.containsKey("required")) {
        JsonArray required = schema["required"].as<JsonArray>();
        for (JsonVariant req : required) {
            const char* fieldName = req.as<const char*>();
            if (!fieldName) continue;

            String fullPath = prefix.isEmpty() ? String(fieldName)
                                               : prefix + "." + fieldName;

            if (!args.containsKey(fieldName) || args[fieldName].isNull()) {
                result.addError(fullPath, "is required");
            }
        }
    }

    // Check property types and constraints
    if (schema.containsKey("properties")) {
        JsonObject properties = schema["properties"].as<JsonObject>();

        for (JsonPair prop : properties) {
            const char* fieldName = prop.key().c_str();
            if (!args.containsKey(fieldName)) continue; // Missing optional fields are fine

            JsonVariant value = args[fieldName];
            JsonObject propSchema = prop.value().as<JsonObject>();
            String fullPath = prefix.isEmpty() ? String(fieldName)
                                               : prefix + "." + fieldName;

            // Skip null values for optional fields
            if (value.isNull()) continue;

            // Type check
            if (propSchema.containsKey("type")) {
                const char* expectedType = propSchema["type"].as<const char*>();
                if (expectedType && !validateType(value, expectedType)) {
                    String msg = "must be ";
                    msg += expectedType;
                    msg += ", got ";
                    msg += jsonTypeName(value);
                    result.addError(fullPath, msg);
                    continue; // Skip further checks on wrong type
                }
            }

            // Enum check (string values)
            if (propSchema.containsKey("enum")) {
                JsonArray enumValues = propSchema["enum"].as<JsonArray>();
                bool found = false;
                for (JsonVariant ev : enumValues) {
                    if (value.is<const char*>() && ev.is<const char*>()) {
                        if (strcmp(value.as<const char*>(), ev.as<const char*>()) == 0) {
                            found = true;
                            break;
                        }
                    } else if (value.is<int>() && ev.is<int>()) {
                        if (value.as<int>() == ev.as<int>()) {
                            found = true;
                            break;
                        }
                    } else if (value.is<double>() && ev.is<double>()) {
                        if (value.as<double>() == ev.as<double>()) {
                            found = true;
                            break;
                        }
                    } else if (value.is<bool>() && ev.is<bool>()) {
                        if (value.as<bool>() == ev.as<bool>()) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    String msg = "must be one of [";
                    bool first = true;
                    for (JsonVariant ev : enumValues) {
                        if (!first) msg += ", ";
                        if (ev.is<const char*>()) {
                            msg += "\"";
                            msg += ev.as<const char*>();
                            msg += "\"";
                        } else if (ev.is<int>() || ev.is<long>()) {
                            msg += String(ev.as<long>());
                        } else if (ev.is<double>() || ev.is<float>()) {
                            msg += String(ev.as<double>(), 2);
                        } else if (ev.is<bool>()) {
                            msg += ev.as<bool>() ? "true" : "false";
                        } else {
                            msg += "?";
                        }
                        first = false;
                    }
                    msg += "]";
                    result.addError(fullPath, msg);
                }
            }

            // Numeric range: minimum/maximum
            if (propSchema.containsKey("minimum") && (value.is<int>() || value.is<long>() || value.is<float>() || value.is<double>())) {
                double min = propSchema["minimum"].as<double>();
                if (value.as<double>() < min) {
                    result.addError(fullPath, "must be >= " + String(min, 2));
                }
            }
            if (propSchema.containsKey("maximum") && (value.is<int>() || value.is<long>() || value.is<float>() || value.is<double>())) {
                double max = propSchema["maximum"].as<double>();
                if (value.as<double>() > max) {
                    result.addError(fullPath, "must be <= " + String(max, 2));
                }
            }

            // String length: minLength/maxLength
            if (value.is<const char*>()) {
                size_t len = strlen(value.as<const char*>());
                if (propSchema.containsKey("minLength")) {
                    size_t minLen = propSchema["minLength"].as<size_t>();
                    if (len < minLen) {
                        result.addError(fullPath, "length must be >= " + String((unsigned long)minLen));
                    }
                }
                if (propSchema.containsKey("maxLength")) {
                    size_t maxLen = propSchema["maxLength"].as<size_t>();
                    if (len > maxLen) {
                        result.addError(fullPath, "length must be <= " + String((unsigned long)maxLen));
                    }
                }
            }

            // Array length: minItems/maxItems
            if (value.is<JsonArray>()) {
                size_t len = value.as<JsonArray>().size();
                if (propSchema.containsKey("minItems")) {
                    size_t minItems = propSchema["minItems"].as<size_t>();
                    if (len < minItems) {
                        result.addError(fullPath, "must have >= " + String((unsigned long)minItems) + " items");
                    }
                }
                if (propSchema.containsKey("maxItems")) {
                    size_t maxItems = propSchema["maxItems"].as<size_t>();
                    if (len > maxItems) {
                        result.addError(fullPath, "must have <= " + String((unsigned long)maxItems) + " items");
                    }
                }
            }

            // Recursive validation for nested objects
            if (value.is<JsonObject>() && propSchema.containsKey("properties")) {
                ValidationResult nested = validateArguments(
                    value.as<JsonObject>(), propSchema, fullPath);
                if (!nested.valid) {
                    for (const auto& err : nested.errors) {
                        result.addError(err.field, err.message);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace mcpd

#endif // MCPD_VALIDATION_H
