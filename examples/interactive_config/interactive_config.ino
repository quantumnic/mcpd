/**
 * mcpd Example: Interactive Configuration
 *
 * Demonstrates elicitation: the MCU asks the AI client to collect
 * user input via a structured form. Perfect for interactive setup
 * wizards, runtime configuration changes, and user-driven workflows.
 *
 * Features showcased:
 *   - Elicitation (server requests user input from client)
 *   - I2C scanner (discover connected hardware)
 *   - Audio content (return audio data from tools)
 *   - WebSocket transport (alongside HTTP)
 *
 * Hardware: ESP32 (any variant)
 * Dependencies: mcpd, ArduinoJson, Wire
 */

#include <mcpd.h>
#include <Wire.h>

// WiFi credentials
const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

// Create MCP server
mcpd::Server mcp("interactive-config");

// Runtime config (modifiable via elicitation)
struct Config {
    String deviceName = "sensor-01";
    float tempThreshold = 30.0;
    int readInterval = 5;  // seconds
    bool alertsEnabled = false;
    String tempUnit = "celsius";
} config;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // Enable WebSocket transport on port 8081
    mcp.enableWebSocket(8081);

    // ── Tool: Get current config ─────────────────────────────────

    mcp.addTool("config_get", "Get current device configuration",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String {
            JsonDocument doc;
            doc["device_name"] = config.deviceName;
            doc["temp_threshold"] = config.tempThreshold;
            doc["read_interval_s"] = config.readInterval;
            doc["alerts_enabled"] = config.alertsEnabled;
            doc["temp_unit"] = config.tempUnit;
            String out;
            serializeJson(doc, out);
            return out;
        });

    // ── Tool: Interactive config wizard (uses elicitation) ───────

    mcp.addTool("config_wizard", "Launch interactive configuration wizard — "
        "asks the user to fill in device settings via a form",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String {
            MCPElicitationRequest req;
            req.message = "Configure device settings. Current values are shown as defaults.";
            req.addTextField("device_name", "Device Name", true,
                             config.deviceName.c_str());
            req.addNumberField("temp_threshold", "Temperature Alert Threshold",
                               true, -40, 125);
            req.addIntegerField("read_interval", "Sensor Read Interval (seconds)",
                                true, 1, 3600);
            req.addBooleanField("alerts_enabled", "Enable Temperature Alerts",
                                false, config.alertsEnabled);
            req.addEnumField("temp_unit", "Temperature Unit",
                             {"celsius", "fahrenheit", "kelvin"}, true);

            mcp.requestElicitation(req, [](const MCPElicitationResponse& resp) {
                if (resp.accepted()) {
                    config.deviceName = resp.getString("device_name");
                    config.tempThreshold = resp.getFloat("temp_threshold", config.tempThreshold);
                    config.readInterval = resp.getInt("read_interval", config.readInterval);
                    config.alertsEnabled = resp.getBool("alerts_enabled", config.alertsEnabled);
                    String unit = resp.getString("temp_unit");
                    if (!unit.isEmpty()) config.tempUnit = unit;
                    Serial.println("[config] Updated via elicitation wizard");
                } else {
                    Serial.println("[config] User declined/cancelled config wizard");
                }
            });

            return R"({"status":"wizard_launched","note":"Form sent to user via elicitation"})";
        });

    // ── Tool: I2C scan ───────────────────────────────────────────
    // Register the built-in I2C scanner
    mcpd::registerI2CScannerTools(mcp);

    // ── Resource: Device status ──────────────────────────────────

    mcp.addResource("device://status", "Device Status",
        "Current device status and configuration", "application/json",
        []() -> String {
            JsonDocument doc;
            doc["name"] = config.deviceName;
            doc["uptime_s"] = millis() / 1000;
            doc["wifi_rssi"] = WiFi.RSSI();
            doc["ip"] = WiFi.localIP().toString();
            doc["config"]["threshold"] = config.tempThreshold;
            doc["config"]["interval"] = config.readInterval;
            doc["config"]["alerts"] = config.alertsEnabled;
            doc["config"]["unit"] = config.tempUnit;
            String out;
            serializeJson(doc, out);
            return out;
        });

    // ── Prompt: Setup guide ──────────────────────────────────────

    mcp.addPrompt("setup_guide", "Guide for setting up this device",
        {},
        [](const std::map<String, String>& args) -> std::vector<MCPPromptMessage> {
            return {
                MCPPromptMessage("user",
                    "I have an ESP32 running mcpd with the interactive config example. "
                    "Help me set it up:\n"
                    "1. First scan the I2C bus to see what's connected\n"
                    "2. Then run the config wizard to set thresholds\n"
                    "3. Finally check the device status resource")
            };
        });

    mcp.begin();
    Serial.println("Ready! HTTP on :80, WebSocket on :8081");
}

void loop() {
    mcp.loop();
}
