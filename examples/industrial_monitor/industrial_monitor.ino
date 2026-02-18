/**
 * mcpd Example — Industrial Process Monitor
 *
 * Demonstrates: SPI sensor reading, resource subscriptions, completion,
 * dynamic tools, logging, and prompts in an industrial monitoring context.
 *
 * Scenario: Monitor a tank with level sensor (SPI ADC), temperature probe,
 * and control valves via GPIO. AI can monitor levels, set thresholds,
 * and control the process.
 *
 * Hardware (simulated for demo):
 *   - SPI ADC (MCP3008) on CS pin 5 — tank level sensor
 *   - DS18B20 temperature on pin 4
 *   - Inlet valve on pin 16
 *   - Outlet valve on pin 17
 *   - Alarm buzzer on pin 18
 */

#include <mcpd.h>
#include <mcpd/tools/MCPGPIOTool.h>
#include <mcpd/tools/MCPSPITool.h>
#include <mcpd/tools/MCPSystemTool.h>

// ── Configuration ──────────────────────────────────────────────────────

const char* WIFI_SSID     = "YourNetwork";
const char* WIFI_PASSWORD = "YourPassword";

const uint8_t PIN_VALVE_INLET  = 16;
const uint8_t PIN_VALVE_OUTLET = 17;
const uint8_t PIN_ALARM        = 18;
const uint8_t PIN_SPI_CS_ADC   = 5;

// Process thresholds
float tankLevelHigh = 85.0;   // % — alarm threshold
float tankLevelLow  = 15.0;   // % — alarm threshold
float tempMax       = 80.0;   // °C — maximum safe temperature

// Simulated sensor state
float tankLevel = 52.3;       // %
float temperature = 23.7;     // °C
bool inletOpen = false;
bool outletOpen = false;
bool alarmActive = false;
unsigned long lastUpdate = 0;

// ── MCP Server ─────────────────────────────────────────────────────────

