/**
 * mcpd — Built-in DHT Sensor Tool
 *
 * Provides: dht_read
 * Supports DHT11, DHT22/AM2302 sensors.
 *
 * NOTE: Requires the DHT sensor library.
 *       Add to lib_deps: adafruit/DHT sensor library@^1.4
 */

#ifndef MCPD_DHT_TOOL_H
#define MCPD_DHT_TOOL_H

#include "../mcpd.h"
#include <DHT.h>

namespace mcpd {
namespace tools {

class DHTTool {
public:
    /**
     * Attach DHT sensor tool to the server.
     * @param server  MCP server instance
     * @param dht     Reference to an initialized DHT sensor
     */
    static void attach(Server& server, DHT& dht) {
        // dht_read — read temperature and humidity
        server.addTool("dht_read",
            "Read temperature and humidity from a DHT sensor",
            R"({"type":"object","properties":{"fahrenheit":{"type":"boolean","description":"Return temperature in Fahrenheit (default: false)","default":false}}})",
            [&dht](const JsonObject& args) -> String {
                bool fahrenheit = args["fahrenheit"] | false;

                float humidity = dht.readHumidity();
                float temperature = dht.readTemperature(fahrenheit);
                float heatIndex = dht.computeHeatIndex(temperature, humidity, fahrenheit);

                JsonDocument doc;

                if (isnan(humidity) || isnan(temperature)) {
                    doc["error"] = "Failed to read from DHT sensor";
                    doc["hint"] = "Check wiring and pull-up resistor";
                } else {
                    doc["temperature"] = serialized(String(temperature, 1));
                    doc["humidity"] = serialized(String(humidity, 1));
                    doc["heatIndex"] = serialized(String(heatIndex, 1));
                    doc["unit"] = fahrenheit ? "°F" : "°C";
                }

                String result;
                serializeJson(doc, result);
                return result;
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_DHT_TOOL_H
