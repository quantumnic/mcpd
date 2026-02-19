/**
 * mcpd — I2C Bus Scanner Tool
 *
 * Scans the I2C bus for connected devices and identifies common sensors/ICs
 * by their addresses. Extremely useful for debugging hardware setups.
 */

#ifndef MCPD_I2C_SCANNER_TOOL_H
#define MCPD_I2C_SCANNER_TOOL_H

#include "../MCPTool.h"
#include <Wire.h>

namespace mcpd {

/**
 * Known I2C device addresses and their common identifiers.
 */
inline const char* identifyI2CDevice(uint8_t addr) {
    switch (addr) {
        case 0x20: case 0x21: case 0x22: case 0x23:
        case 0x24: case 0x25: case 0x26: case 0x27:
            return "PCF8574/MCP23017 (GPIO expander)";
        case 0x38: return "AHT10/AHT20 (temp/humidity) or PCF8574A";
        case 0x39: return "TSL2561 (light) or PCF8574A";
        case 0x3C: return "SSD1306 (OLED display)";
        case 0x3D: return "SSD1306 (OLED display, alt addr)";
        case 0x40: return "INA219 (current/power) or HDC1080 (temp/humidity) or PCA9685 (PWM)";
        case 0x44: return "SHT30/SHT31 (temp/humidity)";
        case 0x45: return "SHT30/SHT31 (alt addr)";
        case 0x48: return "ADS1115 (ADC) or TMP102 (temp) or PCF8591";
        case 0x49: return "ADS1115 (ADC, alt) or TMP102 (alt)";
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57:
            return "AT24C32/AT24C256 (EEPROM)";
        case 0x5A: return "MLX90614 (IR thermometer) or CCS811 (air quality)";
        case 0x5B: return "CCS811 (air quality, alt addr)";
        case 0x5C: return "AM2320 (temp/humidity) or BH1750 (light)";
        case 0x60: return "MCP4725 (DAC) or SI1145 (UV/light)";
        case 0x68: return "DS3231/DS1307 (RTC) or MPU6050/MPU9250 (IMU)";
        case 0x69: return "MPU6050/MPU9250 (IMU, alt addr)";
        case 0x76: return "BME280/BMP280 (pressure/temp) or MS5611";
        case 0x77: return "BME280/BMP280 (alt addr) or BMP180";
        default: return nullptr;
    }
}

/**
 * Register I2C scanner tools on the server.
 *
 * Tools registered:
 *   - i2c_scan  — Scan I2C bus for connected devices with identification
 *   - i2c_probe — Check if a specific I2C address responds
 */
inline void registerI2CScannerTools(Server& server) {

    // ── i2c_scan ───────────────────────────────────────────────────
    {
        MCPTool tool(
            "i2c_scan",
            "Scan the I2C bus for all connected devices. Returns addresses (hex) "
            "and identifies common sensors/ICs. Useful for debugging hardware setup.",
            R"=({
                "type":"object",
                "properties":{
                    "bus":{"type":"integer","enum":[0,1],"description":"I2C bus number (0 or 1, default 0)"},
                    "sda":{"type":"integer","description":"SDA pin (default: board default)"},
                    "scl":{"type":"integer","description":"SCL pin (default: board default)"},
                    "speed":{"type":"integer","description":"Bus speed in Hz (default 100000)","minimum":10000,"maximum":1000000}
                }
            })=",
            [](const JsonObject& args) -> String {
                int bus = args.containsKey("bus") ? args["bus"].as<int>() : 0;
                TwoWire& wire = (bus == 1) ? Wire1 : Wire;

                // Configure pins if specified
                if (args.containsKey("sda") && args.containsKey("scl")) {
                    int sda = args["sda"].as<int>();
                    int scl = args["scl"].as<int>();
                    wire.begin(sda, scl);
                } else {
                    wire.begin();
                }

                int speed = args.containsKey("speed") ? args["speed"].as<int>() : 100000;
                wire.setClock(speed);

                JsonDocument doc;
                doc["bus"] = bus;
                doc["speed_hz"] = speed;
                JsonArray devices = doc["devices"].to<JsonArray>();
                int found = 0;

                for (uint8_t addr = 1; addr < 127; addr++) {
                    wire.beginTransmission(addr);
                    uint8_t error = wire.endTransmission();

                    if (error == 0) {
                        found++;
                        JsonObject dev = devices.add<JsonObject>();
                        char hex[7];
                        snprintf(hex, sizeof(hex), "0x%02X", addr);
                        dev["address"] = hex;
                        dev["decimal"] = addr;

                        const char* name = identifyI2CDevice(addr);
                        if (name) {
                            dev["likely"] = name;
                        }
                    }
                }

                doc["found"] = found;
                if (found == 0) {
                    doc["note"] = "No devices found. Check wiring, pull-up resistors, and power.";
                }

                String output;
                serializeJson(doc, output);
                return output;
            }
        );
        tool.markReadOnly().markLocalOnly();
        server.addTool(tool);
    }

    // ── i2c_probe ──────────────────────────────────────────────────
    {
        MCPTool tool(
            "i2c_probe",
            "Probe a specific I2C address to check if a device responds.",
            R"=({
                "type":"object",
                "properties":{
                    "address":{"type":"integer","description":"I2C address (7-bit, 1-126)","minimum":1,"maximum":126},
                    "bus":{"type":"integer","enum":[0,1],"description":"I2C bus number (default 0)"}
                },
                "required":["address"]
            })=",
            [](const JsonObject& args) -> String {
                uint8_t addr = args["address"].as<uint8_t>();
                int bus = args.containsKey("bus") ? args["bus"].as<int>() : 0;
                TwoWire& wire = (bus == 1) ? Wire1 : Wire;

                wire.beginTransmission(addr);
                uint8_t error = wire.endTransmission();

                JsonDocument doc;
                char hex[7];
                snprintf(hex, sizeof(hex), "0x%02X", addr);
                doc["address"] = hex;
                doc["present"] = (error == 0);
                doc["error_code"] = error;

                const char* errorMsg[] = {
                    "success", "data too long", "NACK on address",
                    "NACK on data", "other error", "timeout"
                };
                doc["status"] = (error <= 5) ? errorMsg[error] : "unknown";

                if (error == 0) {
                    const char* name = identifyI2CDevice(addr);
                    if (name) doc["likely"] = name;
                }

                String output;
                serializeJson(doc, output);
                return output;
            }
        );
        tool.markReadOnly().markLocalOnly();
        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_I2C_SCANNER_TOOL_H
