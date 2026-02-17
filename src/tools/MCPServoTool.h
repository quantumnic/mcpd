/**
 * mcpd — Built-in Servo Tool
 *
 * Provides: servo_write, servo_detach
 * Uses ESP32 LEDC to generate servo PWM signals.
 */

#ifndef MCPD_SERVO_TOOL_H
#define MCPD_SERVO_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class ServoTool {
public:
    static void attach(Server& server, int baseChannel = 8) {
        // servo_write — set servo angle (0-180 degrees)
        server.addTool("servo_write",
            "Set a servo to a specific angle (0-180 degrees)",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin connected to servo signal"},"angle":{"type":"integer","description":"Angle in degrees (0-180)"},"minUs":{"type":"integer","description":"Minimum pulse width in microseconds (default: 544)","default":544},"maxUs":{"type":"integer","description":"Maximum pulse width in microseconds (default: 2400)","default":2400}},"required":["pin","angle"]})",
            [baseChannel](const JsonObject& args) -> String {
                int pin = args["pin"];
                int angle = args["angle"];
                int minUs = args["minUs"] | 544;
                int maxUs = args["maxUs"] | 2400;

                // Clamp angle
                if (angle < 0) angle = 0;
                if (angle > 180) angle = 180;

                // Map angle to pulse width
                int pulseUs = map(angle, 0, 180, minUs, maxUs);

                // Use 50Hz PWM with 16-bit resolution for servo
                // 50Hz = 20ms period, 16-bit = 65536 ticks
                // pulseUs / 20000 * 65536 = duty
                int duty = (int)((float)pulseUs / 20000.0f * 65536.0f);

                ledcSetup(baseChannel, 50, 16);
                ledcAttachPin(pin, baseChannel);
                ledcWrite(baseChannel, duty);

                JsonDocument doc;
                doc["pin"] = pin;
                doc["angle"] = angle;
                doc["pulseUs"] = pulseUs;
                String r; serializeJson(doc, r); return r;
            });

        // servo_detach
        server.addTool("servo_detach",
            "Detach servo from a pin (stop PWM signal)",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin to detach"}},"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                ledcDetachPin(pin);
                return String("{\"pin\":") + pin + ",\"detached\":true}";
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_SERVO_TOOL_H
