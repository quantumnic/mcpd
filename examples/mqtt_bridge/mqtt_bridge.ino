/**
 * mcpd Example: MQTT Bridge
 *
 * Exposes MQTT publish/subscribe as MCP tools so an AI assistant
 * can interact with your MQTT-based IoT ecosystem.
 *
 * Claude can:
 *   - Connect to your MQTT broker
 *   - Subscribe to sensor topics
 *   - Publish commands to actuators
 *   - Read buffered messages
 *
 * Hardware: ESP32 (WiFi)
 * Dependencies: mcpd, PubSubClient, ArduinoJson
 *
 * PlatformIO:
 *   lib_deps =
 *       mcpd
 *       knolleary/PubSubClient@^2.8
 *       bblanchon/ArduinoJson@^7
 */

#include <mcpd.h>
#include <tools/MCPMQTTTool.h>
#include <tools/MCPSystemTool.h>

// ── Configuration ──────────────────────────────────────────────────────

const char* WIFI_SSID     = "YourWiFi";
const char* WIFI_PASSWORD = "YourPassword";

// Optional: pre-configure MQTT broker (or let AI connect via tool)
const char* MQTT_BROKER   = "";  // e.g. "192.168.1.100"
const int   MQTT_PORT     = 1883;

// ── Objects ────────────────────────────────────────────────────────────

mcpd::Server mcp("mqtt-bridge");
mcpd::tools::MQTTTool mqtt;

void setup() {
    Serial.begin(115200);
    Serial.println("\n[mqtt-bridge] Starting...");

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[mqtt-bridge] Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[mqtt-bridge] Connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());

    // Attach built-in tools
    mqtt.attach(mcp);
    mcpd::tools::SystemTool::attach(mcp);

    // Add a custom prompt for common MQTT workflows
    mcp.addPrompt("monitor_sensors",
        "Set up monitoring for common IoT sensor topics",
        {
            {"broker", "MQTT broker address", true},
            {"topics", "Comma-separated topic list to monitor", false}
        },
        [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
            String broker = args.at("broker");
            String topics = "sensors/#";
            auto it = args.find("topics");
            if (it != args.end()) topics = it->second;

            String instructions = String(
                "Please connect to the MQTT broker at ") + broker +
                " and subscribe to these topics: " + topics +
                "\n\nThen periodically check for new messages and summarize "
                "the sensor readings. Alert me if any values seem unusual.";

            return {
                mcpd::MCPPromptMessage("user", instructions.c_str())
            };
        });

    // Start MCP server
    mcp.begin();

    // Auto-connect to MQTT if configured
    if (strlen(MQTT_BROKER) > 0) {
        Serial.printf("[mqtt-bridge] Auto-connecting to MQTT: %s:%d\n",
                      MQTT_BROKER, MQTT_PORT);
        // The AI can also connect manually via mqtt_connect tool
    }

    Serial.printf("[mqtt-bridge] MCP server ready at http://%s/mcp\n",
                  WiFi.localIP().toString().c_str());
}

void loop() {
    mcp.loop();
    mqtt.loop();
}
