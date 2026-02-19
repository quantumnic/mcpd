/**
 * mcpd — Stepper Motor Tool
 *
 * Provides MCP tools for controlling stepper motors with acceleration/deceleration.
 * Supports DIR/STEP interface (A4988, DRV8825, TMC2208, etc.) and configurable
 * acceleration profiles. Non-blocking movement via polling in loop().
 *
 * Tools:
 *   - stepper_move:    Move to absolute position or relative steps
 *   - stepper_status:  Read position, speed, running state
 *   - stepper_config:  Set max speed, acceleration, microstepping
 *   - stepper_home:    Run homing sequence toward endstop
 *   - stepper_stop:    Emergency stop or decelerated stop
 *
 * MIT License
 */

#ifndef MCPD_STEPPER_TOOL_H
#define MCPD_STEPPER_TOOL_H

#include "../MCPTool.h"

namespace mcpd {

struct StepperState {
    int stepPin = -1;
    int dirPin = -1;
    int enablePin = -1;
    int endstopPin = -1;         // optional homing endstop

    volatile long currentPos = 0;
    long targetPos = 0;
    float maxSpeed = 1000.0f;    // steps/sec
    float acceleration = 500.0f; // steps/sec²
    float currentSpeed = 0.0f;   // steps/sec (signed)
    int microsteps = 1;          // 1, 2, 4, 8, 16, 32
    bool inverted = false;
    bool enabled = true;
    bool homing = false;
    bool running = false;

    unsigned long lastStepUs = 0;
    unsigned long stepInterval = 0; // microseconds between steps
    float accelSteps = 0;          // steps since accel/decel started

