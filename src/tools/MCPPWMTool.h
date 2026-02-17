/**
 * mcpd — Built-in PWM Tool
 *
 * Provides: pwm_write, pwm_configure
 * Uses the ESP32 LEDC peripheral for hardware PWM.
 */

#ifndef MCPD_PWM_TOOL_H
#define MCPD_PWM_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class PWMTool {
public:
    static void attach(Server& server) {
        // pwm_write — set duty cycle on a pin
        server.addTool("pwm_write",
            "Write a PWM duty cycle to a pin (0-255 by default, or 0-1023 with 10-bit resolution)",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin number"},"duty":{"type":"integer","description":"Duty cycle value (0-255 for 8-bit, 0-1023 for 10-bit)"},"frequency":{"type":"integer","description":"PWM frequency in Hz (default: 5000)","default":5000},"resolution":{"type":"integer","description":"Resolution in bits (8-16, default: 8)","default":8},"channel":{"type":"integer","description":"LEDC channel (0-15, default: auto)","default":0}},"required":["pin","duty"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                int duty = args["duty"];
                int freq = args["frequency"] | 5000;
                int resolution = args["resolution"] | 8;
                int channel = args["channel"] | 0;

                // Clamp resolution
                if (resolution < 1) resolution = 1;
                if (resolution > 16) resolution = 16;

                // Clamp duty to max for resolution
                int maxDuty = (1 << resolution) - 1;
                if (duty < 0) duty = 0;
                if (duty > maxDuty) duty = maxDuty;

                ledcSetup(channel, freq, resolution);
                ledcAttachPin(pin, channel);
                ledcWrite(channel, duty);

                JsonDocument doc;
                doc["pin"] = pin;
                doc["duty"] = duty;
                doc["maxDuty"] = maxDuty;
                doc["frequency"] = freq;
                doc["resolution"] = resolution;
                doc["channel"] = channel;
                String r; serializeJson(doc, r); return r;
            });

        // pwm_stop — detach PWM from a pin
        server.addTool("pwm_stop",
            "Stop PWM output on a pin",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin number"}},"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                ledcDetachPin(pin);
                return String("{\"pin\":") + pin + ",\"stopped\":true}";
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_PWM_TOOL_H
