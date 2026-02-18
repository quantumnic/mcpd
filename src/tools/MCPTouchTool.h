/**
 * mcpd — Capacitive Touch Sensor Tool (ESP32)
 *
 * Provides MCP tools for reading ESP32 capacitive touch pins.
 * Supports threshold-based touch detection, multi-pad monitoring,
 * and calibration.
 *
 * Tools:
 *   - touch_read:      Read raw touch value from a pin
 *   - touch_read_all:  Read all registered touch pads at once
 *   - touch_calibrate: Auto-calibrate touch threshold from current readings
 *
 * ESP32 Touch Pins: T0(GPIO4), T1(GPIO0), T2(GPIO2), T3(GPIO15),
 *   T4(GPIO13), T5(GPIO12), T6(GPIO14), T7(GPIO27), T8(GPIO33), T9(GPIO32)
 *
 * MIT License
 */

#ifndef MCPD_TOUCH_TOOL_H
#define MCPD_TOUCH_TOOL_H

#include "../MCPTool.h"

namespace mcpd {

struct TouchPadConfig {
    int gpio = -1;
    int touchNum = -1;          // T0-T9
    uint16_t threshold = 40;    // below this = touched (ESP32 touch values decrease on touch)
    uint16_t baseline = 0;      // calibrated no-touch value
    const char* label = "";
};

static TouchPadConfig _touchPads[10];
static int _touchPadCount = 0;

/**
 * Register touch pads and add MCP tools.
 *
 * @param server  The MCP server
 * @param gpios   Array of GPIO pin numbers to use as touch inputs
 * @param count   Number of pins
 * @param labels  Optional array of human labels (e.g. "button_a")
 */
inline void addTouchTools(Server& server, const int* gpios, int count,
                          const char** labels = nullptr) {
    // Map GPIO → touch number
    static const int gpioToTouch[] = {
        // GPIO: touch#  (-1 = not a touch pin)
        [0] = 1, [2] = 2, [4] = 0, [12] = 5, [13] = 4,
        [14] = 6, [15] = 3, [27] = 7, [32] = 9, [33] = 8
    };

    for (int i = 0; i < count && _touchPadCount < 10; i++) {
        int gpio = gpios[i];
        if (gpio < 0 || gpio > 33) continue;

        _touchPads[_touchPadCount].gpio = gpio;
        _touchPads[_touchPadCount].touchNum = gpioToTouch[gpio];
        _touchPads[_touchPadCount].label = (labels && labels[i]) ? labels[i] : "";

        // Initial read for baseline
        _touchPads[_touchPadCount].baseline = touchRead(gpio);
        _touchPads[_touchPadCount].threshold = _touchPads[_touchPadCount].baseline * 0.6;

        _touchPadCount++;
    }

    // Only register tools once
    if (_touchPadCount == count) {

        // ── touch_read ─────────────────────────────────────────────

        MCPTool readTool;
        readTool.name = "touch_read";
        readTool.description = "Read capacitive touch sensor value from a specific pad. Lower values indicate touch.";
        readTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Touch pad index (0-based, in registration order)",
                    "minimum": 0
                },
                "gpio": {
                    "type": "integer",
                    "description": "GPIO pin number (alternative to index)"
                }
            }
        })=";
        readTool.annotations.title = "Read Touch Sensor";
        readTool.annotations.readOnlyHint = true;

        readTool.handler = [](const JsonObject& params) -> String {
            int idx = -1;

            if (params.containsKey("gpio")) {
                int gpio = params["gpio"].as<int>();
                for (int i = 0; i < _touchPadCount; i++) {
                    if (_touchPads[i].gpio == gpio) { idx = i; break; }
                }
            } else {
                idx = params["index"] | 0;
            }

            if (idx < 0 || idx >= _touchPadCount) {
                return R"({"error":"Invalid touch pad"})";
            }

            TouchPadConfig& pad = _touchPads[idx];
            uint16_t value = touchRead(pad.gpio);
            bool touched = value < pad.threshold;

            JsonDocument doc;
            doc["index"] = idx;
            doc["gpio"] = pad.gpio;
            doc["value"] = value;
            doc["threshold"] = pad.threshold;
            doc["baseline"] = pad.baseline;
            doc["touched"] = touched;
            if (strlen(pad.label) > 0) doc["label"] = pad.label;
            // Touch strength as percentage (0=not touched, 100=strong touch)
            if (pad.baseline > 0) {
                int strength = constrain((int)(100.0f * (1.0f - (float)value / pad.baseline)), 0, 100);
                doc["strength_percent"] = strength;
            }

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(readTool);

        // ── touch_read_all ─────────────────────────────────────────

        MCPTool readAllTool;
        readAllTool.name = "touch_read_all";
        readAllTool.description = "Read all registered capacitive touch pads at once. Returns array of readings with touch state.";
        readAllTool.inputSchemaJson = R"({"type":"object","properties":{}})";
        readAllTool.annotations.title = "Read All Touch Sensors";
        readAllTool.annotations.readOnlyHint = true;

        readAllTool.handler = [](const JsonObject&) -> String {
            JsonDocument doc;
            JsonArray pads = doc["pads"].to<JsonArray>();
            int touchedCount = 0;

            for (int i = 0; i < _touchPadCount; i++) {
                TouchPadConfig& pad = _touchPads[i];
                uint16_t value = touchRead(pad.gpio);
                bool touched = value < pad.threshold;
                if (touched) touchedCount++;

                JsonObject p = pads.add<JsonObject>();
                p["index"] = i;
                p["gpio"] = pad.gpio;
                p["value"] = value;
                p["touched"] = touched;
                if (strlen(pad.label) > 0) p["label"] = pad.label;
            }

            doc["total_pads"] = _touchPadCount;
            doc["touched_count"] = touchedCount;

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(readAllTool);

        // ── touch_calibrate ────────────────────────────────────────

        MCPTool calTool;
        calTool.name = "touch_calibrate";
        calTool.description = "Calibrate touch thresholds from current readings. Run with all pads untouched to set accurate baselines.";
        calTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "samples": {
                    "type": "integer",
                    "description": "Number of samples to average (default 10)",
                    "default": 10,
                    "minimum": 1,
                    "maximum": 100
                },
                "sensitivity": {
                    "type": "number",
                    "description": "Touch threshold as fraction of baseline (default 0.6, lower = more sensitive)",
                    "default": 0.6,
                    "minimum": 0.1,
                    "maximum": 0.95
                }
            }
        })=";
        calTool.annotations.title = "Calibrate Touch";
        calTool.annotations.readOnlyHint = false;

        calTool.handler = [](const JsonObject& params) -> String {
            int samples = params["samples"] | 10;
            float sensitivity = params["sensitivity"] | 0.6f;

            JsonDocument doc;
            JsonArray pads = doc["pads"].to<JsonArray>();

            for (int i = 0; i < _touchPadCount; i++) {
                TouchPadConfig& pad = _touchPads[i];

                // Average multiple samples
                uint32_t sum = 0;
                for (int s = 0; s < samples; s++) {
                    sum += touchRead(pad.gpio);
                    delay(5);
                }
                pad.baseline = sum / samples;
                pad.threshold = (uint16_t)(pad.baseline * sensitivity);

                JsonObject p = pads.add<JsonObject>();
                p["index"] = i;
                p["gpio"] = pad.gpio;
                p["baseline"] = pad.baseline;
                p["threshold"] = pad.threshold;
                if (strlen(pad.label) > 0) p["label"] = pad.label;
            }

            doc["samples"] = samples;
            doc["sensitivity"] = serialized(String(sensitivity, 2));
            doc["calibrated"] = true;

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(calTool);
    }
}

} // namespace mcpd

#endif // MCPD_TOUCH_TOOL_H
