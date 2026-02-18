/**
 * mcpd — Interrupt Monitor Tool
 *
 * Provides GPIO interrupt counting and edge-event detection as MCP tools.
 * Useful for: button presses, encoder ticks, flow sensors, reed switches.
 *
 * Tools:
 *   - interrupt_attach:  Configure a pin for interrupt monitoring
 *   - interrupt_read:    Read interrupt count and last trigger time
 *   - interrupt_detach:  Stop monitoring a pin
 *   - interrupt_list:    List all monitored pins
 */

#ifndef MCP_INTERRUPT_TOOL_H
#define MCP_INTERRUPT_TOOL_H

#include "../MCPTool.h"
#include <Arduino.h>
#include <functional>
#include <map>

namespace mcpd {

// ── Interrupt tracking state ───────────────────────────────────────────

struct InterruptState {
    volatile uint32_t count = 0;
    volatile unsigned long lastTriggerUs = 0;
    uint8_t pin = 0;
    int mode = RISING;       // RISING, FALLING, CHANGE
    bool active = false;
    unsigned long attachedAt = 0;
};

// Up to 8 simultaneous interrupt pins
static InterruptState _intStates[8];
static uint8_t _intCount = 0;

// ISR templates for each slot
#define MCPD_ISR(n) \
    static void IRAM_ATTR _intISR##n() { \
        _intStates[n].count++; \
        _intStates[n].lastTriggerUs = micros(); \
    }

MCPD_ISR(0) MCPD_ISR(1) MCPD_ISR(2) MCPD_ISR(3)
MCPD_ISR(4) MCPD_ISR(5) MCPD_ISR(6) MCPD_ISR(7)

static void (*_intISRs[8])() = {
    _intISR0, _intISR1, _intISR2, _intISR3,
    _intISR4, _intISR5, _intISR6, _intISR7
};

// ── Find slot by pin ───────────────────────────────────────────────────

static int _intFindSlot(uint8_t pin) {
    for (uint8_t i = 0; i < _intCount; i++) {
        if (_intStates[i].pin == pin && _intStates[i].active) return i;
    }
    return -1;
}

// ── Parse interrupt mode ───────────────────────────────────────────────

static int _intParseMode(const char* modeStr) {
    if (!modeStr) return RISING;
    if (strcmp(modeStr, "falling") == 0) return FALLING;
    if (strcmp(modeStr, "change") == 0) return CHANGE;
    return RISING;
}

static const char* _intModeStr(int mode) {
    switch (mode) {
        case FALLING: return "falling";
        case CHANGE:  return "change";
        default:      return "rising";
    }
}

// ── Tool registration ──────────────────────────────────────────────────

