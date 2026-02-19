/**
 * mcpd — Built-in GPS Tool
 *
 * Provides: gps_read, gps_satellites, gps_speed, gps_distance, gps_status
 *
 * Reads NMEA data from GPS modules (NEO-6M, NEO-7M, NEO-8M, BN-220, etc.)
 * connected via UART. Parses GGA and RMA sentences for position, altitude,
 * speed, and satellite info.
 *
 * Use cases: asset tracking, geofencing, navigation, environmental monitoring
 * with location stamps, fleet management.
 */

#ifndef MCPD_GPS_TOOL_H
#define MCPD_GPS_TOOL_H

#include "../mcpd.h"
#include <math.h>

namespace mcpd {
namespace tools {

class GPSTool {
public:
    struct GPSData {
        double latitude = 0.0;
        double longitude = 0.0;
        double altitude = 0.0;    // meters
        double speed = 0.0;       // km/h
        double course = 0.0;      // degrees
        double hdop = 99.9;
        int satellites = 0;
        bool fix = false;
        unsigned long lastFixTime = 0;
        unsigned long totalFixes = 0;
        int hour = 0, minute = 0, second = 0;
        int day = 0, month = 0, year = 0;
        // Waypoint for distance calc
        double wpLat = 0.0;
        double wpLon = 0.0;
        bool wpSet = false;
    };

    static GPSData& data() {
        static GPSData d;
        return d;
    }

    static HardwareSerial*& serial() {
        static HardwareSerial* s = nullptr;
        return s;
    }

    // Haversine distance in meters
    static double haversine(double lat1, double lon1, double lat2, double lon2) {
        const double R = 6371000.0; // Earth radius in meters
        double dLat = (lat2 - lat1) * M_PI / 180.0;
        double dLon = (lon2 - lon1) * M_PI / 180.0;
        double a = sin(dLat / 2) * sin(dLat / 2) +
                   cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
                   sin(dLon / 2) * sin(dLon / 2);
        double c = 2 * atan2(sqrt(a), sqrt(1 - a));
        return R * c;
    }

    // Parse NMEA coordinate: DDDMM.MMMM -> decimal degrees
    static double parseCoord(const char* str, char dir) {
        if (!str || strlen(str) < 4) return 0.0;
        double raw = atof(str);
        int degrees = (int)(raw / 100);
        double minutes = raw - degrees * 100;
        double result = degrees + minutes / 60.0;
        if (dir == 'S' || dir == 'W') result = -result;
        return result;
    }

