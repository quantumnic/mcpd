/**
 * mcpd — Built-in ADC (Analog-to-Digital Converter) Tool
 *
 * Provides: adc_read, adc_config, adc_read_voltage, adc_read_multi
 *
 * Enhanced analog reading with configurable resolution, attenuation (ESP32),
 * averaging for noise reduction, and voltage conversion.
 */

#ifndef MCPD_ADC_TOOL_H
#define MCPD_ADC_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class ADCTool {
public:
    static void attach(Server& server) {
        // adc_read — single analog reading with optional averaging
        server.addTool("adc_read", "Read analog value from a pin with optional sample averaging",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"Analog-capable GPIO pin number"},"samples":{"type":"integer","description":"Number of samples to average (1-64, default 1)","minimum":1,"maximum":64}},"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                int samples = args["samples"] | 1;
                if (samples < 1) samples = 1;
                if (samples > 64) samples = 64;

                long sum = 0;
                int minVal = 65535, maxVal = 0;
                for (int i = 0; i < samples; i++) {
                    int v = analogRead(pin);
                    sum += v;
                    if (v < minVal) minVal = v;
                    if (v > maxVal) maxVal = v;
                }
                int avg = (int)(sum / samples);

                String result = "{\"pin\":" + String(pin) +
                                ",\"value\":" + String(avg) +
                                ",\"samples\":" + String(samples);
                if (samples > 1) {
                    result += ",\"min\":" + String(minVal) +
                              ",\"max\":" + String(maxVal);
                }
                result += "}";
                return result;
            });

        // adc_read_voltage — read and convert to voltage
        server.addTool("adc_read_voltage", "Read analog pin and convert to voltage",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"Analog-capable GPIO pin number"},"vref":{"type":"number","description":"Reference voltage in volts (default 3.3)"},"resolution":{"type":"integer","description":"ADC resolution in bits (default 12 = 4096 steps)","enum":[8,10,12]},"samples":{"type":"integer","description":"Number of samples to average (1-64, default 4)","minimum":1,"maximum":64}},"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                float vref = args["vref"] | 3.3f;
                int resolution = args["resolution"] | 12;
                int samples = args["samples"] | 4;
                if (samples < 1) samples = 1;
                if (samples > 64) samples = 64;

                int maxAdc = (1 << resolution) - 1;

                long sum = 0;
                for (int i = 0; i < samples; i++) {
                    sum += analogRead(pin);
                }
                int avg = (int)(sum / samples);
                float voltage = (float)avg / maxAdc * vref;

                // Format voltage to 3 decimal places
                char vBuf[16];
                snprintf(vBuf, sizeof(vBuf), "%.3f", voltage);

                return String("{\"pin\":") + pin +
                       ",\"raw\":" + avg +
                       ",\"voltage\":" + vBuf +
                       ",\"vref\":" + String(vref, 1) +
                       ",\"resolution\":" + resolution + "}";
            });

        // adc_read_multi — read multiple analog pins at once
        server.addTool("adc_read_multi", "Read multiple analog pins in one call",
            R"({"type":"object","properties":{"pins":{"type":"array","items":{"type":"integer"},"description":"Array of analog-capable GPIO pin numbers (max 8)","maxItems":8},"samples":{"type":"integer","description":"Samples per pin for averaging (1-16, default 1)","minimum":1,"maximum":16}},"required":["pins"]})",
            [](const JsonObject& args) -> String {
                JsonArray pins = args["pins"].as<JsonArray>();
                int samples = args["samples"] | 1;
                if (samples < 1) samples = 1;
                if (samples > 16) samples = 16;

                if (pins.isNull() || pins.size() == 0) {
                    return "{\"error\":\"No pins specified\"}";
                }
                if (pins.size() > 8) {
                    return "{\"error\":\"Maximum 8 pins per call\"}";
                }

                String result = "{\"readings\":[";
                bool first = true;
                for (JsonVariant p : pins) {
                    if (!first) result += ",";
                    first = false;

                    int pin = p.as<int>();
                    long sum = 0;
                    for (int i = 0; i < samples; i++) {
                        sum += analogRead(pin);
                    }
                    int avg = (int)(sum / samples);
                    result += "{\"pin\":" + String(pin) + ",\"value\":" + String(avg) + "}";
                }
                result += "]}";
                return result;
            });

#ifdef ESP32
        // adc_config — ESP32-specific ADC configuration
        server.addTool("adc_config", "Configure ADC attenuation and resolution (ESP32)",
            R"({"type":"object","properties":{"pin":{"type":"integer","description":"GPIO pin to configure"},"attenuation":{"type":"string","enum":["0dB","2.5dB","6dB","11dB"],"description":"ADC attenuation: 0dB (0-1.1V), 2.5dB (0-1.5V), 6dB (0-2.2V), 11dB (0-3.3V)"},"resolution":{"type":"integer","enum":[9,10,11,12],"description":"ADC resolution in bits (9-12)"}},"required":["pin"]})",
            [](const JsonObject& args) -> String {
                int pin = args["pin"];
                String result = "{\"pin\":" + String(pin);

                if (args["attenuation"].is<const char*>()) {
                    String atten = args["attenuation"].as<const char*>();
                    if (atten == "0dB")        analogSetPinAttenuation(pin, ADC_0db);
                    else if (atten == "2.5dB")  analogSetPinAttenuation(pin, ADC_2_5db);
                    else if (atten == "6dB")    analogSetPinAttenuation(pin, ADC_6db);
                    else if (atten == "11dB")   analogSetPinAttenuation(pin, ADC_11db);
                    else return "{\"error\":\"Invalid attenuation\"}";
                    result += ",\"attenuation\":\"" + atten + "\"";
                }

                if (args["resolution"].is<int>()) {
                    int res = args["resolution"];
                    analogSetWidth(res);
                    result += ",\"resolution\":" + String(res);
                }

                result += ",\"configured\":true}";
                return result;
            });
#endif
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_ADC_TOOL_H
