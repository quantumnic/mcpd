/**
 * mcpd — OneWire / DS18B20 Temperature Sensor Tool
 *
 * Provides MCP tools for reading DS18B20 (and compatible) temperature sensors
 * via the OneWire protocol. Supports multiple sensors on a single bus.
 *
 * Tools:
 *   - onewire_scan:       Scan bus for connected device addresses
 *   - onewire_read_temp:  Read temperature from a specific sensor (°C)
 *   - onewire_read_all:   Read all sensors on the bus
 *   - onewire_set_resolution: Set sensor resolution (9-12 bit)
 *
 * Requires: OneWire + DallasTemperature libraries (PlatformIO: paulstoffregen/OneWire, milesburton/DallasTemperature)
 *
 * MIT License
 */

#ifndef MCPD_ONEWIRE_TOOL_H
#define MCPD_ONEWIRE_TOOL_H

#include "../MCPTool.h"
#include <OneWire.h>
#include <DallasTemperature.h>

namespace mcpd {

/**
 * Register OneWire / DS18B20 temperature sensor tools.
 *
 * @param server   The MCP server to register tools on
 * @param pin      GPIO pin for the OneWire data bus
 */
inline void addOneWireTools(class Server& server, uint8_t pin) {
    // Static state for the OneWire bus — persists across calls
    static OneWire* ow = nullptr;
    static DallasTemperature* sensors = nullptr;

    if (!ow) {
        ow = new OneWire(pin);
        sensors = new DallasTemperature(ow);
        sensors->begin();
    }

    // ── onewire_scan ───────────────────────────────────────────────────
    {
        MCPTool tool;
        tool.name = "onewire_scan";
        tool.description = "Scan the OneWire bus for connected DS18B20 temperature sensors. Returns addresses and parasite power status.";
        tool.inputSchemaJson = R"({"type":"object","properties":{}})";
        tool.annotations.readOnlyHint = true;
        tool.annotations.title = "Scan OneWire Bus";

        tool.handler = [](const JsonObject&) -> String {
            sensors->begin();  // re-scan
            uint8_t count = sensors->getDeviceCount();

            JsonDocument doc;
            doc["deviceCount"] = count;
            doc["parasitePower"] = sensors->isParasitePowerMode();
            doc["pin"] = ow->pin();
            JsonArray devices = doc["devices"].to<JsonArray>();

            DeviceAddress addr;
            for (uint8_t i = 0; i < count; i++) {
                if (sensors->getAddress(addr, i)) {
                    JsonObject dev = devices.add<JsonObject>();
                    // Format address as hex string
                    char addrStr[24];
                    snprintf(addrStr, sizeof(addrStr),
                             "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                             addr[0], addr[1], addr[2], addr[3],
                             addr[4], addr[5], addr[6], addr[7]);
                    dev["address"] = addrStr;
                    dev["index"] = i;
                    dev["resolution"] = sensors->getResolution(addr);

                    // Identify family
                    const char* family = "Unknown";
                    switch (addr[0]) {
                        case 0x10: family = "DS18S20"; break;
                        case 0x22: family = "DS1822";  break;
                        case 0x28: family = "DS18B20"; break;
                        case 0x3B: family = "DS1825";  break;
                        case 0x42: family = "DS28EA00"; break;
                    }
                    dev["family"] = family;
                }
            }

            String result;
            serializeJson(doc, result);
            return result;
        };

        server.addTool(tool);
    }

    // ── onewire_read_temp ──────────────────────────────────────────────
    {
        MCPTool tool;
        tool.name = "onewire_read_temp";
        tool.description = "Read temperature from a DS18B20 sensor by index or address. Returns temperature in °C and °F.";
        tool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Sensor index (0-based, from onewire_scan)",
                    "minimum": 0
                },
                "address": {
                    "type": "string",
                    "description": "Sensor address as hex (e.g. '28:FF:A0:...'). Overrides index if both given."
                }
            }
        })=";
        tool.annotations.readOnlyHint = true;
        tool.annotations.title = "Read Temperature";

        tool.handler = [](const JsonObject& params) -> String {
            sensors->requestTemperatures();

            float tempC = DEVICE_DISCONNECTED_C;

            if (params.containsKey("address")) {
                // Parse hex address
                const char* addrStr = params["address"].as<const char*>();
                DeviceAddress addr;
                if (addrStr && strlen(addrStr) >= 23) {
                    sscanf(addrStr, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
                           &addr[0], &addr[1], &addr[2], &addr[3],
                           &addr[4], &addr[5], &addr[6], &addr[7]);
                    tempC = sensors->getTempC(addr);
                } else {
                    return R"({"error":"Invalid address format. Use XX:XX:XX:XX:XX:XX:XX:XX"})";
                }
            } else {
                int index = params["index"] | 0;
                tempC = sensors->getTempCByIndex(index);
            }

            if (tempC == DEVICE_DISCONNECTED_C) {
                return R"({"error":"Sensor disconnected or not found"})";
            }

            JsonDocument doc;
            doc["temperatureC"] = serialized(String(tempC, 2));
            doc["temperatureF"] = serialized(String(DallasTemperature::toFahrenheit(tempC), 2));

            String result;
            serializeJson(doc, result);
            return result;
        };

        server.addTool(tool);
    }

    // ── onewire_read_all ───────────────────────────────────────────────
    {
        MCPTool tool;
        tool.name = "onewire_read_all";
        tool.description = "Read temperature from all DS18B20 sensors on the bus. Returns array of readings.";
        tool.inputSchemaJson = R"({"type":"object","properties":{}})";
        tool.annotations.readOnlyHint = true;
        tool.annotations.title = "Read All Temperatures";

        tool.handler = [](const JsonObject&) -> String {
            sensors->requestTemperatures();

            uint8_t count = sensors->getDeviceCount();
            JsonDocument doc;
            doc["deviceCount"] = count;
            JsonArray readings = doc["readings"].to<JsonArray>();

            DeviceAddress addr;
            for (uint8_t i = 0; i < count; i++) {
                if (sensors->getAddress(addr, i)) {
                    float tempC = sensors->getTempC(addr);
                    if (tempC == DEVICE_DISCONNECTED_C) continue;

                    JsonObject r = readings.add<JsonObject>();
                    char addrStr[24];
                    snprintf(addrStr, sizeof(addrStr),
                             "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                             addr[0], addr[1], addr[2], addr[3],
                             addr[4], addr[5], addr[6], addr[7]);
                    r["address"] = addrStr;
                    r["index"] = i;
                    r["temperatureC"] = serialized(String(tempC, 2));
                    r["temperatureF"] = serialized(String(DallasTemperature::toFahrenheit(tempC), 2));
                }
            }

            String result;
            serializeJson(doc, result);
            return result;
        };

        server.addTool(tool);
    }

    // ── onewire_set_resolution ─────────────────────────────────────────
    {
        MCPTool tool;
        tool.name = "onewire_set_resolution";
        tool.description = "Set the measurement resolution of a DS18B20 sensor. Higher resolution = more accurate but slower. 9-bit: 0.5°C (94ms), 10-bit: 0.25°C (188ms), 11-bit: 0.125°C (375ms), 12-bit: 0.0625°C (750ms).";
        tool.inputSchemaJson = R"=({
            "type": "object",
            "properties": {
                "index": {
                    "type": "integer",
                    "description": "Sensor index (0-based)",
                    "minimum": 0
                },
                "resolution": {
                    "type": "integer",
                    "description": "Resolution in bits (9, 10, 11, or 12)",
                    "minimum": 9,
                    "maximum": 12
                }
            },
            "required": ["resolution"]
        })=";
        tool.annotations.title = "Set Sensor Resolution";

        tool.handler = [](const JsonObject& params) -> String {
            int resolution = params["resolution"] | 12;
            int index = params["index"] | 0;

            DeviceAddress addr;
            if (!sensors->getAddress(addr, index)) {
                return R"({"error":"Sensor not found at given index"})";
            }

            sensors->setResolution(addr, resolution);
            int actual = sensors->getResolution(addr);

            JsonDocument doc;
            doc["index"] = index;
            doc["requestedResolution"] = resolution;
            doc["actualResolution"] = actual;
            doc["success"] = (actual == resolution);

            String result;
            serializeJson(doc, result);
            return result;
        };

        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_ONEWIRE_TOOL_H