    // Parse a single NMEA sentence
    static bool parseSentence(const char* sentence) {
        if (!sentence || sentence[0] != '$') return false;
        if (strlen(sentence) < 10) return false;

        char buf[128];
        strncpy(buf, sentence, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        // Remove checksum
        char* star = strchr(buf, '*');
        if (star) *star = '\0';

        // Tokenize
        char* fields[20];
        int nFields = 0;
        char* tok = strtok(buf, ",");
        while (tok && nFields < 20) {
            fields[nFields++] = tok;
            tok = strtok(nullptr, ",");
        }

        if (nFields < 1) return false;

        // $GPGGA — position fix
        if (strstr(fields[0], "GGA") && nFields >= 12) {
            if (strlen(fields[2]) > 0 && strlen(fields[4]) > 0) {
                data().latitude = parseCoord(fields[2], fields[3][0]);
                data().longitude = parseCoord(fields[4], fields[5][0]);
                data().fix = (atoi(fields[6]) > 0);
                data().satellites = atoi(fields[7]);
                data().hdop = atof(fields[8]);
                if (strlen(fields[9]) > 0) data().altitude = atof(fields[9]);
                if (strlen(fields[1]) >= 6) {
                    data().hour = (fields[1][0] - '0') * 10 + (fields[1][1] - '0');
                    data().minute = (fields[1][2] - '0') * 10 + (fields[1][3] - '0');
                    data().second = (fields[1][4] - '0') * 10 + (fields[1][5] - '0');
                }
                if (data().fix) {
                    data().lastFixTime = millis();
                    data().totalFixes++;
                }
                return true;
            }
        }

        // $GPRMC — recommended minimum
        if (strstr(fields[0], "RMC") && nFields >= 10) {
            if (fields[2][0] == 'A') { // Active
                data().latitude = parseCoord(fields[3], fields[4][0]);
                data().longitude = parseCoord(fields[5], fields[6][0]);
                data().speed = atof(fields[7]) * 1.852; // knots to km/h
                data().course = atof(fields[8]);
                data().fix = true;
                if (strlen(fields[9]) >= 6) {
                    data().day = (fields[9][0] - '0') * 10 + (fields[9][1] - '0');
                    data().month = (fields[9][2] - '0') * 10 + (fields[9][3] - '0');
                    data().year = 2000 + (fields[9][4] - '0') * 10 + (fields[9][5] - '0');
                }
                data().lastFixTime = millis();
                data().totalFixes++;
                return true;
            }
        }

        return false;
    }

    // Process available serial data
    static void processSerial() {
        if (!serial()) return;
        static char nmea[128];
        static int pos = 0;

        while (serial()->available()) {
            char c = serial()->read();
            if (c == '\n' || c == '\r') {
                if (pos > 0) {
                    nmea[pos] = '\0';
                    parseSentence(nmea);
                    pos = 0;
                }
            } else if (pos < 126) {
                nmea[pos++] = c;
            }
        }
    }

    static void attach(Server& server, HardwareSerial* gpsSerial = nullptr, long baud = 9600) {
        if (gpsSerial) {
            serial() = gpsSerial;
#ifdef ESP32
            gpsSerial->begin(baud);
#endif
        }

        // gps_read — get current position
        server.addTool("gps_read", "Read current GPS position (latitude, longitude, altitude, time)",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                processSerial();
                GPSData& d = data();

                if (!d.fix) {
                    return R"=({"fix":false,"satellites":)" + String(d.satellites) +
                           R"(,"message":"No GPS fix yet. Ensure antenna has clear sky view."})=";
                }

                char latBuf[16], lonBuf[16], altBuf[16], hdopBuf[16];
                snprintf(latBuf, sizeof(latBuf), "%.6f", d.latitude);
                snprintf(lonBuf, sizeof(lonBuf), "%.6f", d.longitude);
                snprintf(altBuf, sizeof(altBuf), "%.1f", d.altitude);
                snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", d.hdop);

                char timeBuf[32] = "";
                if (d.year > 0) {
                    snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                             d.year, d.month, d.day, d.hour, d.minute, d.second);
                } else {
                    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", d.hour, d.minute, d.second);
                }

                unsigned long age = d.lastFixTime > 0 ? (millis() - d.lastFixTime) / 1000 : 0;

                return String("{\"fix\":true,\"latitude\":") + latBuf +
                       ",\"longitude\":" + lonBuf +
                       ",\"altitude_m\":" + altBuf +
                       ",\"satellites\":" + d.satellites +
                       ",\"hdop\":" + hdopBuf +
                       ",\"time\":\"" + timeBuf +
                       "\",\"fix_age_s\":" + age + "}";
            });

        // gps_satellites — satellite info
        server.addTool("gps_satellites", "Get GPS satellite count, signal quality, and fix status",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                processSerial();
                GPSData& d = data();
                char hdopBuf[16];
                snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", d.hdop);
                String quality = d.hdop < 1.0 ? "excellent" :
                                 d.hdop < 2.0 ? "good" :
                                 d.hdop < 5.0 ? "moderate" :
                                 d.hdop < 10.0 ? "fair" : "poor";

                return String("{\"satellites\":") + d.satellites +
                       ",\"fix\":" + (d.fix ? "true" : "false") +
                       ",\"hdop\":" + hdopBuf +
                       ",\"quality\":\"" + quality +
                       "\",\"total_fixes\":" + d.totalFixes + "}";
            });

        // gps_speed — current speed and course
        server.addTool("gps_speed", "Get current GPS speed and heading/course",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                processSerial();
                GPSData& d = data();

                if (!d.fix) return R"({"fix":false,"message":"No GPS fix"})";

                char spdBuf[16], mpsBuf[16], mphBuf[16], crsBuf[16];
                snprintf(spdBuf, sizeof(spdBuf), "%.1f", d.speed);
                snprintf(mpsBuf, sizeof(mpsBuf), "%.2f", d.speed / 3.6);
                snprintf(mphBuf, sizeof(mphBuf), "%.1f", d.speed * 0.621371);
                snprintf(crsBuf, sizeof(crsBuf), "%.1f", d.course);

                String cardinal;
                if (d.course < 22.5 || d.course >= 337.5) cardinal = "N";
                else if (d.course < 67.5) cardinal = "NE";
                else if (d.course < 112.5) cardinal = "E";
                else if (d.course < 157.5) cardinal = "SE";
                else if (d.course < 202.5) cardinal = "S";
                else if (d.course < 247.5) cardinal = "SW";
                else if (d.course < 292.5) cardinal = "W";
                else cardinal = "NW";

                return String("{\"speed_kmh\":") + spdBuf +
                       ",\"speed_ms\":" + mpsBuf +
                       ",\"speed_mph\":" + mphBuf +
                       ",\"course_deg\":" + crsBuf +
                       ",\"cardinal\":\"" + cardinal + "\"}";
            });

