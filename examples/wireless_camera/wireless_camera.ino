/**
 * mcpd — Wireless Camera Example
 *
 * ESP32-CAM as an MCP server: AI can capture photos, adjust camera settings,
 * and use ESP-NOW for peer-to-peer communication with other MCUs.
 *
 * Hardware: ESP32-CAM (AI-Thinker) or compatible ESP32 camera module.
 *
 * Features:
 *   - Camera capture with flash control
 *   - Resolution/quality adjustment via MCP tools
 *   - ESP-NOW mesh for coordination with other devices
 *   - System diagnostics (heap, uptime, WiFi RSSI)
 *   - MCP prompt for AI-guided surveillance/monitoring
 *
 * Wiring: Just plug in the ESP32-CAM — all pins are onboard.
 *
 * Claude prompt: "Initialize the camera at VGA resolution, take a photo,
 *                 and describe what you see."
 */

#include <mcpd.h>
#include <mcpd/tools/MCPCameraTool.h>
#include <mcpd/tools/MCPESPNowTool.h>
#include <mcpd/tools/MCPWiFiTool.h>
#include <mcpd/tools/MCPSystemTool.h>
#include <mcpd/tools/MCPGPIOTool.h>

// ── WiFi credentials ───────────────────────────────────────────────
const char* WIFI_SSID = "YourNetwork";
const char* WIFI_PASS = "YourPassword";

mcpd::Server mcp("esp32-camera", 80);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n⚡ mcpd Wireless Camera Example");

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Register tools
    mcpd::CameraPins pins;  // AI-Thinker defaults
    mcpd::addCameraTools(mcp, pins);
    mcpd::addESPNowTools(mcp);
    mcpd::tools::WiFiTool::attach(mcp);
    mcpd::tools::SystemTool::attach(mcp);
    mcpd::tools::GPIOTool::attach(mcp);

    // Add a resource for camera status
    mcp.addResource("camera://status", "Camera Status",
        "Current camera sensor settings and state",
        "application/json",
        []() -> String {
            return "{\"initialized\":" + String(_cameraInitialized ? "true" : "false") +
                   ",\"flash_pin\":" + String(_flashPin) + "}";
        });

    // Add a prompt for AI-guided monitoring
    mcpd::MCPPrompt monitorPrompt(
        "security_monitor",
        "AI-guided security monitoring — captures and analyzes photos periodically"
    );
    monitorPrompt.addArgument("interval_seconds", "Capture interval in seconds", false);
    monitorPrompt.addArgument("alert_keywords", "Comma-separated keywords to alert on", false);
    monitorPrompt.setHandler([](const std::map<String, String>& args) -> String {
        String interval = "30";
        String keywords = "person,motion,vehicle";
        auto it = args.find("interval_seconds");
        if (it != args.end()) interval = it->second;
        it = args.find("alert_keywords");
        if (it != args.end()) keywords = it->second;

        return String("You are monitoring a camera feed from an ESP32-CAM. ") +
               "Take a photo every " + interval + " seconds using camera_capture. " +
               "Analyze each image for: " + keywords + ". " +
               "If you detect anything notable, report it. " +
               "Start by initializing the camera with camera_init at VGA resolution, " +
               "then begin the monitoring loop.";
    });
    mcp.addPrompt(monitorPrompt);

    // Start MCP server
    mcp.begin();
    Serial.println("📷 Camera MCP server ready!");
    Serial.printf("   Tools: camera_init, camera_capture, camera_configure,\n");
    Serial.printf("          camera_status, camera_flash, espnow_*\n");
}

void loop() {
    mcp.loop();
}
