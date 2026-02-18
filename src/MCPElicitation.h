/**
 * mcpd — Elicitation Support
 *
 * Implements MCP elicitation capability: allows the server to request
 * structured user input from the client. The client presents a form
 * to the user and returns their responses.
 *
 * This enables interactive workflows where the MCU needs user decisions
 * (e.g., "Which WiFi network to connect to?", "Set threshold to what value?").
 *
 * Usage:
 *   MCPElicitationRequest req;
 *   req.message = "Configure sensor thresholds";
 *   req.addTextField("temp_high", "High temperature alarm (°C)", true);
 *   req.addNumberField("interval", "Read interval (seconds)", false);
 *   req.addEnumField("unit", "Temperature unit", {"celsius", "fahrenheit"}, true);
 *   req.addBooleanField("enable_alarm", "Enable alarm?", false);
 *
 *   server.requestElicitation(req, [](const MCPElicitationResponse& resp) {
 *       if (resp.action == "accept") {
 *           String tempHigh = resp.getString("temp_high");
 *           int interval = resp.getInt("interval");
 *       }
 *   });
 */

#ifndef MCP_ELICITATION_H
#define MCP_ELICITATION_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include <map>

namespace mcpd {

/**
 * A field in an elicitation request (JSON Schema property).
 */
struct MCPElicitationField {
    String name;
    String title;        // human-readable label
    String description;
    String type;         // "string", "number", "integer", "boolean"
    bool required = false;
    String defaultValue;
    std::vector<String> enumValues;  // for enum/select fields
    double minimum = 0;
    double maximum = 0;
    bool hasMinimum = false;
    bool hasMaximum = false;

    void toJsonSchema(JsonObject& properties, JsonArray& requiredArr) const {
        JsonObject prop = properties[name].to<JsonObject>();
        prop["type"] = type;
        if (!title.isEmpty()) prop["title"] = title;
        if (!description.isEmpty()) prop["description"] = description;
        if (!defaultValue.isEmpty()) {
            if (type == "number" || type == "integer") {
                prop["default"] = atof(defaultValue.c_str());
            } else if (type == "boolean") {
                prop["default"] = (defaultValue == "true");
            } else {
                prop["default"] = defaultValue;
            }
        }
        if (!enumValues.empty()) {
            JsonArray arr = prop["enum"].to<JsonArray>();
            for (const auto& v : enumValues) arr.add(v);
        }
        if (hasMinimum) prop["minimum"] = minimum;
        if (hasMaximum) prop["maximum"] = maximum;
        if (required) requiredArr.add(name);
    }
};

/**
 * An elicitation request sent from server to client.
 * The client presents this as a form and returns user's input.
 */
struct MCPElicitationRequest {
    String message;  // Human-readable message explaining what's needed
    std::vector<MCPElicitationField> fields;

    /** Add a text field. */
    void addTextField(const char* name, const char* title, bool required = false,
                      const char* defaultVal = "") {
        MCPElicitationField f;
        f.name = name;
        f.title = title;
        f.type = "string";
        f.required = required;
        if (defaultVal && strlen(defaultVal) > 0) f.defaultValue = defaultVal;
        fields.push_back(f);
    }

    /** Add a number field. */
    void addNumberField(const char* name, const char* title, bool required = false,
                        double min = 0, double max = 0) {
        MCPElicitationField f;
        f.name = name;
        f.title = title;
        f.type = "number";
        f.required = required;
        if (min != 0 || max != 0) {
            f.minimum = min;
            f.maximum = max;
            f.hasMinimum = true;
            f.hasMaximum = true;
        }
        fields.push_back(f);
    }

    /** Add an integer field. */
    void addIntegerField(const char* name, const char* title, bool required = false,
                         int min = 0, int max = 0) {
        MCPElicitationField f;
        f.name = name;
        f.title = title;
        f.type = "integer";
        f.required = required;
        if (min != 0 || max != 0) {
            f.minimum = min;
            f.maximum = max;
            f.hasMinimum = true;
            f.hasMaximum = true;
        }
        fields.push_back(f);
    }

    /** Add a boolean field. */
    void addBooleanField(const char* name, const char* title, bool required = false,
                         bool defaultVal = false) {
        MCPElicitationField f;
        f.name = name;
        f.title = title;
        f.type = "boolean";
        f.required = required;
        f.defaultValue = defaultVal ? "true" : "false";
        fields.push_back(f);
    }

    /** Add an enum/select field. */
    void addEnumField(const char* name, const char* title,
                      std::vector<String> options, bool required = false) {
        MCPElicitationField f;
        f.name = name;
        f.title = title;
        f.type = "string";
        f.required = required;
        f.enumValues = std::move(options);
        fields.push_back(f);
    }

