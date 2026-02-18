/**
 * mcpd Example — Modbus Industrial Gateway
 *
 * Demonstrates:
 *   - Modbus RTU master over RS-485 for PLC/sensor communication
 *   - Reading holding/input registers from industrial devices
 *   - Writing coils and registers for actuator control
 *   - Device scanning for bus discovery
 *   - System diagnostics and heap monitoring
 *
 * Hardware:
 *   - ESP32 with RS-485 transceiver (e.g., MAX485) on UART2
 *   - RS-485 bus connected to Modbus slave devices (PLCs, VFDs, sensors)
 *   - Optional: DE/RE pin for RS-485 direction control
 *
 * Wiring:
 *   ESP32 GPIO16 (RX) → MAX485 RO
 *   ESP32 GPIO17 (TX) → MAX485 DI
 *   ESP32 GPIO4        → MAX485 DE+RE (direction enable)
 *   MAX485 A/B         → RS-485 bus
 *
 * Usage:
 *   Claude: "Initialize Modbus RTU at 9600 baud"
 *   Claude: "Scan for devices on the Modbus bus"
 *   Claude: "Read holding registers 0-9 from slave 1"
 *   Claude: "Write value 1500 to register 40001 on slave 2"
 *   Claude: "Read the temperature from input register 0 on slave 3"
 *   Claude: "Turn on coil 0 on slave 1"
 *   Claude: "Show me the Modbus communication statistics"
 */

#include <mcpd.h>
#include <tools/MCPModbusTool.h>
#include <tools/MCPSystemTool.h>
#include <tools/MCPWiFiTool.h>

// ── Configuration ──────────────────────────────────────────────────────

const char* WIFI_SSID = "YOUR_SSID";
const char* WIFI_PASS = "YOUR_PASSWORD";
const char* SERVER_NAME = "modbus-gateway";

mcpd::Server mcp(SERVER_NAME);

// ── Custom resource: device map ────────────────────────────────────────

void setupResources() {
    // Expose a known device map as a resource
    mcp.addResource("modbus://devices",
        "Known Modbus devices on this bus",
        "application/json",
        []() -> String {
            return R"({
                "devices": [
                    {"address": 1, "type": "Siemens S7-1200", "description": "Main PLC"},
                    {"address": 2, "type": "ABB ACS580", "description": "Variable Frequency Drive"},
                    {"address": 3, "type": "Schneider TM221", "description": "Compact PLC"},
                    {"address": 10, "type": "Endress+Hauser", "description": "Flow meter"},
                    {"address": 11, "type": "Pt100 RTD", "description": "Temperature sensor"}
                ],
                "bus": {"baud": 9600, "parity": "none", "stopBits": 1}
            })";
        });

    // Common Modbus register map template
    mcp.addResourceTemplate(
        "modbus://slave/{slaveId}/registers/{start}/{count}",
        "Read a range of holding registers from a Modbus slave",
        "application/json");

    // Prompt for common industrial tasks
    mcp.addPrompt("modbus-diagnostics",
        "Run a full Modbus bus diagnostic scan and report",
        {},
        [](const std::map<String, String>& args) -> String {
            (void)args;
            return "Please perform the following diagnostic sequence:\n"
                   "1. Scan the Modbus bus for all responding devices (addresses 1-30)\n"
                   "2. For each found device, read holding registers 0-9\n"
                   "3. Check the Modbus error counters\n"
                   "4. Report any communication issues\n"
                   "5. Suggest optimizations (timeout, baud rate) if needed";
        });
}

// ── Setup ──────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== mcpd Modbus Gateway ===");

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Attach tools
    mcpd::tools::ModbusTool::attach(mcp);
    mcpd::tools::SystemTool::attach(mcp);
    mcpd::tools::WiFiTool::attach(mcp);

    // Setup resources and prompts
    setupResources();

    // Enable auth for production use
    // mcp.enableAuth("your-api-key");

    // Start MCP server
    mcp.begin();
    Serial.printf("MCP server '%s' running on port 80\n", SERVER_NAME);
    Serial.println("mDNS: modbus-gateway.local");
}

// ── Loop ───────────────────────────────────────────────────────────────

void loop() {
    mcp.loop();
}
