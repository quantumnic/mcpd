/**
 * mcpd — EEPROM Tool
 *
 * Non-volatile byte-level storage for AVR, RP2040, STM32, and ESP boards.
 * Unlike NVS (ESP32-specific key-value store), EEPROM provides direct
 * address-based read/write, making it portable across all Arduino platforms.
 *
 * Tools registered:
 *   - eeprom_read     — read bytes from EEPROM at a given address
 *   - eeprom_write    — write bytes to EEPROM at a given address
 *   - eeprom_read_int — read a 32-bit integer from EEPROM
 *   - eeprom_write_int— write a 32-bit integer to EEPROM
 *   - eeprom_read_string — read a null-terminated string from EEPROM
 *   - eeprom_write_string — write a string to EEPROM (null-terminated)
 *   - eeprom_clear    — fill EEPROM range (or all) with 0x00
 *   - eeprom_info     — EEPROM size and usage stats
 */

#ifndef MCPD_EEPROM_TOOL_H
#define MCPD_EEPROM_TOOL_H

#include "../MCPTool.h"
#include <EEPROM.h>

namespace mcpd {

/**
 * Register EEPROM tools on the server.
 *
 * @param server  MCPServer instance
 * @param size    EEPROM size in bytes (default 4096, ESP8266/ESP32 need EEPROM.begin(size))
 */
inline void addEEPROMTools(Server& server, uint16_t size = 4096) {
#if defined(ESP32) || defined(ESP8266)
    EEPROM.begin(size);
#endif

    server.addTool("eeprom_read", "Read bytes from EEPROM at a given address",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address (0-based)"},"length":{"type":"integer","description":"Number of bytes to read (1-256)","minimum":1,"maximum":256}},"required":["address","length"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            int len = args["length"];
            if (addr < 0 || addr >= size) {
                return R"=({"error":"Address out of range"})=";
            }
            if (len < 1 || len > 256) {
                return R"=({"error":"Length must be 1-256"})=";
            }
            if (addr + len > size) {
                return R"=({"error":"Read would exceed EEPROM size"})=";
            }

            String result = "{\"address\":" + String(addr) + ",\"length\":" + String(len) + ",\"hex\":\"";
            for (int i = 0; i < len; i++) {
                uint8_t b = EEPROM.read(addr + i);
                const char hexChars[] = "0123456789abcdef";
                result += hexChars[(b >> 4) & 0x0F];
                result += hexChars[b & 0x0F];
            }
            result += "\",\"bytes\":[";
            for (int i = 0; i < len; i++) {
                if (i > 0) result += ",";
                result += String(EEPROM.read(addr + i));
            }
            result += "]}";
            return result;
        });

    server.addTool("eeprom_write", "Write bytes to EEPROM at a given address",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address (0-based)"},"bytes":{"type":"array","items":{"type":"integer","minimum":0,"maximum":255},"description":"Array of byte values to write (0-255)"}},"required":["address","bytes"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            JsonArray bytes = args["bytes"];
            int len = bytes.size();
            if (addr < 0 || addr >= size) {
                return R"=({"error":"Address out of range"})=";
            }
            if (len < 1 || len > 256) {
                return R"=({"error":"Must write 1-256 bytes"})=";
            }
            if (addr + len > size) {
                return R"=({"error":"Write would exceed EEPROM size"})=";
            }

            int written = 0;
            for (int i = 0; i < len; i++) {
                uint8_t b = bytes[i].as<int>();
                EEPROM.write(addr + i, b);
                written++;
            }
#if defined(ESP32) || defined(ESP8266)
            EEPROM.commit();
#endif
            return "{\"written\":" + String(written) + ",\"address\":" + String(addr) + "}";
        });

    server.addTool("eeprom_read_int", "Read a 32-bit integer from EEPROM (little-endian)",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address (must have 4 bytes available)"}},"required":["address"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            if (addr < 0 || addr + 4 > size) {
                return R"=({"error":"Address out of range for 4-byte read"})=";
            }
            int32_t value = 0;
            for (int i = 0; i < 4; i++) {
                value |= ((int32_t)EEPROM.read(addr + i)) << (i * 8);
            }
            return "{\"address\":" + String(addr) + ",\"value\":" + String(value) + "}";
        });

    server.addTool("eeprom_write_int", "Write a 32-bit integer to EEPROM (little-endian)",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address (needs 4 bytes)"},"value":{"type":"integer","description":"Integer value to write"}},"required":["address","value"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            if (addr < 0 || addr + 4 > size) {
                return R"=({"error":"Address out of range for 4-byte write"})=";
            }
            int32_t value = args["value"];
            for (int i = 0; i < 4; i++) {
                EEPROM.write(addr + i, (uint8_t)(value >> (i * 8)));
            }
