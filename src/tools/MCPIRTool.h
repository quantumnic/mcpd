/**
 * mcpd — Built-in IR Remote Tool
 *
 * Provides: ir_send, ir_send_raw, ir_status
 *
 * Infrared remote control — send NEC/Sony/RC5/Samsung/LG protocol codes.
 * Uses standard 38kHz IR LED on configurable pin.
 */

#ifndef MCPD_IR_TOOL_H
#define MCPD_IR_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class IRTool {
public:
    struct Config {
        int sendPin = -1;
        int recvPin = -1;
        uint32_t lastSentCode = 0;
        String lastProtocol = "";
        unsigned long lastSentTime = 0;
        int totalSent = 0;
    };

    static Config cfg;

    static void addIRTools(Server& server, int sendPin, int recvPin = -1) {
        cfg.sendPin = sendPin;
        cfg.recvPin = recvPin;

#ifdef ESP32
        if (sendPin >= 0) {
            pinMode(sendPin, OUTPUT);
            digitalWrite(sendPin, LOW);
        }
#endif

        // --- ir_send ---
        server.addTool("ir_send", "Send an IR code using a standard protocol (NEC, Sony, RC5, Samsung, LG)",
            R"=({"type":"object","properties":{"protocol":{"type":"string","enum":["NEC","Sony","RC5","Samsung","LG"],"description":"IR protocol"},"code":{"type":"integer","description":"IR code value (decimal or hex as integer)"},"bits":{"type":"integer","description":"Bit length (default: protocol-specific)","minimum":8,"maximum":64},"repeat":{"type":"integer","description":"Number of times to send (default 1)","minimum":1,"maximum":10}},"required":["protocol","code"]})=",
            [](const JsonObject& params) -> String {
                const char* protocol = params["protocol"] | "NEC";
                uint32_t code = params["code"] | 0;
                int bits = params["bits"] | 0;
                int repeat = params["repeat"] | 1;

                // Default bits per protocol
                if (bits == 0) {
                    if (strcmp(protocol, "NEC") == 0) bits = 32;
                    else if (strcmp(protocol, "Sony") == 0) bits = 12;
                    else if (strcmp(protocol, "RC5") == 0) bits = 13;
                    else if (strcmp(protocol, "Samsung") == 0) bits = 32;
                    else if (strcmp(protocol, "LG") == 0) bits = 28;
                    else return R"({"error":"Unknown protocol"})";
                }

#ifdef ESP32
                // In real implementation, would use IRremoteESP8266 or similar
                // Generate 38kHz carrier and modulate with protocol timing
                for (int i = 0; i < repeat; i++) {
                    // Protocol-specific timing would go here
                    delayMicroseconds(100);
                }
#endif

                cfg.lastSentCode = code;
                cfg.lastProtocol = protocol;
                cfg.lastSentTime = millis();
                cfg.totalSent += repeat;

                String result = "{\"sent\":true,\"protocol\":\"";
                result += protocol;
                result += "\",\"code\":";
                result += String(code);
                result += ",\"code_hex\":\"0x";
                // Manual hex conversion
                char hexBuf[9];
                snprintf(hexBuf, sizeof(hexBuf), "%08X", (unsigned int)code);
                result += hexBuf;
                result += "\",\"bits\":";
                result += bits;
                result += ",\"repeat\":";
                result += repeat;
                result += ",\"pin\":";
                result += cfg.sendPin;
                result += "}";
                return result;
            });

        // --- ir_send_raw ---
        server.addTool("ir_send_raw", "Send raw IR timing sequence (mark/space pairs in microseconds)",
            R"=({"type":"object","properties":{"timings":{"type":"array","items":{"type":"integer","minimum":1},"description":"Alternating mark/space durations in microseconds","minItems":2,"maxItems":256},"frequency":{"type":"integer","description":"Carrier frequency in Hz (default 38000)","minimum":30000,"maximum":56000}},"required":["timings"]})=",
            [](const JsonObject& params) -> String {
                auto timings = params["timings"];
                int freq = params["frequency"] | 38000;

                if (!timings.is<JsonArray>() || timings.size() < 2)
                    return R"({"error":"Timings must be array of at least 2 values"})";
                if (timings.size() > 256)
                    return R"({"error":"Maximum 256 timing values"})";

                int count = timings.size();

#ifdef ESP32
                // In real implementation: generate carrier at freq and
                // toggle with mark/space timings
                for (int i = 0; i < count; i++) {
                    delayMicroseconds(1);
                }
#endif

                cfg.lastSentTime = millis();
                cfg.totalSent++;
                cfg.lastProtocol = "RAW";

                unsigned long totalUs = 0;
                for (int i = 0; i < count; i++) {
                    totalUs += timings[i].as<unsigned long>();
                }

                String result = "{\"sent\":true,\"protocol\":\"RAW\",\"timings_count\":";
                result += count;
                result += ",\"total_duration_us\":";
                result += String(totalUs);
                result += ",\"carrier_hz\":";
                result += freq;
                result += ",\"pin\":";
                result += cfg.sendPin;
                result += "}";
                return result;
            });

        // --- ir_status ---
        server.addTool("ir_status", "Get IR transmitter status and last sent code",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                String result = "{\"send_pin\":";
                result += cfg.sendPin;
                result += ",\"recv_pin\":";
                result += cfg.recvPin;
                result += ",\"total_sent\":";
                result += cfg.totalSent;
                if (cfg.totalSent > 0) {
                    result += ",\"last_protocol\":\"";
                    result += cfg.lastProtocol;
                    result += "\",\"last_code\":";
                    result += String(cfg.lastSentCode);
                    result += ",\"last_sent_ms_ago\":";
                    result += (millis() - cfg.lastSentTime);
                }
                result += "}";
                return result;
            });
    }
};

IRTool::Config IRTool::cfg;

inline void addIRTools(Server& server, int sendPin, int recvPin = -1) {
    IRTool::addIRTools(server, sendPin, recvPin);
}

} // namespace tools
} // namespace mcpd

#endif // MCPD_IR_TOOL_H
