/**
 * mcpd — Built-in DAC (Digital-to-Analog Converter) Tool
 *
 * Provides: dac_write, dac_write_voltage, dac_status
 *
 * ESP32 has two 8-bit DAC channels: DAC1 (GPIO 25) and DAC2 (GPIO 26).
 * Outputs true analog voltage (not PWM) — useful for waveform generation,
 * analog control signals, audio output, and reference voltages.
 */

#ifndef MCPD_DAC_TOOL_H
#define MCPD_DAC_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class DACTool {
public:
    struct DACState {
        uint8_t value = 0;
        bool enabled = false;
    };

    static DACState& state(int channel) {
        static DACState states[2];
        return states[channel < 0 || channel > 1 ? 0 : channel];
    }

    static int pinToChannel(int pin) {
        if (pin == 25) return 0;
        if (pin == 26) return 1;
        return -1;
    }

    static void attach(Server& server) {
        // dac_write — write raw 8-bit value to DAC
        server.addTool("dac_write", "Write a raw 8-bit value (0-255) to an ESP32 DAC pin (GPIO 25 or 26)",
             R"=({"type":"object","properties":{"pin":{"type":"integer","enum":[25,26],"description":"DAC-capable GPIO pin (25 = DAC1, 26 = DAC2)"},"value":{"type":"integer","minimum":0,"maximum":255,"description":"Output value (0 = 0V, 255 ~ 3.3V)"}},"required":["pin","value"]})=",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                int value = args["value"];
                int ch = pinToChannel(pin);
                if (ch < 0) return R"=({"error":"Invalid DAC pin. Use 25 (DAC1) or 26 (DAC2)"})=";
                if (value < 0 || value > 255) return R"({"error":"Value must be 0-255"})";

#ifdef ESP32
                dacWrite(pin, (uint8_t)value);
#endif
                state(ch).value = (uint8_t)value;
                state(ch).enabled = true;

                char vBuf[16];
                snprintf(vBuf, sizeof(vBuf), "%.3f", value / 255.0 * 3.3);

                return String("{\"pin\":") + pin +
                       ",\"channel\":" + (ch + 1) +
                       ",\"value\":" + value +
                       ",\"voltage\":" + vBuf + "}";
            });

        // dac_write_voltage — write a voltage value to DAC
        server.addTool("dac_write_voltage", "Write a voltage (0-3.3V) to an ESP32 DAC pin",
            R"=({"type":"object","properties":{"pin":{"type":"integer","enum":[25,26],"description":"DAC-capable GPIO pin (25 = DAC1, 26 = DAC2)"},"voltage":{"type":"number","minimum":0,"maximum":3.3,"description":"Desired output voltage (0-3.3V)"}},"required":["pin","voltage"]})=",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                float voltage = args["voltage"] | 0.0f;
                int ch = pinToChannel(pin);
                if (ch < 0) return R"=({"error":"Invalid DAC pin. Use 25 (DAC1) or 26 (DAC2)"})=";
                if (voltage < 0.0f || voltage > 3.3f) return R"=({"error":"Voltage must be 0-3.3V"})=";

                uint8_t value = (uint8_t)(voltage / 3.3f * 255.0f + 0.5f);
#ifdef ESP32
                dacWrite(pin, value);
#endif
                state(ch).value = value;
                state(ch).enabled = true;

                float actualV = value / 255.0f * 3.3f;
                char reqBuf[16], actBuf[16];
                snprintf(reqBuf, sizeof(reqBuf), "%.3f", voltage);
                snprintf(actBuf, sizeof(actBuf), "%.3f", actualV);

                return String("{\"pin\":") + pin +
                       ",\"channel\":" + (ch + 1) +
                       ",\"raw\":" + value +
                       ",\"requested_voltage\":" + reqBuf +
                       ",\"actual_voltage\":" + actBuf +
                       ",\"resolution_step_mv\":" +
                       String(3300.0f / 255.0f, 1) + "}";
            });

        // dac_status — read current DAC state
        server.addTool("dac_status", "Get current DAC output status for both channels",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                String result = "{\"channels\":[";
                for (int ch = 0; ch < 2; ch++) {
                    if (ch > 0) result += ",";
                    int pin = ch == 0 ? 25 : 26;
                    DACState& s = state(ch);
                    char vBuf[16];
                    snprintf(vBuf, sizeof(vBuf), "%.3f", s.value / 255.0 * 3.3);
                    result += String("{\"channel\":") + (ch + 1) +
                              ",\"pin\":" + pin +
                              ",\"enabled\":" + (s.enabled ? "true" : "false") +
                              ",\"value\":" + s.value +
                              ",\"voltage\":" + vBuf + "}";
                }
                result += "]}";
                return result;
            });
    }
};

} // namespace tools

/**
 * Register DAC tools with a single call.
 *
 * @param server  The mcpd::Server instance
 */
inline void addDACTools(Server& server) {
    tools::DACTool::attach(server);
}

} // namespace mcpd

#endif // MCPD_DAC_TOOL_H
