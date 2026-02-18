/**
 * mcpd — Rotary Encoder Tool
 *
 * Provides MCP tools for reading rotary encoders (with optional push button).
 * Uses hardware interrupts for accurate counting. Supports multiple encoders.
 *
 * Tools:
 *   - encoder_read:   Read current position, direction, and speed
 *   - encoder_reset:  Reset encoder position to zero (or specified value)
 *   - encoder_config: Configure encoder parameters (steps per revolution, limits)
 *
 * MIT License
 */

#ifndef MCPD_ENCODER_TOOL_H
#define MCPD_ENCODER_TOOL_H

#include "../MCPTool.h"

namespace mcpd {

// Lightweight encoder state — supports up to 4 encoders
struct EncoderState {
    volatile long position = 0;
    volatile int lastA = 0;
    int pinA = -1;
    int pinB = -1;
    int pinButton = -1;
    long minPos = LONG_MIN;
    long maxPos = LONG_MAX;
    int stepsPerRev = 0;  // 0 = no revolution tracking
    unsigned long lastChangeMs = 0;
};

static EncoderState _encoders[4];
static int _encoderCount = 0;

// ISR templates for each encoder slot
static void IRAM_ATTR _encoderISR0() {
    int a = digitalRead(_encoders[0].pinA);
    int b = digitalRead(_encoders[0].pinB);
    if (a != _encoders[0].lastA) {
        _encoders[0].position += (a != b) ? 1 : -1;
        if (_encoders[0].position < _encoders[0].minPos) _encoders[0].position = _encoders[0].minPos;
        if (_encoders[0].position > _encoders[0].maxPos) _encoders[0].position = _encoders[0].maxPos;
        _encoders[0].lastA = a;
        _encoders[0].lastChangeMs = millis();
    }
}
static void IRAM_ATTR _encoderISR1() {
    int a = digitalRead(_encoders[1].pinA);
    int b = digitalRead(_encoders[1].pinB);
    if (a != _encoders[1].lastA) {
        _encoders[1].position += (a != b) ? 1 : -1;
        if (_encoders[1].position < _encoders[1].minPos) _encoders[1].position = _encoders[1].minPos;
        if (_encoders[1].position > _encoders[1].maxPos) _encoders[1].position = _encoders[1].maxPos;
        _encoders[1].lastA = a;
        _encoders[1].lastChangeMs = millis();
    }
}
static void IRAM_ATTR _encoderISR2() {
    int a = digitalRead(_encoders[2].pinA);
    int b = digitalRead(_encoders[2].pinB);
    if (a != _encoders[2].lastA) {
        _encoders[2].position += (a != b) ? 1 : -1;
        if (_encoders[2].position < _encoders[2].minPos) _encoders[2].position = _encoders[2].minPos;
        if (_encoders[2].position > _encoders[2].maxPos) _encoders[2].position = _encoders[2].maxPos;
        _encoders[2].lastA = a;
        _encoders[2].lastChangeMs = millis();
    }
}
static void IRAM_ATTR _encoderISR3() {
    int a = digitalRead(_encoders[3].pinA);
    int b = digitalRead(_encoders[3].pinB);
    if (a != _encoders[3].lastA) {
        _encoders[3].position += (a != b) ? 1 : -1;
        if (_encoders[3].position < _encoders[3].minPos) _encoders[3].position = _encoders[3].minPos;
        if (_encoders[3].position > _encoders[3].maxPos) _encoders[3].position = _encoders[3].maxPos;
        _encoders[3].lastA = a;
        _encoders[3].lastChangeMs = millis();
    }
}

static void (*_encoderISRs[4])() = { _encoderISR0, _encoderISR1, _encoderISR2, _encoderISR3 };

/**
 * Register a rotary encoder and add MCP tools for it.
 *
 * @param server     The MCP server
 * @param pinA       Encoder channel A pin
 * @param pinB       Encoder channel B pin
 * @param pinButton  Optional push-button pin (-1 to disable)
 * @return Encoder index (0-3), or -1 if max encoders reached
 */
inline int addEncoderTools(Server& server, int pinA, int pinB, int pinButton = -1) {
    if (_encoderCount >= 4) return -1;

    int idx = _encoderCount++;
    _encoders[idx].pinA = pinA;
    _encoders[idx].pinB = pinB;
    _encoders[idx].pinButton = pinButton;

    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    if (pinButton >= 0) pinMode(pinButton, INPUT_PULLUP);

    _encoders[idx].lastA = digitalRead(pinA);
    attachInterrupt(digitalPinToInterrupt(pinA), _encoderISRs[idx], CHANGE);

    // Only register tools once (for the first encoder)
    if (idx == 0) {
        // ── encoder_read ───────────────────────────────────────────

        MCPTool readTool;
        readTool.name = "encoder_read";
        readTool.description = "Read rotary encoder position, direction, speed, and button state.";
        readTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Encoder index (0-3, default 0)",
                    "default": 0,
                    "minimum": 0,
                    "maximum": 3
                }
            }
        })=";
        readTool.annotations.title = "Read Encoder";
        readTool.annotations.readOnlyHint = true;

        readTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _encoderCount) {
                return "{\"error\":\"Invalid encoder index\"}";
            }

            EncoderState& enc = _encoders[idx];
            long pos = enc.position;
            unsigned long now = millis();
            unsigned long elapsed = now - enc.lastChangeMs;

            JsonDocument doc;
            doc["index"] = idx;
            doc["position"] = pos;
            doc["pin_a"] = enc.pinA;
            doc["pin_b"] = enc.pinB;
            doc["idle_ms"] = elapsed;

            if (enc.stepsPerRev > 0) {
                float revolutions = (float)pos / enc.stepsPerRev;
                float degrees = fmod(revolutions * 360.0f, 360.0f);
                if (degrees < 0) degrees += 360.0f;
                doc["revolutions"] = serialized(String(revolutions, 2));
                doc["degrees"] = serialized(String(degrees, 1));
            }

            if (enc.pinButton >= 0) {
                doc["button_pressed"] = !digitalRead(enc.pinButton); // active low
            }

            if (enc.minPos != LONG_MIN) doc["min_limit"] = enc.minPos;
            if (enc.maxPos != LONG_MAX) doc["max_limit"] = enc.maxPos;

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(readTool);

        // ── encoder_reset ──────────────────────────────────────────

        MCPTool resetTool;
        resetTool.name = "encoder_reset";
        resetTool.description = "Reset encoder position to zero or a specified value.";
        resetTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Encoder index (0-3)",
                    "default": 0
                },
                "value": {
                    "type": "integer",
                    "description": "New position value (default 0)",
                    "default": 0
                }
            }
        })=";
        resetTool.annotations.title = "Reset Encoder";
        resetTool.annotations.readOnlyHint = false;

        resetTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            long val = params["value"] | 0L;
            if (idx < 0 || idx >= _encoderCount) {
                return "{\"error\":\"Invalid encoder index\"}";
            }
            _encoders[idx].position = val;
            _encoders[idx].lastChangeMs = millis();

            JsonDocument doc;
            doc["index"] = idx;
            doc["position"] = val;
            doc["reset"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(resetTool);

        // ── encoder_config ─────────────────────────────────────────

        MCPTool configTool;
        configTool.name = "encoder_config";
        configTool.description = "Configure encoder: steps per revolution, position limits.";
        configTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Encoder index (0-3)",
                    "default": 0
                },
                "steps_per_rev": {
                    "type": "integer",
                    "description": "Steps per full revolution (e.g. 20 for common encoders). 0 to disable revolution tracking.",
                    "minimum": 0
                },
                "min_position": {
                    "type": "integer",
                    "description": "Minimum position limit (omit for no limit)"
                },
                "max_position": {
                    "type": "integer",
                    "description": "Maximum position limit (omit for no limit)"
                }
            }
        })=";
        configTool.annotations.title = "Configure Encoder";
        configTool.annotations.readOnlyHint = false;

        configTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _encoderCount) {
                return "{\"error\":\"Invalid encoder index\"}";
            }

            EncoderState& enc = _encoders[idx];

            if (params.containsKey("steps_per_rev")) {
                enc.stepsPerRev = params["steps_per_rev"] | 0;
            }
            if (params.containsKey("min_position")) {
                enc.minPos = params["min_position"] | (long)LONG_MIN;
            }
            if (params.containsKey("max_position")) {
                enc.maxPos = params["max_position"] | (long)LONG_MAX;
            }

            JsonDocument doc;
            doc["index"] = idx;
            doc["steps_per_rev"] = enc.stepsPerRev;
            if (enc.minPos != LONG_MIN) doc["min_position"] = enc.minPos;
            if (enc.maxPos != LONG_MAX) doc["max_position"] = enc.maxPos;
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

#endif // MCPD_ENCODER_TOOL_H