inline void addInterruptTools(Server& server) {
    // interrupt_attach — configure a pin for interrupt monitoring
    server.addTool(
        MCPTool("interrupt_attach", "Attach interrupt to a GPIO pin for edge detection",
            R"({"type":"object","properties":{
                "pin":{"type":"integer","description":"GPIO pin number"},
                "mode":{"type":"string","enum":["rising","falling","change"],"default":"rising"},
                "pullup":{"type":"boolean","default":true,"description":"Enable internal pull-up"}
            },"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"] | -1;
                if (pin < 0) return R"({"error":"Pin number required"})";

                // Check if already attached
                int slot = _intFindSlot((uint8_t)pin);
                if (slot >= 0) {
                    return String(R"({"error":"Pin )") + pin + R"( already has interrupt attached"})";
                }

                if (_intCount >= 8) {
                    return R"({"error":"Maximum 8 interrupt pins supported"})";
                }

                const char* modeStr = args["mode"] | "rising";
                int mode = _intParseMode(modeStr);
                bool pullup = args["pullup"] | true;

                uint8_t idx = _intCount++;
                _intStates[idx].pin = (uint8_t)pin;
                _intStates[idx].mode = mode;
                _intStates[idx].count = 0;
                _intStates[idx].lastTriggerUs = 0;
                _intStates[idx].active = true;
                _intStates[idx].attachedAt = millis();

                pinMode(pin, pullup ? INPUT_PULLUP : INPUT);
                attachInterrupt(digitalPinToInterrupt(pin), _intISRs[idx], mode);

                return String(R"({"pin":)") + pin +
                       R"(,"mode":")" + _intModeStr(mode) +
                       R"(","pullup":)" + (pullup ? "true" : "false") +
                       R"(,"slot":)" + idx +
                       R"(,"attached":true})";
            }
        ).annotate(MCPToolAnnotations().setReadOnlyHint(false).setIdempotentHint(true))
    );

    // interrupt_read — read count and timing
    server.addTool(
        MCPTool("interrupt_read", "Read interrupt count and timing for a pin",
            R"({"type":"object","properties":{
                "pin":{"type":"integer","description":"GPIO pin number"},
                "reset":{"type":"boolean","default":false,"description":"Reset counter after reading"}
            },"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"] | -1;
                if (pin < 0) return R"({"error":"Pin number required"})";

                int slot = _intFindSlot((uint8_t)pin);
                if (slot < 0) {
                    return String(R"({"error":"No interrupt attached to pin )") + pin + R"("})";
                }

                noInterrupts();
                uint32_t count = _intStates[slot].count;
                unsigned long lastUs = _intStates[slot].lastTriggerUs;
                if (args["reset"] | false) {
                    _intStates[slot].count = 0;
                }
                interrupts();

                unsigned long now = millis();
                unsigned long uptime = now - _intStates[slot].attachedAt;
                float rate = (uptime > 0) ? (float)count / ((float)uptime / 1000.0f) : 0.0f;

                return String(R"({"pin":)") + pin +
                       R"(,"count":)" + count +
                       R"(,"rate_hz":)" + String(rate, 2) +
                       R"(,"last_trigger_us":)" + lastUs +
                       R"(,"uptime_ms":)" + uptime +
                       R"(,"mode":")" + _intModeStr(_intStates[slot].mode) + R"("})";
            }
        ).annotate(MCPToolAnnotations().setReadOnlyHint(true))
    );

    // interrupt_detach — stop monitoring
    server.addTool(
        MCPTool("interrupt_detach", "Detach interrupt from a GPIO pin",
            R"({"type":"object","properties":{
                "pin":{"type":"integer","description":"GPIO pin number"}
            },"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"] | -1;
                if (pin < 0) return R"({"error":"Pin number required"})";

                int slot = _intFindSlot((uint8_t)pin);
                if (slot < 0) {
                    return String(R"({"error":"No interrupt attached to pin )") + pin + R"("})";
                }

                detachInterrupt(digitalPinToInterrupt(pin));
                _intStates[slot].active = false;

                return String(R"({"pin":)") + pin +
                       R"(,"detached":true,"final_count":)" +
                       _intStates[slot].count + "}";
            }
        ).annotate(MCPToolAnnotations().setReadOnlyHint(false))
    );

    // interrupt_list — list all monitored pins
    server.addTool(
        MCPTool("interrupt_list", "List all pins with active interrupt monitoring",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                String result = R"({"pins":[)";
                bool first = true;
                for (uint8_t i = 0; i < _intCount; i++) {
                    if (!_intStates[i].active) continue;
                    if (!first) result += ",";
                    first = false;
                    result += String(R"({"pin":)") + _intStates[i].pin +
                              R"(,"count":)" + _intStates[i].count +
                              R"(,"mode":")" + _intModeStr(_intStates[i].mode) +
                              R"("})";
                }
                result += "]}";
                return result;
            }
        ).annotate(MCPToolAnnotations().setReadOnlyHint(true))
    );
}

} // namespace mcpd

#endif // MCP_INTERRUPT_TOOL_H
