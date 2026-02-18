/**
 * mcpd — Built-in Relay Control Tool
 *
 * Provides: relay_set, relay_get, relay_toggle, relay_pulse, relay_all_off, relay_status
 *
 * Controls relay modules (1/2/4/8 channel) with safety features:
 * - Active-low or active-high configuration per relay
 * - Interlock groups (mutually exclusive relays, e.g., heat+cool)
 * - Maximum-on timers (auto-off after N seconds for safety)
 * - Labeled channels for human-readable identification
 *
 * Use cases: home automation, industrial control, irrigation,
 * motor direction control, HVAC, lighting.
 */

#ifndef MCPD_RELAY_TOOL_H
#define MCPD_RELAY_TOOL_H

#include "../mcpd.h"
#include <vector>

namespace mcpd {
namespace tools {

class RelayTool {
public:
    struct RelayChannel {
        uint8_t pin;
        String label;
        bool activeLow;      // true = LOW activates relay (common for relay modules)
        bool state;           // logical state (true = ON)
        int interlockGroup;   // -1 = none, same group = mutually exclusive
        unsigned long maxOnMs; // 0 = no limit
        unsigned long onSince; // millis() when turned on, 0 if off
        unsigned long totalOnMs; // cumulative on-time
        unsigned long switchCount;
    };

    static std::vector<RelayChannel>& channels() {
        static std::vector<RelayChannel> ch;
        return ch;
    }

    static int addChannel(uint8_t pin, const char* label, bool activeLow = true,
                           int interlockGroup = -1, unsigned long maxOnSec = 0) {
        RelayChannel ch;
        ch.pin = pin;
        ch.label = label;
        ch.activeLow = activeLow;
        ch.state = false;
        ch.interlockGroup = interlockGroup;
        ch.maxOnMs = maxOnSec * 1000;
        ch.onSince = 0;
        ch.totalOnMs = 0;
        ch.switchCount = 0;

#ifdef ESP32
        pinMode(pin, OUTPUT);
        digitalWrite(pin, activeLow ? HIGH : LOW); // start OFF
#endif

        int idx = channels().size();
        channels().push_back(ch);
        return idx;
    }

    static bool setRelay(int idx, bool on) {
        if (idx < 0 || idx >= (int)channels().size()) return false;
        RelayChannel& ch = channels()[idx];

        // If turning on, check interlocks — turn off conflicting relays
        if (on && ch.interlockGroup >= 0) {
            for (int i = 0; i < (int)channels().size(); i++) {
                if (i == idx) continue;
                RelayChannel& other = channels()[i];
                if (other.interlockGroup == ch.interlockGroup && other.state) {
                    setRelay(i, false); // turn off conflicting
                }
            }
        }

        bool wasOn = ch.state;
        ch.state = on;

#ifdef ESP32
        digitalWrite(ch.pin, (on != ch.activeLow) ? HIGH : LOW);
#endif

        if (on && !wasOn) {
            ch.onSince = millis();
            ch.switchCount++;
        } else if (!on && wasOn) {
            if (ch.onSince > 0) {
                ch.totalOnMs += millis() - ch.onSince;
            }
            ch.onSince = 0;
            ch.switchCount++;
        }

        return true;
    }

    // Check max-on timers, call from loop()
    static void checkTimers() {
        unsigned long now = millis();
        for (int i = 0; i < (int)channels().size(); i++) {
            RelayChannel& ch = channels()[i];
            if (ch.state && ch.maxOnMs > 0 && ch.onSince > 0) {
                if (now - ch.onSince >= ch.maxOnMs) {
                    setRelay(i, false); // safety auto-off
                }
            }
        }
    }

    static int findByLabel(const char* label) {
        for (int i = 0; i < (int)channels().size(); i++) {
            if (channels()[i].label.equalsIgnoreCase(label)) return i;
        }
        return -1;
    }

    static int resolveIndex(const JsonObject& args) {
        if (args.containsKey("channel")) return args["channel"].as<int>();
        if (args.containsKey("label")) return findByLabel(args["label"].as<const char*>());
        return -1;
    }

    static String channelJson(int idx) {
        RelayChannel& ch = channels()[idx];
        unsigned long onTime = ch.totalOnMs;
        if (ch.state && ch.onSince > 0) onTime += millis() - ch.onSince;

        return String("{\"channel\":") + idx +
               ",\"label\":\"" + ch.label +
               "\",\"pin\":" + ch.pin +
               ",\"state\":\"" + (ch.state ? "ON" : "OFF") +
               "\",\"active_low\":" + (ch.activeLow ? "true" : "false") +
               ",\"interlock_group\":" + ch.interlockGroup +
               ",\"max_on_s\":" + (ch.maxOnMs / 1000) +
               ",\"on_time_s\":" + (onTime / 1000) +
               ",\"switches\":" + ch.switchCount + "}";
    }

