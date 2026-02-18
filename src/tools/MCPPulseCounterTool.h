/**
 * mcpd — Pulse Counter Tool
 *
 * Provides MCP tools for hardware pulse counting using ESP32 PCNT peripheral
 * or software interrupt counting. Ideal for flow meters, RPM sensors,
 * frequency measurement, and event counting.
 *
 * Tools:
 *   - pulse_read:    Read pulse count, frequency, and RPM
 *   - pulse_reset:   Reset counter to zero
 *   - pulse_config:  Configure counting mode, filters, and scaling
 *
 * MIT License
 */

#ifndef MCPD_PULSE_COUNTER_TOOL_H
#define MCPD_PULSE_COUNTER_TOOL_H

#include "../MCPTool.h"

namespace mcpd {

struct PulseCounterState {
    int pin = -1;
    volatile unsigned long count = 0;
    volatile unsigned long lastPulseUs = 0;
    volatile unsigned long prevPulseUs = 0;   // for frequency calc
    unsigned long lastResetMs = 0;
    float pulsesPerUnit = 1.0f;               // e.g. pulses per liter, per revolution
    const char* unitName = "count";           // e.g. "liters", "revolutions"
    int pulsesPerRevolution = 1;              // for RPM calculation
    bool countRising = true;
    bool countFalling = false;
    uint16_t filterUs = 0;                    // glitch filter (ignore pulses shorter than this)
};

static PulseCounterState _pulseCounters[4];
static int _pulseCounterCount = 0;

// ISR handlers
static void IRAM_ATTR _pulseISR0() {
    unsigned long now = micros();
    if (_pulseCounters[0].filterUs > 0 &&
        (now - _pulseCounters[0].lastPulseUs) < _pulseCounters[0].filterUs) return;
    _pulseCounters[0].prevPulseUs = _pulseCounters[0].lastPulseUs;
    _pulseCounters[0].lastPulseUs = now;
    _pulseCounters[0].count++;
}
static void IRAM_ATTR _pulseISR1() {
    unsigned long now = micros();
    if (_pulseCounters[1].filterUs > 0 &&
        (now - _pulseCounters[1].lastPulseUs) < _pulseCounters[1].filterUs) return;
    _pulseCounters[1].prevPulseUs = _pulseCounters[1].lastPulseUs;
    _pulseCounters[1].lastPulseUs = now;
    _pulseCounters[1].count++;
}
static void IRAM_ATTR _pulseISR2() {
    unsigned long now = micros();
    if (_pulseCounters[2].filterUs > 0 &&
        (now - _pulseCounters[2].lastPulseUs) < _pulseCounters[2].filterUs) return;
    _pulseCounters[2].prevPulseUs = _pulseCounters[2].lastPulseUs;
    _pulseCounters[2].lastPulseUs = now;
    _pulseCounters[2].count++;
}
static void IRAM_ATTR _pulseISR3() {
    unsigned long now = micros();
    if (_pulseCounters[3].filterUs > 0 &&
        (now - _pulseCounters[3].lastPulseUs) < _pulseCounters[3].filterUs) return;
    _pulseCounters[3].prevPulseUs = _pulseCounters[3].lastPulseUs;
    _pulseCounters[3].lastPulseUs = now;
    _pulseCounters[3].count++;
}

static void (*_pulseISRs[4])() = { _pulseISR0, _pulseISR1, _pulseISR2, _pulseISR3 };

/**
 * Register a pulse counter and add MCP tools.
 *
 * @param server          The MCP server
 * @param pin             GPIO input pin
 * @param pulsesPerUnit   Pulses per unit of measurement (e.g. 450 pulses/liter for a flow meter)
 * @param unitName        Unit label (e.g. "liters", "revolutions")
 * @param pulsesPerRev    Pulses per revolution for RPM calculation (default 1)
 * @return Counter index (0-3), or -1 if max reached
 */
inline int addPulseCounterTools(Server& server, int pin,
                                float pulsesPerUnit = 1.0f,
                                const char* unitName = "count",
                                int pulsesPerRev = 1) {
    if (_pulseCounterCount >= 4) return -1;

    int idx = _pulseCounterCount++;
    _pulseCounters[idx].pin = pin;
    _pulseCounters[idx].pulsesPerUnit = pulsesPerUnit;
    _pulseCounters[idx].unitName = unitName;
    _pulseCounters[idx].pulsesPerRevolution = pulsesPerRev;
    _pulseCounters[idx].lastResetMs = millis();

    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), _pulseISRs[idx], RISING);

    // Register tools once
    if (idx == 0) {

        // ── pulse_read ─────────────────────────────────────────────

        MCPTool readTool;
        readTool.name = "pulse_read";
        readTool.description = "Read pulse counter: count, frequency (Hz), RPM, and scaled unit value. Useful for flow meters, tachometers, and event counting.";
        readTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Counter index (0-3, default 0)",
                    "default": 0,
                    "minimum": 0,
                    "maximum": 3
                }
            }
        })=";
        readTool.annotations.title = "Read Pulse Counter";
        readTool.annotations.readOnlyHint = true;

        readTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _pulseCounterCount) {
                return R"({"error":"Invalid counter index"})";
            }

            PulseCounterState& pc = _pulseCounters[idx];
            unsigned long now = micros();
            unsigned long nowMs = millis();

            // Snapshot volatile values
            unsigned long count = pc.count;
            unsigned long lastPulse = pc.lastPulseUs;
            unsigned long prevPulse = pc.prevPulseUs;

            JsonDocument doc;
            doc["index"] = idx;
            doc["pin"] = pc.pin;
            doc["count"] = (long)count;

            // Frequency from last two pulses
            float freqHz = 0;
            if (prevPulse > 0 && lastPulse > prevPulse) {
                unsigned long periodUs = lastPulse - prevPulse;
                freqHz = 1000000.0f / periodUs;

                // If last pulse was >2s ago, frequency is effectively 0
                if ((now - lastPulse) > 2000000UL) freqHz = 0;
            }
            doc["frequency_hz"] = serialized(String(freqHz, 2));

            // RPM
            if (pc.pulsesPerRevolution > 0) {
                float rpm = freqHz * 60.0f / pc.pulsesPerRevolution;
                doc["rpm"] = serialized(String(rpm, 1));
            }

            // Scaled unit value
            if (pc.pulsesPerUnit > 0) {
                float unitVal = count / pc.pulsesPerUnit;
                doc["unit_value"] = serialized(String(unitVal, 3));
                doc["unit_name"] = pc.unitName;
            }

            // Time since last pulse
            if (lastPulse > 0) {
                doc["last_pulse_ms_ago"] = (now - lastPulse) / 1000;
            }

            // Elapsed since reset
            unsigned long elapsedMs = nowMs - pc.lastResetMs;
            doc["elapsed_ms"] = elapsedMs;

            // Average frequency over entire elapsed period
            if (elapsedMs > 0 && count > 0) {
                float avgHz = (float)count * 1000.0f / elapsedMs;
                doc["avg_frequency_hz"] = serialized(String(avgHz, 2));
            }

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(readTool);

        // ── pulse_reset ────────────────────────────────────────────

        MCPTool resetTool;
        resetTool.name = "pulse_reset";
        resetTool.description = "Reset pulse counter to zero.";
        resetTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Counter index (0-3, default 0)",
                    "default": 0
                }
            }
        })=";
        resetTool.annotations.title = "Reset Pulse Counter";
        resetTool.annotations.readOnlyHint = false;

        resetTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _pulseCounterCount) {
                return R"({"error":"Invalid counter index"})";
            }

            unsigned long prevCount = _pulseCounters[idx].count;
            _pulseCounters[idx].count = 0;
            _pulseCounters[idx].lastPulseUs = 0;
            _pulseCounters[idx].prevPulseUs = 0;
            _pulseCounters[idx].lastResetMs = millis();

            JsonDocument doc;
            doc["index"] = idx;
            doc["previous_count"] = (long)prevCount;
            doc["reset"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(resetTool);

        // ── pulse_config ───────────────────────────────────────────

        MCPTool configTool;
        configTool.name = "pulse_config";
        configTool.description = "Configure pulse counter: scaling factor, unit name, glitch filter, edge mode.";
        configTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Counter index (0-3, default 0)",
                    "default": 0
                },
                "pulses_per_unit": {
                    "type": "number",
                    "description": "Number of pulses per unit of measurement",
                    "minimum": 0.001
                },
                "unit_name": {
                    "type": "string",
                    "description": "Unit label (e.g. 'liters', 'meters', 'revolutions')"
                },
                "pulses_per_revolution": {
                    "type": "integer",
                    "description": "Pulses per revolution for RPM calculation",
                    "minimum": 1
                },
                "filter_us": {
                    "type": "integer",
                    "description": "Glitch filter: ignore pulses shorter than this (microseconds, 0=disabled)",
                    "minimum": 0,
                    "maximum": 100000
                },
                "edge": {
                    "type": "string",
                    "description": "Which edge(s) to count",
                    "enum": ["rising", "falling", "both"]
                }
            }
        })=";
        configTool.annotations.title = "Configure Pulse Counter";
        configTool.annotations.readOnlyHint = false;

        configTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _pulseCounterCount) {
                return R"({"error":"Invalid counter index"})";
            }

            PulseCounterState& pc = _pulseCounters[idx];

            if (params.containsKey("pulses_per_unit")) {
                pc.pulsesPerUnit = params["pulses_per_unit"].as<float>();
            }
            if (params.containsKey("unit_name")) {
                pc.unitName = params["unit_name"].as<const char*>();
            }
            if (params.containsKey("pulses_per_revolution")) {
                pc.pulsesPerRevolution = params["pulses_per_revolution"] | 1;
            }
            if (params.containsKey("filter_us")) {
                pc.filterUs = params["filter_us"] | 0;
            }
            if (params.containsKey("edge")) {
                const char* edge = params["edge"];
                if (edge) {
                    // Re-attach interrupt with new mode
                    detachInterrupt(digitalPinToInterrupt(pc.pin));
                    int mode = RISING;
                    if (strcmp(edge, "falling") == 0) mode = FALLING;
                    else if (strcmp(edge, "both") == 0) mode = CHANGE;
                    attachInterrupt(digitalPinToInterrupt(pc.pin), _pulseISRs[idx], mode);
                    pc.countRising = (mode == RISING || mode == CHANGE);
                    pc.countFalling = (mode == FALLING || mode == CHANGE);
                }
            }

            JsonDocument doc;
            doc["index"] = idx;
            doc["pulses_per_unit"] = serialized(String(pc.pulsesPerUnit, 3));
            doc["unit_name"] = pc.unitName;
            doc["pulses_per_revolution"] = pc.pulsesPerRevolution;
            doc["filter_us"] = pc.filterUs;
            doc["configured"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(configTool);
    }

    return idx;
}

} // namespace mcpd

#endif // MCPD_PULSE_COUNTER_TOOL_H