#if defined(ESP32) || defined(ESP8266)
            EEPROM.commit();
#endif
            return "{\"address\":" + String(addr) + ",\"value\":" + String(value) + "}";
        });

    server.addTool("eeprom_read_string", "Read a null-terminated string from EEPROM",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address"},"maxLength":{"type":"integer","description":"Max chars to read (default 128)","minimum":1,"maximum":512}},"required":["address"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            int maxLen = args["maxLength"] | 128;
            if (addr < 0 || addr >= size) {
                return R"=({"error":"Address out of range"})=";
            }
            if (maxLen > size - addr) maxLen = size - addr;

            String str = "";
            for (int i = 0; i < maxLen; i++) {
                uint8_t b = EEPROM.read(addr + i);
                if (b == 0) break;
                str += (char)b;
            }
            return "{\"address\":" + String(addr) + ",\"value\":\"" + str + "\",\"length\":" + String(str.length()) + "}";
        });

    server.addTool("eeprom_write_string", "Write a null-terminated string to EEPROM",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address"},"value":{"type":"string","description":"String to write (max 256 chars)"}},"required":["address","value"]})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"];
            const char* str = args["value"].as<const char*>();
            if (!str) return R"=({"error":"Missing value"})=";
            int len = strlen(str);
            if (len > 256) return R"=({"error":"String too long (max 256)"})=";
            if (addr < 0 || addr + len + 1 > size) {
                return R"=({"error":"Write would exceed EEPROM size"})=";
            }

            for (int i = 0; i <= len; i++) {  // includes null terminator
                EEPROM.write(addr + i, (uint8_t)str[i]);
            }
#if defined(ESP32) || defined(ESP8266)
            EEPROM.commit();
#endif
            return "{\"written\":" + String(len + 1) + ",\"address\":" + String(addr) + ",\"string\":\"" + String(str) + "\"}";
        });

    server.addTool("eeprom_clear", "Clear EEPROM range (fill with 0x00)",
        R"=({"type":"object","properties":{"address":{"type":"integer","description":"Start address (default 0)"},"length":{"type":"integer","description":"Bytes to clear (default: entire EEPROM)"}}})=",
        [size](const JsonObject& args) -> String {
            int addr = args["address"] | 0;
            int len = args["length"] | (int)size;
            if (addr < 0 || addr >= size) {
                return R"=({"error":"Address out of range"})=";
            }
            if (addr + len > size) len = size - addr;

            for (int i = 0; i < len; i++) {
                EEPROM.write(addr + i, 0);
            }
#if defined(ESP32) || defined(ESP8266)
            EEPROM.commit();
#endif
            return "{\"cleared\":" + String(len) + ",\"from\":" + String(addr) + ",\"to\":" + String(addr + len - 1) + "}";
        });

    server.addTool("eeprom_info", "Get EEPROM size and usage statistics",
        R"=({"type":"object","properties":{}})=",
        [size](const JsonObject& args) -> String {
            (void)args;
            int nonZero = 0;
            for (int i = 0; i < size; i++) {
                if (EEPROM.read(i) != 0) nonZero++;
            }
            return "{\"size\":" + String(size) +
                   ",\"used\":" + String(nonZero) +
                   ",\"free\":" + String(size - nonZero) +
                   ",\"usagePercent\":" + String((float)nonZero / size * 100.0, 1) + "}";
        });
}

}  // namespace mcpd

#endif  // MCPD_EEPROM_TOOL_H