    static void attach(Server& server) {
        // relay_set — turn relay on or off
        server.addTool("relay_set", "Turn a relay channel ON or OFF. Use channel index or label.",
            R"=({"type":"object","properties":{"channel":{"type":"integer","description":"Relay channel index (0-based)"},"label":{"type":"string","description":"Relay label (alternative to channel index)"},"state":{"type":"string","enum":["on","off"],"description":"Desired state"}},"required":["state"]})=",
            [](const JsonObject& args) -> String {
                int idx = resolveIndex(args);
                if (idx < 0 || idx >= (int)channels().size())
                    return R"({"error":"Invalid relay channel. Provide 'channel' index or 'label'"})";

                const char* stateStr = args["state"] | "";
                bool on = (strcmp(stateStr, "on") == 0);
                setRelay(idx, on);
                return channelJson(idx);
            });

        // relay_get — read current state
        server.addTool("relay_get", "Read the current state of a relay channel",
            R"({"type":"object","properties":{"channel":{"type":"integer","description":"Relay channel index"},"label":{"type":"string","description":"Relay label"}},"required":[]})",
            [](const JsonObject& args) -> String {
                int idx = resolveIndex(args);
                if (idx < 0 || idx >= (int)channels().size())
                    return R"({"error":"Invalid relay channel"})";
                return channelJson(idx);
            });

        // relay_toggle — toggle relay state
        server.addTool("relay_toggle", "Toggle a relay channel (ON→OFF or OFF→ON)",
            R"({"type":"object","properties":{"channel":{"type":"integer","description":"Relay channel index"},"label":{"type":"string","description":"Relay label"}},"required":[]})",
            [](const JsonObject& args) -> String {
                int idx = resolveIndex(args);
                if (idx < 0 || idx >= (int)channels().size())
                    return R"({"error":"Invalid relay channel"})";
                setRelay(idx, !channels()[idx].state);
                return channelJson(idx);
            });

        // relay_pulse — briefly activate a relay
        server.addTool("relay_pulse", "Pulse a relay ON for a specified duration then auto-OFF",
            R"=({"type":"object","properties":{"channel":{"type":"integer","description":"Relay channel index"},"label":{"type":"string","description":"Relay label"},"duration_ms":{"type":"integer","minimum":50,"maximum":30000,"description":"Pulse duration in milliseconds (50-30000)"}},"required":["duration_ms"]})=",
            [](const JsonObject& args) -> String {
                int idx = resolveIndex(args);
                if (idx < 0 || idx >= (int)channels().size())
                    return R"({"error":"Invalid relay channel"})";
                int dur = args["duration_ms"] | 500;
                if (dur < 50 || dur > 30000)
                    return R"({"error":"Duration must be 50-30000ms"})";

                setRelay(idx, true);
#ifdef ESP32
                delay(dur);
#endif
                setRelay(idx, false);

                return String("{\"channel\":") + idx +
                       ",\"label\":\"" + channels()[idx].label +
                       "\",\"pulsed_ms\":" + dur + "}";
            });

        // relay_all_off — emergency all-off
        server.addTool("relay_all_off", "Turn ALL relays OFF immediately (emergency/safety stop)",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                int count = 0;
                for (int i = 0; i < (int)channels().size(); i++) {
                    if (channels()[i].state) {
                        setRelay(i, false);
                        count++;
                    }
                }
                return String("{\"turned_off\":") + count +
                       ",\"total_channels\":" + channels().size() + "}";
            });

        // relay_status — overview of all channels
        server.addTool("relay_status", "Get status of all relay channels",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                checkTimers();
                int onCount = 0;
                for (auto& ch : channels()) if (ch.state) onCount++;

                String result = String("{\"total_channels\":") + channels().size() +
                               ",\"active\":" + onCount + ",\"channels\":[";
                for (int i = 0; i < (int)channels().size(); i++) {
                    if (i > 0) result += ",";
                    result += channelJson(i);
                }
                result += "]}";
                return result;
            });
    }
};

} // namespace tools

/**
 * Register relay tools. Call addRelayChannel() first to define channels, then this.
 *
 * @param server  The mcpd::Server instance
 */
inline void addRelayTools(Server& server) {
    tools::RelayTool::attach(server);
}

/**
 * Add a relay channel before calling addRelayTools().
 *
 * @param pin            GPIO pin connected to relay
 * @param label          Human-readable label (e.g., "pump", "heater", "light")
 * @param activeLow      true if LOW activates relay (common for relay modules)
 * @param interlockGroup Group number for mutual exclusion (-1 = none)
 * @param maxOnSec       Auto-off after N seconds (0 = no limit)
 * @return Channel index
 */
inline int addRelayChannel(uint8_t pin, const char* label, bool activeLow = true,
                            int interlockGroup = -1, unsigned long maxOnSec = 0) {
    return tools::RelayTool::addChannel(pin, label, activeLow, interlockGroup, maxOnSec);
}

} // namespace mcpd

#endif // MCPD_RELAY_TOOL_H
