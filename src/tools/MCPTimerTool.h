/**
 * mcpd — Hardware Timer Tool
 *
 * Provides configurable hardware timers for periodic tasks,
 * one-shot delays, and frequency measurement.
 */

#ifndef MCPD_TIMER_TOOL_H
#define MCPD_TIMER_TOOL_H

#include "../MCPTool.h"
#include <map>

namespace mcpd {

namespace timer_internal {

struct TimerState {
    bool active = false;
    uint32_t interval_us = 0;
    bool oneshot = false;
    volatile uint32_t fire_count = 0;
    hw_timer_t* hw_timer = nullptr;
};

// Up to 4 hardware timers on ESP32
static TimerState timers[4];

static void IRAM_ATTR onTimer0() { timers[0].fire_count++; }
static void IRAM_ATTR onTimer1() { timers[1].fire_count++; }
static void IRAM_ATTR onTimer2() { timers[2].fire_count++; }
static void IRAM_ATTR onTimer3() { timers[3].fire_count++; }

static void (*isrTable[4])() = { onTimer0, onTimer1, onTimer2, onTimer3 };

} // namespace timer_internal

/**
 * Register hardware timer tools on the server.
 *
 * Tools registered:
 *   - timer_start   — start a hardware timer (periodic or one-shot)
 *   - timer_stop    — stop a running timer
 *   - timer_status  — read timer fire count and state
 *   - timer_millis  — precise timing: read millis/micros timestamps
 *   - timer_pulse_in — measure pulse width on a GPIO pin
 */
inline void registerTimerTools(Server& server) {

    // ── timer_start ────────────────────────────────────────────────
    {
        MCPTool tool(
            "timer_start",
            "Start a hardware timer. ESP32 has timers 0-3. "
            "Can be periodic (repeating) or one-shot. Tracks fire count.",
            R"=({
                "type":"object",
                "properties":{
                    "timer_id":{"type":"integer","minimum":0,"maximum":3,"description":"Timer number (0-3)"},
                    "interval_us":{"type":"integer","minimum":10,"description":"Interval in microseconds"},
                    "oneshot":{"type":"boolean","description":"If true, fires once then stops (default false)"}
                },
                "required":["timer_id","interval_us"]
            })=",
            [](const JsonObject& args) -> String {
                int id = args["timer_id"].as<int>();
                if (id < 0 || id > 3) return R"({"error":"timer_id must be 0-3"})";

                uint32_t interval = args["interval_us"].as<uint32_t>();
                if (interval < 10) return R"({"error":"interval_us must be >= 10"})";

                bool oneshot = args.containsKey("oneshot") ? args["oneshot"].as<bool>() : false;

                auto& ts = timer_internal::timers[id];

                // Stop existing timer if running
                if (ts.hw_timer) {
                    timerAlarmDisable(ts.hw_timer);
                    timerDetachInterrupt(ts.hw_timer);
                    timerEnd(ts.hw_timer);
                }

                ts.active = true;
                ts.interval_us = interval;
                ts.oneshot = oneshot;
                ts.fire_count = 0;

                ts.hw_timer = timerBegin(id, 80, true); // 80MHz / 80 = 1MHz (1us ticks)
                timerAttachInterrupt(ts.hw_timer, timer_internal::isrTable[id], true);
                timerAlarmWrite(ts.hw_timer, interval, !oneshot); // autoreload = !oneshot
                timerAlarmEnable(ts.hw_timer);

                JsonDocument doc;
                doc["status"] = "started";
                doc["timer_id"] = id;
                doc["interval_us"] = interval;
                doc["oneshot"] = oneshot;
                String out;
                serializeJson(doc, out);
                return out;
            }
        );
        tool.markLocalOnly();
        server.addTool(tool);
    }

    // ── timer_stop ─────────────────────────────────────────────────
    {
        MCPTool tool(
            "timer_stop",
            "Stop a running hardware timer.",
            R"=({
                "type":"object",
                "properties":{
                    "timer_id":{"type":"integer","minimum":0,"maximum":3,"description":"Timer number (0-3)"}
                },
                "required":["timer_id"]
            })=",
            [](const JsonObject& args) -> String {
                int id = args["timer_id"].as<int>();
                if (id < 0 || id > 3) return R"({"error":"timer_id must be 0-3"})";

                auto& ts = timer_internal::timers[id];
                if (ts.hw_timer) {
                    timerAlarmDisable(ts.hw_timer);
                    timerDetachInterrupt(ts.hw_timer);
                    timerEnd(ts.hw_timer);
                    ts.hw_timer = nullptr;
                }

                uint32_t finalCount = ts.fire_count;
                ts.active = false;

                JsonDocument doc;
                doc["status"] = "stopped";
                doc["timer_id"] = id;
                doc["total_fires"] = finalCount;
                String out;
                serializeJson(doc, out);
                return out;
            }
        );
        tool.markLocalOnly();
        server.addTool(tool);
    }

    // ── timer_status ───────────────────────────────────────────────
    {
        MCPTool tool(
            "timer_status",
            "Read the status and fire count of a hardware timer.",
            R"=({
                "type":"object",
                "properties":{
                    "timer_id":{"type":"integer","minimum":0,"maximum":3,"description":"Timer number (0-3)"},
                    "reset_count":{"type":"boolean","description":"Reset fire count after reading (default false)"}
                },
                "required":["timer_id"]
            })=",
            [](const JsonObject& args) -> String {
                int id = args["timer_id"].as<int>();
                if (id < 0 || id > 3) return R"({"error":"timer_id must be 0-3"})";

                auto& ts = timer_internal::timers[id];
                bool resetCount = args.containsKey("reset_count")
                    ? args["reset_count"].as<bool>() : false;

                uint32_t count = ts.fire_count;
                if (resetCount) ts.fire_count = 0;

                JsonDocument doc;
                doc["timer_id"] = id;
                doc["active"] = ts.active;
                doc["interval_us"] = ts.interval_us;
                doc["oneshot"] = ts.oneshot;
                doc["fire_count"] = count;
                String out;
                serializeJson(doc, out);
                return out;
            }
        );
        tool.markReadOnly().markLocalOnly();
        server.addTool(tool);
    }

    // ── timer_millis ───────────────────────────────────────────────
    {
        MCPTool tool(
            "timer_millis",
            "Read precise timestamps: millis() and micros() for timing measurements.",
            R"({"type":"object","properties":{}})",
            [](const JsonObject& args) -> String {
                JsonDocument doc;
                doc["millis"] = millis();
                doc["micros"] = micros();
                String out;
                serializeJson(doc, out);
                return out;
            }
        );
        tool.markReadOnly().markLocalOnly().markIdempotent();
        server.addTool(tool);
    }

    // ── timer_pulse_in ─────────────────────────────────────────────
    {
        MCPTool tool(
            "timer_pulse_in",
            "Measure pulse width on a GPIO pin (like Arduino pulseIn). "
            "Useful for ultrasonic sensors (HC-SR04), IR receivers, etc.",
            R"=({
                "type":"object",
                "properties":{
                    "pin":{"type":"integer","description":"GPIO pin number"},
                    "level":{"type":"integer","enum":[0,1],"description":"Which level to measure: HIGH=1 or LOW=0"},
                    "timeout_us":{"type":"integer","description":"Timeout in microseconds (default 1000000 = 1s)","minimum":1}
                },
                "required":["pin","level"]
            })=",
            [](const JsonObject& args) -> String {
                int pin = args["pin"].as<int>();
                int level = args["level"].as<int>();
                unsigned long timeout = args.containsKey("timeout_us")
                    ? args["timeout_us"].as<unsigned long>() : 1000000UL;

                unsigned long duration = pulseIn(pin, level, timeout);

                JsonDocument doc;
                doc["pin"] = pin;
                doc["level"] = level;
                doc["pulse_us"] = duration;
                doc["timed_out"] = (duration == 0);

                // For HC-SR04: distance_cm = duration / 58.0
                if (duration > 0) {
                    doc["hc_sr04_cm"] = (float)duration / 58.0f;
                }

                String out;
                serializeJson(doc, out);
                return out;
            }
        );
        tool.markReadOnly().markLocalOnly();
        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_TIMER_TOOL_H
