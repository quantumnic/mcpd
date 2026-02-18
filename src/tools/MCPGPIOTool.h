/**
 * mcpd — Built-in GPIO Tools
 *
 * Provides: digital_read, digital_write, analog_read, pin_mode
 */

#ifndef MCPD_GPIO_TOOL_H
#define MCPD_GPIO_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class GPIOTool {
public:
    static void attach(Server& server) {
        // pin_mode
        {
            MCPTool tool("pin_mode", "Set the mode of a GPIO pin",
                R"j({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin number"},"mode":{"type":"string","enum":["INPUT","OUTPUT","INPUT_PULLUP","INPUT_PULLDOWN"],"description":"Pin mode"}},"required":["pin","mode"]})j",
                [](const JsonObject& args) -> String {
                    int pin = args["pin"];
                    String mode = args["mode"].as<const char*>();
                    if (mode == "INPUT") pinMode(pin, INPUT);
                    else if (mode == "OUTPUT") pinMode(pin, OUTPUT);
                    else if (mode == "INPUT_PULLUP") pinMode(pin, INPUT_PULLUP);
                    #ifdef INPUT_PULLDOWN
                    else if (mode == "INPUT_PULLDOWN") pinMode(pin, INPUT_PULLDOWN);
                    #endif
                    else return "{\"error\":\"Unknown mode\"}";
                    return String("{\"pin\":") + pin + ",\"mode\":\"" + mode + "\"}";
                });
            tool.markIdempotent().markLocalOnly();
            server.addTool(tool);
        }

        // digital_read
        {
            MCPTool tool("digital_read", "Read the digital value of a GPIO pin",
                R"j({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin number"}},"required":["pin"]})j",
                [](const JsonObject& args) -> String {
                    int pin = args["pin"];
                    int value = digitalRead(pin);
                    return String("{\"pin\":") + pin + ",\"value\":" + value + "}";
                });
            tool.markReadOnly().markLocalOnly();
            server.addTool(tool);
        }

        // digital_write
        {
            MCPTool tool("digital_write", "Write a digital value to a GPIO pin",
                R"j({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin number"},"value":{"type":"integer","enum":[0,1],"description":"HIGH (1) or LOW (0)"}},"required":["pin","value"]})j",
                [](const JsonObject& args) -> String {
                    int pin = args["pin"];
                    int value = args["value"];
                    digitalWrite(pin, value);
                    return String("{\"pin\":") + pin + ",\"value\":" + value + "}";
                });
            tool.markIdempotent().markLocalOnly();
            server.addTool(tool);
        }

        // analog_read
        {
            MCPTool tool("analog_read", "Read the analog value of a pin (0-4095 on ESP32)",
                R"j({"type":"object","properties":{"pin":{"type":"integer","description":"Analog-capable GPIO pin number"}},"required":["pin"]})j",
                [](const JsonObject& args) -> String {
                    int pin = args["pin"];
                    int value = analogRead(pin);
                    return String("{\"pin\":") + pin + ",\"value\":" + value + "}";
                });
            tool.markReadOnly().markLocalOnly();
            server.addTool(tool);
        }
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_GPIO_TOOL_H
