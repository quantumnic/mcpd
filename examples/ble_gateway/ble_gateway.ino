/**
 * mcpd Example: BLE Gateway
 *
 * Demonstrates mcpd v0.10.0 features:
 *   - BLE transport for phone/tablet access without WiFi
 *   - Rate limiting to protect the device
 *   - Connection lifecycle hooks for status LED
 *   - Watchdog tool for production reliability
 *
 * The device acts as both a WiFi and BLE MCP server, allowing
 * AI assistants to connect via either transport.
 *
 * Hardware: ESP32 with built-in LED
 */

#include <WiFi.h>
#include <mcpd.h>
#include <tools/MCPGPIOTool.h>
#include <tools/MCPSystemTool.h>
#include <tools/MCPWatchdogTool.h>

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

mcpd::Server mcp("ble-gateway");

#define LED_PIN 2

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // Register tools
    mcpd::tools::GPIOTool::registerAll(mcp);
    mcpd::tools::SystemTool::registerAll(mcp);
    mcpd::tools::WatchdogTool::registerAll(mcp);

    // Custom sensor tool
    mcp.addTool("read_battery", "Read battery voltage (simulated)",
        R"({"type":"object","properties":{}})",
        [](const JsonObject&) -> String {
            float voltage = 3.3 + (random(100) / 1000.0);
            return String("{\"voltage\":") + String(voltage, 3) +
                   ",\"percentage\":" + String((int)((voltage - 3.0) / 1.2 * 100)) + "}";
        });

    // Enable BLE transport — works without WiFi too
    mcp.enableBLE("mcpd-gateway");

    // Enable WebSocket for web clients
    mcp.enableWebSocket(8081);

    // Rate limit: 5 requests/sec with burst of 10
    mcp.setRateLimit(5.0, 10);

    // Lifecycle hooks
    mcp.onInitialize([](const String& clientName) {
        Serial.printf("🤖 Client connected: %s\n", clientName.c_str());
        digitalWrite(LED_PIN, HIGH);
    });

    mcp.onConnect([]() {
        Serial.println("📡 Transport connected");
    });

    mcp.onDisconnect([]() {
        Serial.println("📡 Transport disconnected");
        digitalWrite(LED_PIN, LOW);
    });

    mcp.begin();
}

void loop() {
    mcp.loop();
}