        // gps_distance — distance to a waypoint
        server.addTool("gps_distance", "Calculate distance from current position to a target coordinate, or set/clear a waypoint",
            R"=({"type":"object","properties":{"lat":{"type":"number","description":"Target latitude (-90 to 90)","minimum":-90,"maximum":90},"lon":{"type":"number","description":"Target longitude (-180 to 180)","minimum":-180,"maximum":180},"set_waypoint":{"type":"boolean","description":"If true, save this as a persistent waypoint for future distance checks"}},"required":[]})=",
            [](const JsonObject& args) -> String {
                processSerial();
                GPSData& d = data();

                if (!d.fix) return R"({"fix":false,"message":"No GPS fix"})";

                double targetLat, targetLon;
                bool useWaypoint = false;

                if (args.containsKey("lat") && args.containsKey("lon")) {
                    targetLat = args["lat"];
                    targetLon = args["lon"];

                    if (args["set_waypoint"] | false) {
                        d.wpLat = targetLat;
                        d.wpLon = targetLon;
                        d.wpSet = true;
                    }
                } else if (d.wpSet) {
                    targetLat = d.wpLat;
                    targetLon = d.wpLon;
                    useWaypoint = true;
                } else {
                    return R"({"error":"No target coordinates provided and no waypoint set"})";
                }

                double dist = haversine(d.latitude, d.longitude, targetLat, targetLon);
                char distBuf[16], tLatBuf[16], tLonBuf[16];
                snprintf(distBuf, sizeof(distBuf), "%.1f", dist);
                snprintf(tLatBuf, sizeof(tLatBuf), "%.6f", targetLat);
                snprintf(tLonBuf, sizeof(tLonBuf), "%.6f", targetLon);

                return String("{\"distance_m\":") + distBuf +
                       ",\"distance_km\":" + String(dist / 1000.0, 3) +
                       ",\"target_lat\":" + tLatBuf +
                       ",\"target_lon\":" + tLonBuf +
                       ",\"waypoint_active\":" + (d.wpSet ? "true" : "false") +
                       ",\"source\":\"" + (useWaypoint ? "waypoint" : "provided") + "\"}";
            });

        // gps_status — overall GPS module status
        server.addTool("gps_status", "Get GPS module status, fix info, and statistics",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                processSerial();
                GPSData& d = data();

                unsigned long uptime = millis() / 1000;
                unsigned long fixAge = d.lastFixTime > 0 ? (millis() - d.lastFixTime) / 1000 : 0;
                char hdopBuf[16];
                snprintf(hdopBuf, sizeof(hdopBuf), "%.1f", d.hdop);

                return String("{\"fix\":") + (d.fix ? "true" : "false") +
                       ",\"satellites\":" + d.satellites +
                       ",\"hdop\":" + hdopBuf +
                       ",\"total_fixes\":" + d.totalFixes +
                       ",\"last_fix_age_s\":" + fixAge +
                       ",\"uptime_s\":" + uptime +
                       ",\"waypoint_set\":" + (d.wpSet ? "true" : "false") +
                       ",\"serial_connected\":" + (serial() ? "true" : "false") + "}";
            });
    }
};

} // namespace tools

/**
 * Register GPS tools with a single call.
 *
 * @param server     The mcpd::Server instance
 * @param gpsSerial  HardwareSerial connected to GPS module (e.g. &Serial2)
 * @param baud       GPS baud rate (default: 9600)
 */
inline void addGPSTools(Server& server, HardwareSerial* gpsSerial = nullptr, long baud = 9600) {
    tools::GPSTool::attach(server, gpsSerial, baud);
}

} // namespace mcpd

#endif // MCPD_GPS_TOOL_H