    /** Serialize to JSON-RPC request params. */
    void toJson(JsonObject& obj) const {
        obj["message"] = message;

        // Build JSON Schema for the requested content
        JsonObject schema = obj["requestedSchema"].to<JsonObject>();
        schema["type"] = "object";
        JsonObject properties = schema["properties"].to<JsonObject>();
        JsonArray required = schema["required"].to<JsonArray>();

        for (const auto& field : fields) {
            field.toJsonSchema(properties, required);
        }
    }

    /** Build a complete JSON-RPC request string. */
    String toJsonRpc(int requestId) const {
        JsonDocument doc;
        doc["jsonrpc"] = "2.0";
        doc["id"] = requestId;
        doc["method"] = "elicitation/create";
        JsonObject params = doc["params"].to<JsonObject>();
        toJson(params);
        String output;
        serializeJson(doc, output);
        return output;
    }
};

/**
 * Response from an elicitation request.
 */
struct MCPElicitationResponse {
    String action;  // "accept", "decline", or "cancel"
    JsonDocument contentDoc;  // holds the parsed content
    bool valid = false;

    /** Get a string value from the response content. */
    String getString(const char* key) const {
        if (action != "accept") return "";
        JsonVariantConst v = contentDoc[key];
        if (v.is<const char*>()) return String(v.as<const char*>());
        return "";
    }

    /** Get an integer value from the response content. */
    int getInt(const char* key, int defaultVal = 0) const {
        if (action != "accept") return defaultVal;
        JsonVariantConst v = contentDoc[key];
        if (v.is<int>()) return v.as<int>();
        return defaultVal;
    }

    /** Get a float value from the response content. */
    float getFloat(const char* key, float defaultVal = 0) const {
        if (action != "accept") return defaultVal;
        JsonVariantConst v = contentDoc[key];
        if (v.is<float>()) return v.as<float>();
        return defaultVal;
    }

    /** Get a boolean value from the response content. */
    bool getBool(const char* key, bool defaultVal = false) const {
        if (action != "accept") return defaultVal;
        JsonVariantConst v = contentDoc[key];
        if (v.is<bool>()) return v.as<bool>();
        return defaultVal;
    }

    /** Check if user accepted. */
    bool accepted() const { return action == "accept"; }

    /** Check if user declined. */
    bool declined() const { return action == "decline"; }

    /** Parse from JSON-RPC result. */
    static MCPElicitationResponse fromJson(const JsonObject& result) {
        MCPElicitationResponse resp;
        const char* act = result["action"].as<const char*>();
        resp.action = act ? act : "";

        if (resp.action == "accept" && !result["content"].isNull()) {
            // Copy content into our document
            JsonObject content = result["content"];
            for (JsonPair kv : content) {
                resp.contentDoc[kv.key()] = kv.value();
            }
        }

        resp.valid = !resp.action.isEmpty();
        return resp;
    }
};

/** Callback for elicitation responses. */
using MCPElicitationCallback = std::function<void(const MCPElicitationResponse& response)>;

/**
 * Manages pending elicitation requests.
 */
class ElicitationManager {
public:
    struct PendingRequest {
        int requestId;
        MCPElicitationCallback callback;
        unsigned long sentAt;
    };

    ElicitationManager() = default;

    /** Queue an elicitation request. Returns the request ID. */
    int queueRequest(const MCPElicitationRequest& request, MCPElicitationCallback callback) {
        int id = _nextId++;
        String jsonRpc = request.toJsonRpc(id);
        _pending.push_back({id, callback, millis()});
        _outgoing.push_back(jsonRpc);
        return id;
    }

    /** Get and clear pending outgoing messages. */
    std::vector<String> drainOutgoing() {
        std::vector<String> out;
        std::swap(out, _outgoing);
        return out;
    }

    /** Handle a response from the client. Returns true if matched. */
    bool handleResponse(int requestId, const JsonObject& result) {
        for (auto it = _pending.begin(); it != _pending.end(); ++it) {
            if (it->requestId == requestId) {
                MCPElicitationResponse resp = MCPElicitationResponse::fromJson(result);
                if (it->callback) {
                    it->callback(resp);
                }
                _pending.erase(it);
                return true;
            }
        }
        return false;
    }

    /** Check if there are pending requests. */
    bool hasPending() const { return !_pending.empty(); }

    /** Number of pending requests. */
    size_t pendingCount() const { return _pending.size(); }

    /** Timeout old requests (default: 120s — user needs time to fill form). */
    void pruneExpired(unsigned long timeoutMs = 120000) {
        unsigned long now = millis();
        _pending.erase(
            std::remove_if(_pending.begin(), _pending.end(),
                [now, timeoutMs](const PendingRequest& p) {
                    return (now - p.sentAt) > timeoutMs;
                }),
            _pending.end()
        );
    }

private:
    std::vector<PendingRequest> _pending;
    std::vector<String> _outgoing;
    int _nextId = 8000;  // Start high, different range from sampling
};

} // namespace mcpd

#endif // MCP_ELICITATION_H
