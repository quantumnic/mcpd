/**
 * mcpd — Power Management Tool
 *
 * Provides deep sleep, light sleep, watchdog timer, and power info
 * for battery-powered MCU projects.
 */

#ifndef MCPD_POWER_TOOL_H
#define MCPD_POWER_TOOL_H

#include "../MCPTool.h"

namespace mcpd {

/**
 * Register power management tools on the server.
 *
 * Tools registered:
 *   - power_info      — read power state (uptime, reset reason, free heap, VCC if available)
 *   - power_deep_sleep — enter deep sleep for N microseconds (or indefinitely until ext wakeup)
 *   - power_light_sleep — enter light sleep for N microseconds
 *   - power_restart    — software restart the MCU
 *   - power_watchdog   — configure/feed/disable hardware watchdog timer
 */
inline void registerPowerTools(Server& server) {

    // ── power_info ─────────────────────────────────────────────────
    {
        MCPTool tool(
            "power_info",
            "Read power and system state: uptime, reset reason, free heap, CPU frequency",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                JsonDocument doc;
                doc["uptime_ms"] = millis();
                doc["uptime_s"] = millis() / 1000;
                doc["free_heap"] = ESP.getFreeHeap();
                doc["min_free_heap"] = ESP.getMinFreeHeap();
                doc["heap_size"] = ESP.getHeapSize();
                doc["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
                doc["chip_model"] = ESP.getChipModel();
                doc["chip_revision"] = ESP.getChipRevision();
                doc["flash_size"] = ESP.getFlashChipSize();
                doc["sdk_version"] = ESP.getSdkVersion();

                // Reset reason
                esp_reset_reason_t reason = esp_reset_reason();
                const char* reasons[] = {
                    "unknown", "power_on", "ext_pin", "software",
                    "panic", "int_watchdog", "task_watchdog", "watchdog",
                    "deepsleep", "brownout", "sdio"
                };
                int ri = (int)reason;
                doc["reset_reason"] = (ri >= 0 && ri <= 10) ? reasons[ri] : "unknown";

                // Wakeup cause
                esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
                const char* wakeups[] = {
                    "not_sleep", "unused", "ext0", "ext1", "timer",
                    "touchpad", "ulp", "gpio", "uart", "wifi", "cocpu",
                    "cocpu_trap", "bt"
                };
                int wi = (int)wakeup;
                doc["wakeup_cause"] = (wi >= 0 && wi <= 12) ? wakeups[wi] : "unknown";

                #ifdef CONFIG_IDF_TARGET_ESP32S3
                doc["psram_size"] = ESP.getPsramSize();
                doc["free_psram"] = ESP.getFreePsram();
                #endif

                String output;
                serializeJson(doc, output);
                return output;
            }
        );
        tool.markReadOnly().markLocalOnly();
        server.addTool(tool);
    }

    // ── power_deep_sleep ───────────────────────────────────────────
    {
        MCPTool tool(
            "power_deep_sleep",
            "Enter deep sleep mode. Wakes after specified duration or on external pin. "
            "WARNING: This stops all execution — the MCU will reset on wake.",
            R"=({
                "type":"object",
                "properties":{
                    "duration_us":{"type":"integer","description":"Sleep duration in microseconds (0 = indefinite, wake by ext pin only)"},
                    "ext_wakeup_pin":{"type":"integer","description":"GPIO pin for external wakeup (EXT0, active high)"},
                    "ext_wakeup_level":{"type":"integer","enum":[0,1],"description":"Wakeup level: 0=low, 1=high (default 1)"}
                }
            })=",
            [](const JsonObject& args) -> String {
                uint64_t duration = 0;
                if (args.containsKey("duration_us")) {
                    duration = args["duration_us"].as<uint64_t>();
                }

                if (args.containsKey("ext_wakeup_pin")) {
                    int pin = args["ext_wakeup_pin"].as<int>();
                    int level = args.containsKey("ext_wakeup_level")
                        ? args["ext_wakeup_level"].as<int>() : 1;
                    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level);
                }

                if (duration > 0) {
                    esp_sleep_enable_timer_wakeup(duration);
                }

                Serial.printf("[mcpd] Entering deep sleep (duration=%llu us)\n", duration);
                Serial.flush();
                esp_deep_sleep_start();

                return R"({"status":"entering_deep_sleep"})"; // Won't reach here
            }
        );
        tool.markLocalOnly();
        server.addTool(tool);
    }

    // ── power_light_sleep ──────────────────────────────────────────
    {
        MCPTool tool(
            "power_light_sleep",
            "Enter light sleep mode for specified duration. CPU pauses but RAM is retained. "
            "Returns after waking.",
            R"({
                "type":"object",
                "properties":{
                    "duration_us":{"type":"integer","description":"Sleep duration in microseconds","minimum":1000}
                },
                "required":["duration_us"]
            })",
            [](const JsonObject& args) -> String {
                uint64_t duration = args["duration_us"].as<uint64_t>();
                if (duration < 1000) {
                    return R"({"error":"duration_us must be >= 1000"})";
                }

                esp_sleep_enable_timer_wakeup(duration);
                unsigned long before = millis();
                esp_err_t err = esp_light_sleep_start();
                unsigned long slept = millis() - before;

                JsonDocument doc;
                doc["status"] = (err == ESP_OK) ? "woke_up" : "error";
                doc["requested_us"] = duration;
                doc["actual_sleep_ms"] = slept;
                doc["wakeup_cause"] = (int)esp_sleep_get_wakeup_cause();

                String output;
                serializeJson(doc, output);
                return output;
            }
        );
        tool.markLocalOnly().markIdempotent();
        server.addTool(tool);
    }

    // ── power_restart ──────────────────────────────────────────────
    {
        MCPTool tool(
            "power_restart",
            "Software restart (reboot) the microcontroller. WARNING: Disconnects all clients.",
            R"=({"type":"object","properties":{"delay_ms":{"type":"integer","description":"Delay before restart in ms (default 100)","minimum":0,"maximum":10000}}})=",
            [](const JsonObject& args) -> String {
                int delay_ms = args.containsKey("delay_ms")
                    ? args["delay_ms"].as<int>() : 100;
                if (delay_ms < 0) delay_ms = 0;
                if (delay_ms > 10000) delay_ms = 10000;

                Serial.printf("[mcpd] Restarting in %d ms\n", delay_ms);
                Serial.flush();
                delay(delay_ms);
                ESP.restart();

                return R"({"status":"restarting"})"; // Won't reach here
            }
        );
        tool.markLocalOnly();
        server.addTool(tool);
    }

    // ── power_watchdog ─────────────────────────────────────────────
    {
        MCPTool tool(
            "power_watchdog",
            "Configure, feed, or disable the task watchdog timer (TWDT). "
            "Useful for detecting hangs in long-running operations.",
            R"=({
                "type":"object",
                "properties":{
                    "action":{"type":"string","enum":["enable","feed","disable"],"description":"Watchdog action"},
                    "timeout_s":{"type":"integer","description":"Watchdog timeout in seconds (for enable)","minimum":1,"maximum":120}
                },
                "required":["action"]
            })=",
            [](const JsonObject& args) -> String {
                const char* action = args["action"];
                if (!action) return R"({"error":"missing action"})";

                String act(action);

                if (act == "enable") {
                    int timeout = args.containsKey("timeout_s")
                        ? args["timeout_s"].as<int>() : 10;
                    if (timeout < 1) timeout = 1;
                    if (timeout > 120) timeout = 120;

                    esp_task_wdt_config_t config = {
                        .timeout_ms = (uint32_t)(timeout * 1000),
                        .idle_core_mask = 0,
                        .trigger_panic = true
                    };
                    esp_err_t err = esp_task_wdt_reconfigure(&config);
                    if (err == ESP_OK) {
                        // Subscribe current task if not already
                        esp_task_wdt_add(NULL);
                    }

                    JsonDocument doc;
                    doc["status"] = (err == ESP_OK) ? "enabled" : "error";
                    doc["timeout_s"] = timeout;
                    if (err != ESP_OK) doc["esp_err"] = (int)err;
                    String out;
                    serializeJson(doc, out);
                    return out;
                }
                else if (act == "feed") {
                    esp_err_t err = esp_task_wdt_reset();
                    JsonDocument doc;
                    doc["status"] = (err == ESP_OK) ? "fed" : "error";
                    if (err != ESP_OK) doc["esp_err"] = (int)err;
                    String out;
                    serializeJson(doc, out);
                    return out;
                }
                else if (act == "disable") {
                    esp_task_wdt_delete(NULL);
                    return R"({"status":"disabled"})";
                }

                return R"({"error":"unknown action"})";
            }
        );
        tool.markLocalOnly();
        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_POWER_TOOL_H
