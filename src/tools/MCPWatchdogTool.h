/**
 * MCPWatchdogTool — Hardware watchdog management for production deployments
 *
 * Tools: watchdog_status, watchdog_enable, watchdog_feed, watchdog_disable
 */

#ifndef MCP_WATCHDOG_TOOL_H
#define MCP_WATCHDOG_TOOL_H

#include "../MCPTool.h"
#include <Arduino.h>

#ifdef ESP32
#include <esp_task_wdt.h>
#endif

namespace mcpd {
namespace tools {

class WatchdogTool {
public:
    static unsigned long _lastFeedMs;
    static bool _enabled;
    static int _timeoutSec;

    template<typename ServerT>
    static void registerAll(ServerT& server) {
        // watchdog_status
        {
            MCPTool tool;
            tool.name = "watchdog_status";
            tool.description = "Get hardware watchdog status";
            tool.inputSchemaJson = "{\"type\":\"object\",\"properties\":{}}";
            tool.handler = [](const JsonObject&) -> String {
                JsonDocument doc;
                doc["enabled"] = _enabled;
                doc["timeout_seconds"] = _timeoutSec;
                if (_enabled && _lastFeedMs > 0) {
                    unsigned long elapsed = (millis() - _lastFeedMs) / 1000;
                    doc["seconds_since_feed"] = elapsed;
                    doc["remaining_seconds"] = (_timeoutSec > (int)elapsed) ?
                                                (_timeoutSec - (int)elapsed) : 0;
                }
                doc["uptime_seconds"] = millis() / 1000;
                String result;
                serializeJson(doc, result);
                return result;
            };
            tool.annotations.title = "Watchdog Status";
            tool.annotations.readOnlyHint = true;
            tool.annotations.openWorldHint = false;
            server.addTool(tool);
        }

        // watchdog_enable
        {
            MCPTool tool;
            tool.name = "watchdog_enable";
            tool.description = "Enable the hardware watchdog timer with a timeout in seconds (1-120)";
            tool.inputSchemaJson = "{\"type\":\"object\",\"properties\":{\"timeout_seconds\":{\"type\":\"integer\",\"description\":\"Watchdog timeout in seconds\"},\"panic\":{\"type\":\"boolean\",\"description\":\"Trigger panic on timeout\"}}}";
            tool.handler = [](const JsonObject& args) -> String {
                int timeout = args["timeout_seconds"] | 30;
                if (timeout < 1) timeout = 1;
                if (timeout > 120) timeout = 120;
#ifdef ESP32
                esp_task_wdt_config_t config = {
                    .timeout_ms = (uint32_t)(timeout * 1000),
                    .idle_core_mask = 0,
                    .trigger_panic = args["panic"] | false
                };
                esp_err_t err = esp_task_wdt_reconfigure(&config);
                if (err == ESP_ERR_INVALID_STATE) {
                    err = esp_task_wdt_init(&config);
                    if (err == ESP_OK) esp_task_wdt_add(NULL);
                }
                if (err != ESP_OK) {
                    return String("{\"error\":\"") + esp_err_to_name(err) + "\"}";
                }
#endif
                _enabled = true;
                _timeoutSec = timeout;
                _lastFeedMs = millis();
                JsonDocument doc;
                doc["status"] = "enabled";
                doc["timeout_seconds"] = timeout;
                String result;
                serializeJson(doc, result);
                return result;
            };
            tool.annotations.title = "Enable Watchdog";
            tool.annotations.readOnlyHint = false;
            tool.annotations.destructiveHint = true;
            server.addTool(tool);
        }

        // watchdog_feed
        {
            MCPTool tool;
            tool.name = "watchdog_feed";
            tool.description = "Feed (reset) the watchdog timer to prevent device reset";
            tool.inputSchemaJson = "{\"type\":\"object\",\"properties\":{}}";
            tool.handler = [](const JsonObject&) -> String {
                if (!_enabled) return "{\"error\":\"Watchdog not enabled\"}";
#ifdef ESP32
                esp_task_wdt_reset();
#endif
                _lastFeedMs = millis();
                JsonDocument doc;
                doc["status"] = "fed";
                doc["timestamp_ms"] = _lastFeedMs;
                String result;
                serializeJson(doc, result);
                return result;
            };
            tool.annotations.title = "Feed Watchdog";
            tool.annotations.readOnlyHint = false;
            server.addTool(tool);
        }

        // watchdog_disable
        {
            MCPTool tool;
            tool.name = "watchdog_disable";
            tool.description = "Disable the hardware watchdog timer";
            tool.inputSchemaJson = "{\"type\":\"object\",\"properties\":{}}";
            tool.handler = [](const JsonObject&) -> String {
#ifdef ESP32
                esp_task_wdt_delete(NULL);
                esp_task_wdt_deinit();
#endif
                _enabled = false;
                _timeoutSec = 0;
                return "{\"status\":\"disabled\"}";
            };
            tool.annotations.title = "Disable Watchdog";
            tool.annotations.readOnlyHint = false;
            tool.annotations.destructiveHint = true;
            server.addTool(tool);
        }
    }
};

unsigned long WatchdogTool::_lastFeedMs = 0;
bool WatchdogTool::_enabled = false;
int WatchdogTool::_timeoutSec = 0;

} // namespace tools
} // namespace mcpd

#endif // MCP_WATCHDOG_TOOL_H
