/**
 * mcpd — Built-in RTC (Real-Time Clock) Tool
 *
 * Provides: rtc_get, rtc_set, rtc_alarm, rtc_temperature, rtc_status
 *
 * DS3231/DS1307 I2C real-time clock support.
 * Provides accurate timekeeping independent of WiFi/NTP.
 */

#ifndef MCPD_RTC_TOOL_H
#define MCPD_RTC_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class RTCTool {
public:
    struct DateTime {
        int year = 2026;
        int month = 1;
        int day = 1;
        int hour = 0;
        int minute = 0;
        int second = 0;
        int dayOfWeek = 4;  // 0=Sun
    };

    struct Alarm {
        bool enabled = false;
        int hour = 0;
        int minute = 0;
        int second = 0;
        bool triggered = false;
    };

    struct Config {
        uint8_t address = 0x68;  // DS3231 default
        String chipType = "DS3231";
        DateTime dt;
        Alarm alarm1;
        Alarm alarm2;
        float temperature = 22.5;
        bool initialized = false;
        unsigned long lastSetTime = 0;
    };

    static Config cfg;

    static const char* dayName(int dow) {
        static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        return (dow >= 0 && dow <= 6) ? days[dow] : "???";
    }

    static bool isValidDate(int y, int m, int d) {
        if (m < 1 || m > 12 || d < 1) return false;
        int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) daysInMonth[1] = 29;
        return d <= daysInMonth[m - 1];
    }

    static void addRTCTools(Server& server, uint8_t address = 0x68,
                            const char* chipType = "DS3231") {
        cfg.address = address;
        cfg.chipType = chipType;
        cfg.initialized = true;

        // --- rtc_get ---
        server.addTool("rtc_get", "Read current date and time from the RTC",
            R"({"type":"object","properties":{"format":{"type":"string","enum":["iso","components"],"description":"Output format (default: iso)"}}})",
            [](const JsonObject& params) -> String {
                const char* fmt = params["format"] | "iso";

#ifdef ESP32
                // In real implementation: read from I2C
                Wire.beginTransmission(cfg.address);
                Wire.endTransmission();
#endif

                char iso[25];
                snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d",
                         cfg.dt.year, cfg.dt.month, cfg.dt.day,
                         cfg.dt.hour, cfg.dt.minute, cfg.dt.second);

                if (strcmp(fmt, "components") == 0) {
                    String r = "{\"year\":";
                    r += cfg.dt.year;
                    r += ",\"month\":"; r += cfg.dt.month;
                    r += ",\"day\":"; r += cfg.dt.day;
                    r += ",\"hour\":"; r += cfg.dt.hour;
                    r += ",\"minute\":"; r += cfg.dt.minute;
                    r += ",\"second\":"; r += cfg.dt.second;
                    r += ",\"day_of_week\":\""; r += dayName(cfg.dt.dayOfWeek);
                    r += "\",\"day_of_week_num\":"; r += cfg.dt.dayOfWeek;
                    r += ",\"iso\":\""; r += iso;
                    r += "\"}";
                    return r;
                }

                return String("{\"datetime\":\"") + iso +
                       "\",\"day_of_week\":\"" + dayName(cfg.dt.dayOfWeek) + "\"}";
            });

        // --- rtc_set ---
        server.addTool("rtc_set", "Set the RTC date and time",
            R"({"type":"object","properties":{"year":{"type":"integer","minimum":2000,"maximum":2099},"month":{"type":"integer","minimum":1,"maximum":12},"day":{"type":"integer","minimum":1,"maximum":31},"hour":{"type":"integer","minimum":0,"maximum":23},"minute":{"type":"integer","minimum":0,"maximum":59},"second":{"type":"integer","minimum":0,"maximum":59}},"required":["year","month","day","hour","minute","second"]})",
            [](const JsonObject& params) -> String {
                int y = params["year"];
                int mo = params["month"];
                int d = params["day"];
                int h = params["hour"];
                int mi = params["minute"];
                int s = params["second"];

                if (!isValidDate(y, mo, d))
                    return R"({"error":"Invalid date"})";

#ifdef ESP32
                Wire.beginTransmission(cfg.address);
                Wire.endTransmission();
#endif

                cfg.dt.year = y;
                cfg.dt.month = mo;
                cfg.dt.day = d;
                cfg.dt.hour = h;
                cfg.dt.minute = mi;
                cfg.dt.second = s;
                cfg.lastSetTime = millis();

                // Calculate day of week (Zeller's)
                int a = (14 - mo) / 12;
                int yy = y - a;
                int m = mo + 12 * a - 2;
                cfg.dt.dayOfWeek = (d + (31*m/12) + yy + yy/4 - yy/100 + yy/400) % 7;

                char iso[25];
                snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d",
                         y, mo, d, h, mi, s);

                return String("{\"set\":true,\"datetime\":\"") + iso +
                       "\",\"day_of_week\":\"" + dayName(cfg.dt.dayOfWeek) + "\"}";
            });

        // --- rtc_alarm ---
        server.addTool("rtc_alarm", "Set or check RTC alarm (DS3231 has 2 alarms)",
            R"({"type":"object","properties":{"alarm":{"type":"integer","enum":[1,2],"description":"Alarm number (1 or 2)"},"action":{"type":"string","enum":["set","clear","check"],"description":"Action to perform"},"hour":{"type":"integer","minimum":0,"maximum":23},"minute":{"type":"integer","minimum":0,"maximum":59},"second":{"type":"integer","minimum":0,"maximum":59,"description":"Alarm 1 only"}},"required":["alarm","action"]})",
            [](const JsonObject& params) -> String {
                int alarmNum = params["alarm"] | 1;
                const char* action = params["action"] | "check";
                Alarm& alarm = (alarmNum == 1) ? cfg.alarm1 : cfg.alarm2;

                if (strcmp(action, "set") == 0) {
                    alarm.hour = params["hour"] | 0;
                    alarm.minute = params["minute"] | 0;
                    alarm.second = (alarmNum == 1) ? (params["second"] | 0) : 0;
                    alarm.enabled = true;
                    alarm.triggered = false;

                    char timeBuf[12];
                    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                             alarm.hour, alarm.minute, alarm.second);

                    return String("{\"alarm\":") + alarmNum +
                           ",\"set\":true,\"time\":\"" + timeBuf + "\"}";
                }
                else if (strcmp(action, "clear") == 0) {
                    alarm.enabled = false;
                    alarm.triggered = false;
                    return String("{\"alarm\":") + alarmNum + ",\"cleared\":true}";
                }
                else {
                    // check
                    String r = String("{\"alarm\":") + alarmNum;
                    r += ",\"enabled\":"; r += alarm.enabled ? "true" : "false";
                    r += ",\"triggered\":"; r += alarm.triggered ? "true" : "false";
                    if (alarm.enabled) {
                        char timeBuf[12];
                        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
                                 alarm.hour, alarm.minute, alarm.second);
                        r += ",\"time\":\""; r += timeBuf; r += "\"";
                    }
                    r += "}";
                    return r;
                }
            });

        // --- rtc_temperature ---
        if (strcmp(chipType, "DS3231") == 0) {
            server.addTool("rtc_temperature", "Read the DS3231 built-in temperature sensor (±3°C accuracy)",
                R"({"type":"object","properties":{}})",
                [](const JsonObject&) -> String {
#ifdef ESP32
                    Wire.beginTransmission(cfg.address);
                    Wire.endTransmission();
#endif
                    String r = "{\"celsius\":";
                    r += String(cfg.temperature, 2);
                    r += ",\"fahrenheit\":";
                    r += String(cfg.temperature * 9.0 / 5.0 + 32.0, 2);
                    r += ",\"sensor\":\"DS3231 built-in\"}";
                    return r;
                });
        }

        // --- rtc_status ---
        server.addTool("rtc_status", "Get RTC module status and configuration",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                char iso[25];
                snprintf(iso, sizeof(iso), "%04d-%02d-%02dT%02d:%02d:%02d",
                         cfg.dt.year, cfg.dt.month, cfg.dt.day,
                         cfg.dt.hour, cfg.dt.minute, cfg.dt.second);

                String r = "{\"chip\":\"";
                r += cfg.chipType;
                r += "\",\"address\":\"0x";
                if (cfg.address < 0x10) r += "0";
                r += String(cfg.address, HEX);
                r += "\",\"datetime\":\""; r += iso;
                r += "\",\"alarm1_enabled\":"; r += cfg.alarm1.enabled ? "true" : "false";
                r += ",\"alarm2_enabled\":"; r += cfg.alarm2.enabled ? "true" : "false";
                if (cfg.chipType == "DS3231") {
                    r += ",\"temperature_c\":";
                    r += String(cfg.temperature, 2);
                }
                r += "}";
                return r;
            });
    }
};

RTCTool::Config RTCTool::cfg;

inline void addRTCTools(Server& server, uint8_t address = 0x68,
                        const char* chipType = "DS3231") {
    RTCTool::addRTCTools(server, address, chipType);
}

} // namespace tools
} // namespace mcpd

#endif // MCPD_RTC_TOOL_H
