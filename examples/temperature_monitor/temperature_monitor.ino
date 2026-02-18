/**
 * mcpd Example — Temperature Monitor
 *
 * Multi-sensor DS18B20 temperature monitoring station with heap diagnostics.
 * Demonstrates: OneWire tools, heap monitoring, session management,
 * resource subscriptions for live data, and prompts for configuration.
 *
 * Hardware:
 *   - ESP32 board
 *   - 1+ DS18B20 sensors on GPIO 4 (with 4.7kΩ pull-up)
 *
 * Dependencies (platformio.ini):
 *   lib_deps =
 *     redbasecap-buiss/mcpd
 *     paulstoffregen/OneWire
 *     milesburton/DallasTemperature
 */

#include <WiFi.h>
#include <mcpd.h>
#include <tools/MCPOneWireTool.h>

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

mcpd::Server mcp("temp-monitor");

// Temperature history (ring buffer)
struct TempReading {
    float tempC;
    unsigned long timestamp;
};
static TempReading history[60];  // Last 60 readings
static int historyIdx = 0;
static unsigned long lastSample = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Temperature Monitor] Starting...");

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // Configure session management
    mcp.setMaxSessions(2);               // Allow 2 concurrent AI clients
    mcp.setSessionTimeout(15 * 60000);   // 15 min idle timeout
    mcp.setRateLimit(5.0, 10);           // 5 req/s sustained, 10 burst

    // Register OneWire temperature tools (GPIO 4)
    mcpd::addOneWireTools(mcp, 4);

    // Register heap monitoring tools
    mcpd::tools::HeapTool::registerAll(mcp);

    // Add a temperature history resource
    mcp.addResource("temp://history", "Temperature History",
                    "Last 60 temperature readings (1 per minute)",
                    "application/json",
                    []() -> String {
        JsonDocument doc;
        JsonArray arr = doc["readings"].to<JsonArray>();
        for (int i = 0; i < 60; i++) {
            int idx = (historyIdx + i) % 60;
            if (history[idx].timestamp == 0) continue;
            JsonObject r = arr.add<JsonObject>();
            r["tempC"] = serialized(String(history[idx].tempC, 2));
            r["minutesAgo"] = (millis() - history[idx].timestamp) / 60000;
        }
        String result;
        serializeJson(doc, result);
        return result;
    });

    // Add a diagnostic prompt
    mcp.addPrompt("diagnose", "Analyze temperature trends and device health",
        {},  // no arguments
        [](const std::map<String, String>&) -> std::vector<mcpd::MCPPromptMessage> {
            String msg = "You are monitoring a DS18B20 temperature sensor station. "
                         "Please: 1) Read all current temperatures using onewire_read_all, "
                         "2) Check the temperature history resource at temp://history, "
                         "3) Check device health with heap_status, "
                         "4) Provide analysis of trends, anomalies, and recommendations.";
            return { mcpd::MCPPromptMessage::user(msg) };
        });

    // Lifecycle hooks
    mcp.onInitialize([](const String& client) {
        Serial.printf("[Monitor] Client connected: %s\n", client.c_str());
    });

    mcp.begin();
    Serial.println("[Monitor] MCP server ready — tools: onewire_scan, onewire_read_temp, "
                   "onewire_read_all, onewire_set_resolution, heap_status, heap_history");
}

void loop() {
    mcp.loop();

    // Sample temperature every 60 seconds for history
    if (millis() - lastSample > 60000 || lastSample == 0) {
        lastSample = millis();
        // Note: This is a simplified sample — in production you'd
        // use the DallasTemperature library directly here
        mcp.heap().sample();  // Also sample heap periodically
    }
}
