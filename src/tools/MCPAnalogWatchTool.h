/**
 * mcpd — Analog Watchdog Tool
 *
 * Monitors analog pins and reports threshold crossings.
 * Useful for: voltage monitoring, battery level, light sensors, soil moisture.
 *
 * Tools:
 *   - analog_watch_set:    Configure threshold monitoring on an ADC pin
 *   - analog_watch_status: Check current status of all watches
 *   - analog_watch_clear:  Remove a watch
 */

#ifndef MCP_ANALOG_WATCH_TOOL_H
#define MCP_ANALOG_WATCH_TOOL_H

#include "../MCPTool.h"
#include <Arduino.h>

namespace mcpd {

struct AnalogWatch {
    uint8_t pin = 0;
    uint16_t lowThreshold = 0;
    uint16_t highThreshold = 4095;
    bool active = false;
    bool triggered = false;
    const char* triggerType = "none";   // "above", "below", "none"
    uint16_t lastReading = 0;
    unsigned long lastCheckMs = 0;
    unsigned long triggeredAtMs = 0;
    String label;
};

static AnalogWatch _analogWatches[8];
static uint8_t _analogWatchCount = 0;

static int _findAnalogWatch(uint8_t pin) {
    for (uint8_t i = 0; i < _analogWatchCount; i++) {
        if (_analogWatches[i].pin == pin && _analogWatches[i].active) return i;
    }
    return -1;
}

// Call this in loop() to update watch states
inline void updateAnalogWatches() {
    unsigned long now = millis();
    for (uint8_t i = 0; i < _analogWatchCount; i++) {
        if (!_analogWatches[i].active) continue;
        // Check every 100ms
        if (now - _analogWatches[i].lastCheckMs < 100) continue;
        _analogWatches[i].lastCheckMs = now;

        uint16_t reading = analogRead(_analogWatches[i].pin);
        _analogWatches[i].lastReading = reading;

        bool wasTriggered = _analogWatches[i].triggered;
        if (reading > _analogWatches[i].highThreshold) {
            _analogWatches[i].triggered = true;
            _analogWatches[i].triggerType = "above";
            if (!wasTriggered) _analogWatches[i].triggeredAtMs = now;
        } else if (reading < _analogWatches[i].lowThreshold) {
            _analogWatches[i].triggered = true;
            _analogWatches[i].triggerType = "below";
            if (!wasTriggered) _analogWatches[i].triggeredAtMs = now;
        } else {
            _analogWatches[i].triggered = false;
            _analogWatches[i].triggerType = "none";
        }
    }
}

inline void addAnalogWatchTools(Server& server) {
    // analog_watch_set — configure threshold monitoring
    server.addTool(
        MCPTool("analog_watch_set", "Set up analog threshold monitoring on a pin",
            R"=({"type":"object","properties":{
                "pin":{"type":"integer","description":"Analog pin number"},
                "low":{"type":"integer","description":"Low threshold (trigger if reading below this)","default":0},
                "high":{"type":"integer","description":"High threshold (trigger if reading above this)","default":4095},
                "label":{"type":"string","description":"Human-readable label for this watch"}
            },"required":["pin"]})=",
            [](const JsonObject& args) -> String {
                int pin = args["pin"] | -1;
                if (pin < 0) return R"({"error":"Pin number required"})";

                int slot = _findAnalogWatch((uint8_t)pin);
                if (slot < 0) {
                    if (_analogWatchCount >= 8) return R"({"error":"Maximum 8 analog watches supported"})";
                    slot = _analogWatchCount++;
                }

                _analogWatches[slot].pin = (uint8_t)pin;
                _analogWatches[slot].lowThreshold = args["low"] | 0;
                _analogWatches[slot].highThreshold = args["high"] | 4095;
                _analogWatches[slot].active = true;
                _analogWatches[slot].triggered = false;
                _analogWatches[slot].triggerType = "none";
                _analogWatches[slot].lastCheckMs = 0;

                const char* label = args["label"] | "";
                _analogWatches[slot].label = label;

                // Take initial reading
                uint16_t initial = analogRead(pin);
                _analogWatches[slot].lastReading = initial;

                return String(R"({"pin":)") + pin +
                       R"(,"low":)" + _analogWatches[slot].lowThreshold +
                       R"(,"high":)" + _analogWatches[slot].highThreshold +
                       R"(,"label":")" + _analogWatches[slot].label +
                       R"(","initial_reading":)" + initial +
                       R"(,"active":true})";
            }
        ).annotate(MCPToolAnnotations().setIdempotentHint(true))
    );

    // analog_watch_status — check all watches
    server.addTool(
        MCPTool("analog_watch_status", "Get status of all analog threshold watches",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                String result = R"({"watches":[)";
                bool first = true;
                int alertCount = 0;
                for (uint8_t i = 0; i < _analogWatchCount; i++) {
                    if (!_analogWatches[i].active) continue;
                    if (!first) result += ",";
                    first = false;
                    if (_analogWatches[i].triggered) alertCount++;
                    result += String(R"({"pin":)") + _analogWatches[i].pin +
                              R"(,"label":")" + _analogWatches[i].label +
                              R"(","reading":)" + _analogWatches[i].lastReading +
                              R"(,"low":)" + _analogWatches[i].lowThreshold +
                              R"(,"high":)" + _analogWatches[i].highThreshold +
                              R"(,"triggered":)" + (_analogWatches[i].triggered ? "true" : "false") +
                              R"(,"trigger":")" + _analogWatches[i].triggerType + R"("})";
                }
                result += String(R"(],"total_active":)") + _analogWatchCount +
                          R"(,"alerts":)" + alertCount + "}";
                return result;
            }
        ).annotate(MCPToolAnnotations().setReadOnlyHint(true))
    );

    // analog_watch_clear — remove a watch
    server.addTool(
        MCPTool("analog_watch_clear", "Remove analog threshold watch from a pin",
            R"({"type":"object","properties":{
                "pin":{"type":"integer","description":"Analog pin number"}
            },"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"] | -1;
                if (pin < 0) return R"({"error":"Pin number required"})";

                int slot = _findAnalogWatch((uint8_t)pin);
                if (slot < 0) {
                    return String(R"({"error":"No watch on pin )") + pin + R"("})";
                }

                _analogWatches[slot].active = false;
                return String(R"({"pin":)") + pin + R"(,"cleared":true})";
            }
        )
    );
}

} // namespace mcpd

#endif // MCP_ANALOG_WATCH_TOOL_H
