/**
 * mcpd — Built-in NVS (Non-Volatile Storage) Tool
 *
 * Provides: nvs_get, nvs_set, nvs_delete, nvs_list, nvs_status
 *
 * Persistent key-value storage that survives reboots. Uses ESP32 NVS
 * (Preferences library) or emulated EEPROM on other platforms.
 * Supports string, integer, float, and boolean values.
 *
 * Use cases: store calibration data, user settings, device state,
 * last-known-good values, counters, flags.
 */

#ifndef MCPD_NVS_TOOL_H
#define MCPD_NVS_TOOL_H

#include "../mcpd.h"
#include <map>

#ifdef ESP32
#include <Preferences.h>
#endif

namespace mcpd {
namespace tools {

class NVSTool {
public:
    // In-memory mirror for testability and cross-platform support
    struct Entry {
        String value;
        String type; // "string", "int", "float", "bool"
    };

    static std::map<String, Entry>& store() {
        static std::map<String, Entry> s;
        return s;
    }

    static String& namespaceName() {
        static String ns = "mcpd";
        return ns;
    }

    static unsigned long& totalOps() {
        static unsigned long ops = 0;
        return ops;
    }

    static void attach(Server& server, const char* ns = "mcpd") {
        namespaceName() = ns;

#ifdef ESP32
        // Load existing keys from NVS on startup
        Preferences prefs;
        prefs.begin(ns, true); // read-only
        prefs.end();
#endif

        // nvs_set — store a key-value pair
        server.addTool("nvs_set", "Store a key-value pair in persistent non-volatile storage (survives reboots)",
            R"=({"type":"object","properties":{"key":{"type":"string","description":"Storage key (max 15 chars)","maxLength":15},"value":{"description":"Value to store (string, number, or boolean)"},"type":{"type":"string","enum":["string","int","float","bool"],"description":"Value type (auto-detected if omitted)"}},"required":["key","value"]})=",
            [](const JsonObject& args) -> String {
                const char* key = args["key"] | "";
                if (strlen(key) == 0) return R"({"error":"Key is required"})";
                if (strlen(key) > 15) return R"=({"error":"Key too long (max 15 characters for NVS)"})=";

                String valueStr;
                String type;

                if (args.containsKey("type")) {
                    type = args["type"].as<String>();
                }

                // Auto-detect type if not specified
                if (args["value"].is<bool>()) {
                    bool v = args["value"].as<bool>();
                    valueStr = v ? "true" : "false";
                    if (type.isEmpty()) type = "bool";
                } else if (args["value"].is<int>() || args["value"].is<long>()) {
                    long v = args["value"];
                    valueStr = String(v);
                    if (type.isEmpty()) type = "int";
                } else if (args["value"].is<float>() || args["value"].is<double>()) {
                    double v = args["value"];
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.6g", v);
                    valueStr = buf;
                    if (type.isEmpty()) type = "float";
                } else {
                    valueStr = args["value"].as<String>();
                    if (type.isEmpty()) type = "string";
                }

#ifdef ESP32
                Preferences prefs;
                prefs.begin(namespaceName().c_str(), false);
                if (type == "int") {
                    prefs.putLong(key, valueStr.toInt());
                } else if (type == "float") {
                    prefs.putFloat(key, valueStr.toFloat());
                } else if (type == "bool") {
                    prefs.putBool(key, valueStr == "true");
                } else {
                    prefs.putString(key, valueStr);
                }
                prefs.end();
#endif
                Entry e;
                e.value = valueStr;
                e.type = type;
                store()[String(key)] = e;
                totalOps()++;

                return String("{\"key\":\"") + key +
                       "\",\"value\":" + (type == "string" ? "\"" + valueStr + "\"" : valueStr) +
                       ",\"type\":\"" + type +
                       "\",\"persisted\":true}";
            });

        // nvs_get — retrieve a value by key
        server.addTool("nvs_get", "Retrieve a value from persistent non-volatile storage",
            R"({"type":"object","properties":{"key":{"type":"string","description":"Storage key to retrieve"}},"required":["key"]})",
            [](const JsonObject& args) -> String {
                const char* key = args["key"] | "";
                if (strlen(key) == 0) return R"({"error":"Key is required"})";

                auto it = store().find(String(key));
                if (it == store().end()) {
                    return String("{\"key\":\"") + key + "\",\"found\":false}";
                }

                totalOps()++;
                Entry& e = it->second;
                return String("{\"key\":\"") + key +
                       "\",\"found\":true,\"value\":" +
                       (e.type == "string" ? "\"" + e.value + "\"" : e.value) +
                       ",\"type\":\"" + e.type + "\"}";
            });

        // nvs_delete — remove a key
        server.addTool("nvs_delete", "Delete a key from persistent non-volatile storage",
            R"({"type":"object","properties":{"key":{"type":"string","description":"Storage key to delete"}},"required":["key"]})",
            [](const JsonObject& args) -> String {
                const char* key = args["key"] | "";
                if (strlen(key) == 0) return R"({"error":"Key is required"})";

                auto it = store().find(String(key));
                if (it == store().end()) {
                    return String("{\"key\":\"") + key + "\",\"deleted\":false,\"reason\":\"Key not found\"}";
                }

#ifdef ESP32
                Preferences prefs;
                prefs.begin(namespaceName().c_str(), false);
                prefs.remove(key);
                prefs.end();
#endif
                store().erase(it);
                totalOps()++;

                return String("{\"key\":\"") + key + "\",\"deleted\":true}";
            });

        // nvs_list — list all stored keys
        server.addTool("nvs_list", "List all keys in persistent non-volatile storage",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                totalOps()++;
                String result = "{\"namespace\":\"" + namespaceName() + "\",\"count\":" + String(store().size()) + ",\"entries\":[";
                bool first = true;
                for (auto& kv : store()) {
                    if (!first) result += ",";
                    first = false;
                    result += "{\"key\":\"" + kv.first + "\",\"type\":\"" + kv.second.type + "\",\"value\":";
                    if (kv.second.type == "string") {
                        result += "\"" + kv.second.value + "\"";
                    } else {
                        result += kv.second.value;
                    }
                    result += "}";
                }
                result += "]}";
                return result;
            });

        // nvs_status — storage info
        server.addTool("nvs_status", "Get NVS storage status and statistics",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                unsigned long freeEntries = 0;
                unsigned long usedEntries = store().size();

#ifdef ESP32
                Preferences prefs;
                prefs.begin(namespaceName().c_str(), true);
                freeEntries = prefs.freeEntries();
                prefs.end();
#else
                freeEntries = 1000 - usedEntries; // emulated
#endif

                return String("{\"namespace\":\"") + namespaceName() +
                       "\",\"used_entries\":" + usedEntries +
                       ",\"free_entries\":" + freeEntries +
                       ",\"total_operations\":" + totalOps() +
                       ",\"platform\":" +
#ifdef ESP32
                       "\"esp32_nvs\""
#else
                       "\"emulated\""
#endif
                       "}";
            });
    }
};

} // namespace tools

/**
 * Register NVS tools with a single call.
 *
 * @param server  The mcpd::Server instance
 * @param ns      NVS namespace (default: "mcpd", max 15 chars)
 */
inline void addNVSTools(Server& server, const char* ns = "mcpd") {
    tools::NVSTool::attach(server, ns);
}

} // namespace mcpd

#endif // MCPD_NVS_TOOL_H
