/**
 * mcpd — Smart Display Example
 *
 * Demonstrates LCD display + RTC clock + IR remote + DHT sensor.
 * AI can read/set the clock, display messages on the LCD, send IR
 * commands to control appliances, and monitor temperature/humidity.
 *
 * Hardware:
 *   - I2C LCD 16x2 (PCF8574 backpack, addr 0x27) → SDA/SCL
 *   - DS3231 RTC module → SDA/SCL (shared I2C bus)
 *   - DHT22 temperature/humidity sensor → GPIO 4
 *   - IR LED → GPIO 14
 *   - IR Receiver (optional) → GPIO 15
 *
 * Wiring:
 *   ESP32 SDA (GPIO 21) → LCD SDA + RTC SDA
 *   ESP32 SCL (GPIO 22) → LCD SCL + RTC SCL
 *   ESP32 GPIO 4        → DHT22 data
 *   ESP32 GPIO 14       → IR LED (with transistor driver)
 *   ESP32 GPIO 15       → IR Receiver signal (optional)
 */

#include <mcpd.h>
#include <mcpd/tools/MCPLCDTool.h>
#include <mcpd/tools/MCPRTCTool.h>
#include <mcpd/tools/MCPIRTool.h>
#include <mcpd/tools/MCPDHTTool.h>
#include <mcpd/tools/MCPSystemTool.h>

const char* WIFI_SSID = "your-ssid";
const char* WIFI_PASS = "your-password";

mcpd::Server mcp("smart-display");

#define DHT_PIN     4
#define IR_SEND_PIN 14
#define IR_RECV_PIN 15
#define LCD_ADDR    0x27
#define LCD_COLS    16
#define LCD_ROWS    2

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

    // Register tools
    mcpd::tools::addLCDTools(mcp, LCD_ADDR, LCD_COLS, LCD_ROWS);
    mcpd::tools::addRTCTools(mcp, 0x68, "DS3231");
    mcpd::tools::addIRTools(mcp, IR_SEND_PIN, IR_RECV_PIN);
    mcpd::tools::addDHTTools(mcp, DHT_PIN);
    mcpd::tools::addSystemTools(mcp);

    // Custom resource: current display + clock combined status
    mcp.addResource("display://status", "Display Status",
        "Current LCD content, time, and environment readings",
        "application/json",
        [](const String&) -> String {
            return R"({"display":"smart-display","features":["lcd","rtc","ir","dht"]})";
        });

    // Prompt: smart home control
    mcp.addPrompt("control_room", "Control the room: set display messages, adjust AC via IR, check time/temperature",
        {{"action", "string", "What to do (e.g., 'show time', 'turn on AC', 'display weather')", true}},
        [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
            String action = args.count("action") ? args.at("action") : "status";
            return {{
                "user",
                "You are controlling a smart display with LCD screen, real-time clock, IR remote, "
                "and temperature sensor. The user wants to: " + action + ". "
                "Use the available tools to accomplish this. Read the RTC for time, "
                "display info on the LCD, send IR codes for appliance control, "
                "and read DHT for temperature/humidity."
            }};
        });

    mcp.begin();
    Serial.println("Smart Display MCP server ready!");
    Serial.printf("Tools: lcd_print, lcd_clear, rtc_get, rtc_set, ir_send, ...\n");
}

void loop() {
    mcp.loop();
}