mcpd::Server mcp("industrial-monitor");

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Connect WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nConnected: %s\n", WiFi.localIP().toString().c_str());

    // GPIO setup
    pinMode(PIN_VALVE_INLET, OUTPUT);
    pinMode(PIN_VALVE_OUTLET, OUTPUT);
    pinMode(PIN_ALARM, OUTPUT);

    // ── Register Resources ─────────────────────────────────────────────

    mcp.addResource("process://tank/level", "Tank Level",
        "Current tank fill level as percentage (0-100%)",
        "application/json",
        []() -> String {
            JsonDocument doc;
            doc["level"] = tankLevel;
            doc["unit"] = "%";
            doc["timestamp"] = millis();
            doc["highThreshold"] = tankLevelHigh;
            doc["lowThreshold"] = tankLevelLow;
            doc["status"] = (tankLevel > tankLevelHigh) ? "HIGH" :
                            (tankLevel < tankLevelLow) ? "LOW" : "NORMAL";
            String result;
            serializeJson(doc, result);
            return result;
        });

    mcp.addResource("process://tank/temperature", "Tank Temperature",
        "Current process temperature",
        "application/json",
        []() -> String {
            JsonDocument doc;
            doc["temperature"] = temperature;
            doc["unit"] = "°C";
            doc["maxSafe"] = tempMax;
            doc["status"] = (temperature > tempMax) ? "OVER_TEMP" : "NORMAL";
            String result;
            serializeJson(doc, result);
            return result;
        });

    mcp.addResource("process://status", "Process Status",
        "Overall process status summary",
        "application/json",
        []() -> String {
            JsonDocument doc;
            doc["tankLevel"] = tankLevel;
            doc["temperature"] = temperature;
            doc["inletValve"] = inletOpen ? "OPEN" : "CLOSED";
            doc["outletValve"] = outletOpen ? "OPEN" : "CLOSED";
            doc["alarm"] = alarmActive;
            doc["uptimeMs"] = millis();
            String result;
            serializeJson(doc, result);
            return result;
        });

    // ── Resource Template ──────────────────────────────────────────────

    mcp.addResourceTemplate(
        "process://history/{metric}/{interval}",
        "Process History",
        "Historical data for a metric over a time interval",
        "application/json",
        [](const std::map<String, String>& params) -> String {
            String metric = params.at("metric");
            String interval = params.at("interval");
            JsonDocument doc;
            doc["metric"] = metric;
            doc["interval"] = interval;
            doc["note"] = "Historical data storage not implemented in this demo";
            JsonArray data = doc["data"].to<JsonArray>();
            // Generate some simulated history
            for (int i = 0; i < 10; i++) {
                JsonObject point = data.add<JsonObject>();
                point["t"] = i;
                point["v"] = (metric == "level") ? (tankLevel + random(-5, 5)) :
                             (temperature + random(-2, 2));
            }
            String result;
            serializeJson(doc, result);
            return result;
        });

    // ── Completion Providers ───────────────────────────────────────────

    mcp.completions().addResourceTemplateCompletion(
        "process://history/{metric}/{interval}", "metric",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"level", "temperature", "flow_rate", "pressure"};
        });

    mcp.completions().addResourceTemplateCompletion(
        "process://history/{metric}/{interval}", "interval",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {"1h", "6h", "24h", "7d", "30d"};
        });

    // ── Tools ──────────────────────────────────────────────────────────

    mcp.addTool("valve_control",
        "Open or close inlet/outlet valves. Safety interlocks prevent both open simultaneously.",
        R"({
            "type": "object",
            "properties": {
                "valve": {
                    "type": "string",
                    "enum": ["inlet", "outlet"],
                    "description": "Which valve to control"
                },
                "state": {
                    "type": "string",
                    "enum": ["open", "close"],
                    "description": "Desired valve state"
                }
            },
            "required": ["valve", "state"]
        })",
        [](const JsonObject& args) -> String {
            String valve = args["valve"] | "";
            String state = args["state"] | "";
            bool open = (state == "open");

            // Safety interlock: don't allow both valves open at once
            if (open) {
                if (valve == "inlet" && outletOpen) {
                    return R"({"error":"Safety interlock: close outlet valve before opening inlet"})";
                }
                if (valve == "outlet" && inletOpen) {
                    return R"({"error":"Safety interlock: close inlet valve before opening outlet"})";
                }
            }

            if (valve == "inlet") {
                inletOpen = open;
                digitalWrite(PIN_VALVE_INLET, open ? HIGH : LOW);
            } else if (valve == "outlet") {
                outletOpen = open;
                digitalWrite(PIN_VALVE_OUTLET, open ? HIGH : LOW);
            } else {
                return R"({"error":"Unknown valve"})";
            }

            JsonDocument doc;
            doc["valve"] = valve;
            doc["state"] = state;
            doc["success"] = true;
            String result;
            serializeJson(doc, result);
            return result;
        });

    mcp.addTool("set_threshold",
        "Set alarm thresholds for tank level or temperature",
        R"({
            "type": "object",
            "properties": {
                "parameter": {
                    "type": "string",
                    "enum": ["level_high", "level_low", "temp_max"],
                    "description": "Which threshold to set"
                },
                "value": {
                    "type": "number",
                    "description": "Threshold value"
                }
            },
            "required": ["parameter", "value"]
        })",
        [](const JsonObject& args) -> String {
            String param = args["parameter"] | "";
            float value = args["value"] | 0.0f;

            if (param == "level_high")     tankLevelHigh = value;
            else if (param == "level_low") tankLevelLow = value;
            else if (param == "temp_max")  tempMax = value;
            else return R"({"error":"Unknown parameter"})";

            JsonDocument doc;
            doc["parameter"] = param;
            doc["newValue"] = value;
            doc["success"] = true;
            String result;
            serializeJson(doc, result);
            return result;
        });

    mcp.addTool("alarm_acknowledge",
        "Acknowledge and silence an active alarm",
        R"({"type":"object","properties":{}})",
        [](const JsonObject& args) -> String {
            bool wasActive = alarmActive;
            alarmActive = false;
            digitalWrite(PIN_ALARM, LOW);

            JsonDocument doc;
            doc["previousState"] = wasActive ? "ACTIVE" : "INACTIVE";
            doc["currentState"] = "ACKNOWLEDGED";
            String result;
            serializeJson(doc, result);
            return result;
        });

    // ── Prompts ────────────────────────────────────────────────────────

    mcp.addPrompt("shift_handover",
        "Generate a shift handover report for the incoming operator",
        {},
        [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
            String summary = String("Generate a shift handover report. Current process state:\n") +
                "- Tank level: " + String(tankLevel, 1) + "% (thresholds: " +
                String(tankLevelLow, 0) + "-" + String(tankLevelHigh, 0) + "%)\n" +
                "- Temperature: " + String(temperature, 1) + "°C (max: " +
                String(tempMax, 0) + "°C)\n" +
                "- Inlet valve: " + (inletOpen ? "OPEN" : "CLOSED") + "\n" +
                "- Outlet valve: " + (outletOpen ? "OPEN" : "CLOSED") + "\n" +
                "- Alarm: " + (alarmActive ? "ACTIVE" : "Clear") + "\n" +
                "- Uptime: " + String(millis() / 1000) + "s\n\n" +
                "Summarize the status and note any concerns for the incoming operator.";
            return { mcpd::MCPPromptMessage("user", summary.c_str()) };
        });

    mcp.addPrompt("diagnose",
        "Diagnose a potential process issue",
        {
            mcpd::MCPPromptArgument("symptom", "Describe the observed symptom", true)
        },
        [](const std::map<String, String>& args) -> std::vector<mcpd::MCPPromptMessage> {
            String symptom = args.at("symptom");
            String msg = String("A process operator reports: \"") + symptom + "\"\n\n" +
                "Current readings:\n" +
                "- Tank: " + String(tankLevel, 1) + "%, Temp: " +
                String(temperature, 1) + "°C\n" +
                "- Valves: inlet=" + (inletOpen ? "OPEN" : "CLOSED") +
                ", outlet=" + (outletOpen ? "OPEN" : "CLOSED") + "\n\n" +
                "Diagnose the likely cause and recommend corrective actions.";
            return { mcpd::MCPPromptMessage("user", msg.c_str()) };
        });

    // Completion for diagnose prompt
    mcp.completions().addPromptCompletion("diagnose", "symptom",
        [](const String& argName, const String& partial) -> std::vector<String> {
            return {
                "Tank level rising unexpectedly",
                "Tank level dropping fast",
                "Temperature spiking",
                "Temperature not reaching setpoint",
                "Valve not responding",
                "Alarm keeps triggering",
                "Unusual noise from pump"
            };
        });

    // ── Attach built-in tools ──────────────────────────────────────────

    mcpd::tools::SystemTool::attach(mcp);

    // Enable pagination for large tool lists
    mcp.setPageSize(10);

    // Start the server
    mcp.begin();
    Serial.println("Industrial Monitor ready!");
}

void loop() {
    mcp.loop();

    // Simulate sensor updates every 2 seconds
    if (millis() - lastUpdate > 2000) {
        lastUpdate = millis();

        // Simulate tank level changes
        if (inletOpen) tankLevel += 0.5;
        if (outletOpen) tankLevel -= 0.8;
        tankLevel += (float)random(-10, 10) / 100.0;
        tankLevel = constrain(tankLevel, 0.0, 100.0);

        // Simulate temperature drift
        temperature += (float)random(-5, 5) / 100.0;

        // Check alarms
        bool newAlarm = (tankLevel > tankLevelHigh) ||
                        (tankLevel < tankLevelLow) ||
                        (temperature > tempMax);

        if (newAlarm && !alarmActive) {
            alarmActive = true;
            digitalWrite(PIN_ALARM, HIGH);
            mcp.logging().warning("process",
                ("Alarm triggered! Level=" + String(tankLevel, 1) +
                 "% Temp=" + String(temperature, 1) + "°C").c_str());
        }

        // Notify subscribers about resource updates
        mcp.notifyResourceUpdated("process://tank/level");
        mcp.notifyResourceUpdated("process://tank/temperature");
        mcp.notifyResourceUpdated("process://status");
    }
}