    void computeNextStep() {
        if (currentPos == targetPos && !homing) {
            running = false;
            currentSpeed = 0;
            return;
        }

        running = true;
        long stepsToGo = targetPos - currentPos;
        int dir = (stepsToGo > 0) ? 1 : -1;
        if (homing) dir = inverted ? 1 : -1; // home toward zero

        // Simple trapezoidal acceleration
        long stepsRemaining = abs(stepsToGo);
        float decelSteps = (currentSpeed * currentSpeed) / (2.0f * acceleration);

        if (stepsRemaining <= (long)decelSteps && !homing) {
            // Decelerate
            currentSpeed -= acceleration * ((float)stepInterval / 1000000.0f);
            if (currentSpeed < 100.0f) currentSpeed = 100.0f; // min speed
        } else if (abs(currentSpeed) < maxSpeed) {
            // Accelerate
            currentSpeed += acceleration * ((float)stepInterval / 1000000.0f);
            if (currentSpeed > maxSpeed) currentSpeed = maxSpeed;
        }

        if (currentSpeed < 1.0f) currentSpeed = 100.0f;
        stepInterval = (unsigned long)(1000000.0f / currentSpeed);

        unsigned long now = micros();
        if (now - lastStepUs >= stepInterval) {
            // Check endstop during homing
            if (homing && endstopPin >= 0 && !digitalRead(endstopPin)) {
                homing = false;
                running = false;
                currentPos = 0;
                targetPos = 0;
                currentSpeed = 0;
                return;
            }

            // Execute step
            bool dirState = (dir > 0) != inverted;
            digitalWrite(dirPin, dirState ? HIGH : LOW);
            digitalWrite(stepPin, HIGH);
            delayMicroseconds(2);
            digitalWrite(stepPin, LOW);

            currentPos += dir;
            lastStepUs = now;
        }
    }
};

static StepperState _steppers[4];
static int _stepperCount = 0;

/**
 * Call from loop() to update stepper motion. Non-blocking.
 */
inline void stepperLoop() {
    for (int i = 0; i < _stepperCount; i++) {
        if (_steppers[i].running || _steppers[i].homing) {
            _steppers[i].computeNextStep();
        }
    }
}

/**
 * Register stepper motor tools.
 *
 * @param server     The MCP server
 * @param stepPin    STEP output pin
 * @param dirPin     DIR output pin
 * @param enablePin  ENABLE pin (-1 if not used)
 * @param endstopPin Endstop input pin for homing (-1 if not used)
 * @return Stepper index (0-3), or -1 if max reached
 */
inline int addStepperTools(Server& server, int stepPin, int dirPin,
                           int enablePin = -1, int endstopPin = -1) {
    if (_stepperCount >= 4) return -1;

    int idx = _stepperCount++;
    _steppers[idx].stepPin = stepPin;
    _steppers[idx].dirPin = dirPin;
    _steppers[idx].enablePin = enablePin;
    _steppers[idx].endstopPin = endstopPin;

    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    if (enablePin >= 0) {
        pinMode(enablePin, OUTPUT);
        digitalWrite(enablePin, LOW); // active low enable
    }
    if (endstopPin >= 0) {
        pinMode(endstopPin, INPUT_PULLUP);
    }

    // Register tools only once
    if (idx == 0) {

        // ── stepper_move ───────────────────────────────────────────

        MCPTool moveTool;
        moveTool.name = "stepper_move";
        moveTool.description = "Move stepper motor to an absolute position or by relative steps. Non-blocking — returns immediately, motor moves in background.";
        moveTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Stepper index (0-3, default 0)",
                    "default": 0,
                    "minimum": 0,
                    "maximum": 3
                },
                "position": {
                    "type": "integer",
                    "description": "Absolute target position in steps"
                },
                "relative": {
                    "type": "integer",
                    "description": "Relative steps to move (positive=forward, negative=backward)"
                }
            }
        })=";
        moveTool.annotations.title = "Move Stepper";
        moveTool.annotations.readOnlyHint = false;

        moveTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _stepperCount) {
                return R"({"error":"Invalid stepper index"})";
            }

            StepperState& st = _steppers[idx];

            if (params.containsKey("position")) {
                st.targetPos = params["position"].as<long>();
            } else if (params.containsKey("relative")) {
                st.targetPos = st.currentPos + params["relative"].as<long>();
            } else {
                return R"({"error":"Specify 'position' or 'relative'"})";
            }

            st.running = true;
            st.homing = false;
            if (st.currentSpeed < 1.0f) st.currentSpeed = 100.0f;
            st.lastStepUs = micros();

            JsonDocument doc;
            doc["index"] = idx;
            doc["current_position"] = (long)st.currentPos;
            doc["target_position"] = st.targetPos;
            doc["steps_to_go"] = abs(st.targetPos - st.currentPos);
            doc["moving"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(moveTool);

        // ── stepper_status ─────────────────────────────────────────

        MCPTool statusTool;
        statusTool.name = "stepper_status";
        statusTool.description = "Read stepper motor position, speed, and running state.";
        statusTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Stepper index (0-3, default 0)",
                    "default": 0
                }
            }
        })=";
        statusTool.annotations.title = "Stepper Status";
        statusTool.annotations.readOnlyHint = true;

        statusTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _stepperCount) {
                return R"({"error":"Invalid stepper index"})";
            }

            StepperState& st = _steppers[idx];
            JsonDocument doc;
            doc["index"] = idx;
            doc["current_position"] = (long)st.currentPos;
            doc["target_position"] = st.targetPos;
            doc["steps_to_go"] = abs(st.targetPos - st.currentPos);
            doc["running"] = st.running;
            doc["homing"] = st.homing;
            doc["current_speed"] = serialized(String(st.currentSpeed, 1));
            doc["max_speed"] = serialized(String(st.maxSpeed, 1));
            doc["acceleration"] = serialized(String(st.acceleration, 1));
            doc["microsteps"] = st.microsteps;
            doc["enabled"] = st.enabled;
            doc["step_pin"] = st.stepPin;
            doc["dir_pin"] = st.dirPin;

            if (st.endstopPin >= 0) {
                doc["endstop_triggered"] = !digitalRead(st.endstopPin);
            }

            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(statusTool);

        // ── stepper_config ─────────────────────────────────────────

        MCPTool configTool;
        configTool.name = "stepper_config";
        configTool.description = "Configure stepper motor parameters: max speed, acceleration, microstepping, direction inversion, enable/disable.";
        configTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Stepper index (0-3, default 0)",
                    "default": 0
                },
                "max_speed": {
                    "type": "number",
                    "description": "Maximum speed in steps/second (1-100000)",
                    "minimum": 1,
                    "maximum": 100000
                },
                "acceleration": {
                    "type": "number",
                    "description": "Acceleration in steps/second² (1-50000)",
                    "minimum": 1,
                    "maximum": 50000
                },
                "microsteps": {
                    "type": "integer",
                    "description": "Microstepping divisor (1, 2, 4, 8, 16, 32)",
                    "enum": [1, 2, 4, 8, 16, 32]
                },
                "inverted": {
                    "type": "boolean",
                    "description": "Invert direction"
                },
                "enabled": {
                    "type": "boolean",
                    "description": "Enable/disable motor driver"
                }
            }
        })=";
        configTool.annotations.title = "Configure Stepper";
        configTool.annotations.readOnlyHint = false;

        configTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _stepperCount) {
                return R"({"error":"Invalid stepper index"})";
            }

            StepperState& st = _steppers[idx];

            if (params.containsKey("max_speed")) {
                st.maxSpeed = params["max_speed"].as<float>();
                if (st.maxSpeed < 1) st.maxSpeed = 1;
                if (st.maxSpeed > 100000) st.maxSpeed = 100000;
            }
            if (params.containsKey("acceleration")) {
                st.acceleration = params["acceleration"].as<float>();
                if (st.acceleration < 1) st.acceleration = 1;
                if (st.acceleration > 50000) st.acceleration = 50000;
            }
            if (params.containsKey("microsteps")) {
                st.microsteps = params["microsteps"] | 1;
            }
            if (params.containsKey("inverted")) {
                st.inverted = params["inverted"] | false;
            }
            if (params.containsKey("enabled")) {
                st.enabled = params["enabled"] | true;
                if (st.enablePin >= 0) {
                    digitalWrite(st.enablePin, st.enabled ? LOW : HIGH);
                }
            }

            JsonDocument doc;
            doc["index"] = idx;
            doc["max_speed"] = serialized(String(st.maxSpeed, 1));
            doc["acceleration"] = serialized(String(st.acceleration, 1));
            doc["microsteps"] = st.microsteps;
            doc["inverted"] = st.inverted;
            doc["enabled"] = st.enabled;
            doc["configured"] = true;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(configTool);

        // ── stepper_home ───────────────────────────────────────────

        MCPTool homeTool;
        homeTool.name = "stepper_home";
        homeTool.description = "Run homing sequence: move toward endstop at reduced speed until triggered, then set position to zero.";
        homeTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Stepper index (0-3, default 0)",
                    "default": 0
                },
                "speed": {
                    "type": "number",
                    "description": "Homing speed in steps/sec (default: 25% of max speed)",
                    "minimum": 1
                }
            }
        })=";
        homeTool.annotations.title = "Home Stepper";
        homeTool.annotations.readOnlyHint = false;

        homeTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _stepperCount) {
                return R"({"error":"Invalid stepper index"})";
            }

            StepperState& st = _steppers[idx];
            if (st.endstopPin < 0) {
                return R"({"error":"No endstop configured for this stepper"})";
            }

            // Already at endstop?
            if (!digitalRead(st.endstopPin)) {
                st.currentPos = 0;
                st.targetPos = 0;
                return R"({"status":"already_home","position":0})";
            }

            float homeSpeed = params.containsKey("speed")
                ? params["speed"].as<float>()
                : st.maxSpeed * 0.25f;

            st.homing = true;
            st.running = true;
            st.maxSpeed = homeSpeed;
            st.currentSpeed = homeSpeed;
            st.targetPos = st.currentPos - 1000000L; // large negative target
            st.lastStepUs = micros();

            JsonDocument doc;
            doc["index"] = idx;
            doc["status"] = "homing";
            doc["homing_speed"] = serialized(String(homeSpeed, 1));
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(homeTool);

        // ── stepper_stop ───────────────────────────────────────────

        MCPTool stopTool;
        stopTool.name = "stepper_stop";
        stopTool.description = "Stop stepper motor immediately (emergency stop) or with deceleration.";
        stopTool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Stepper index (0-3, default 0)",
                    "default": 0
                },
                "emergency": {
                    "type": "boolean",
                    "description": "If true, stop instantly (may lose steps). If false, decelerate to stop.",
                    "default": false
                }
            }
        })=";
        stopTool.annotations.title = "Stop Stepper";
        stopTool.annotations.readOnlyHint = false;

        stopTool.handler = [](const JsonObject& params) -> String {
            int idx = params["index"] | 0;
            if (idx < 0 || idx >= _stepperCount) {
                return R"({"error":"Invalid stepper index"})";
            }

            StepperState& st = _steppers[idx];
            bool emergency = params["emergency"] | false;

            if (emergency) {
                st.running = false;
                st.homing = false;
                st.targetPos = st.currentPos;
                st.currentSpeed = 0;
            } else {
                // Decelerate: set target to current + decel distance
                float decelSteps = (st.currentSpeed * st.currentSpeed) / (2.0f * st.acceleration);
                int dir = (st.targetPos >= st.currentPos) ? 1 : -1;
                st.targetPos = st.currentPos + (long)(dir * decelSteps);
                st.homing = false;
            }

            JsonDocument doc;
            doc["index"] = idx;
            doc["stopped"] = emergency;
            doc["decelerating"] = !emergency && st.running;
            doc["position"] = (long)st.currentPos;
            String out;
            serializeJson(doc, out);
            return out;
        };

        server.addTool(stopTool);
    }

    return idx;
}

} // namespace mcpd

#endif // MCPD_STEPPER_TOOL_H
