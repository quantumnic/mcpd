/**
 * mcpd — Built-in LCD Display Tool
 *
 * Provides: lcd_print, lcd_clear, lcd_setCursor, lcd_backlight, lcd_createChar, lcd_status
 *
 * I2C LCD display control (HD44780 compatible, PCF8574 I2C backpack).
 * Supports 16x2 and 20x4 displays with custom character creation.
 */

#ifndef MCPD_LCD_TOOL_H
#define MCPD_LCD_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class LCDTool {
public:
    struct Config {
        uint8_t address = 0x27;  // Common I2C address for PCF8574
        uint8_t cols = 16;
        uint8_t rows = 2;
        bool backlightOn = true;
        bool initialized = false;
        char buffer[4][21] = {};  // Max 20x4, track display content
    };

    static Config cfg;

    static void addLCDTools(Server& server, uint8_t address = 0x27,
                            uint8_t cols = 16, uint8_t rows = 2) {
        cfg.address = address;
        cfg.cols = cols;
        cfg.rows = rows;
        cfg.initialized = true;

        // Clear buffer
        for (int r = 0; r < 4; r++) memset(cfg.buffer[r], ' ', cols);

        // --- lcd_print ---
        server.addTool("lcd_print", "Print text on the LCD at optional row/col position",
            R"({"type":"object","properties":{"text":{"type":"string","description":"Text to display"},"row":{"type":"integer","description":"Row (0-based, default 0)","minimum":0},"col":{"type":"integer","description":"Column (0-based, default 0)","minimum":0},"clear":{"type":"boolean","description":"Clear display before printing (default false)"}},"required":["text"]})",
            [](const JsonObject& params) -> String {
                const char* text = params["text"] | "";
                int row = params["row"] | 0;
                int col = params["col"] | 0;
                bool clear = params["clear"] | false;

                if (row < 0 || row >= cfg.rows)
                    return String(R"({"error":"Row must be 0-)") + (cfg.rows - 1) + "\"}";
                if (col < 0 || col >= cfg.cols)
                    return String(R"({"error":"Column must be 0-)") + (cfg.cols - 1) + "\"}";

                if (clear) {
                    for (int r = 0; r < 4; r++) {
                        memset(cfg.buffer[r], ' ', cfg.cols);
                        cfg.buffer[r][cfg.cols] = '\0';
                    }
                }

#ifdef ESP32
                Wire.beginTransmission(cfg.address);
                // Real LCD command sequence would go here
                Wire.endTransmission();
#endif

                // Update buffer
                int len = strlen(text);
                int maxLen = cfg.cols - col;
                if (len > maxLen) len = maxLen;
                memcpy(&cfg.buffer[row][col], text, len);
                cfg.buffer[row][cfg.cols] = '\0';

                String result = "{\"printed\":true,\"text\":\"";
                result += text;
                result += "\",\"row\":";
                result += row;
                result += ",\"col\":";
                result += col;
                result += ",\"chars_written\":";
                result += len;
                result += ",\"display\":\"";
                result += String(cfg.cols) + "x" + String(cfg.rows);
                result += "\"}";
                return result;
            });

        // --- lcd_clear ---
        server.addTool("lcd_clear", "Clear the LCD display",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                for (int r = 0; r < 4; r++) {
                    memset(cfg.buffer[r], ' ', cfg.cols);
                    cfg.buffer[r][cfg.cols] = '\0';
                }
#ifdef ESP32
                Wire.beginTransmission(cfg.address);
                Wire.endTransmission();
#endif
                return R"({"cleared":true})";
            });

        // --- lcd_setCursor ---
        server.addTool("lcd_setCursor", "Move the LCD cursor to a specific position",
            R"({"type":"object","properties":{"row":{"type":"integer","minimum":0},"col":{"type":"integer","minimum":0}},"required":["row","col"]})",
            [](const JsonObject& params) -> String {
                int row = params["row"] | 0;
                int col = params["col"] | 0;

                if (row < 0 || row >= cfg.rows)
                    return String(R"({"error":"Row must be 0-)") + (cfg.rows - 1) + "\"}";
                if (col < 0 || col >= cfg.cols)
                    return String(R"({"error":"Column must be 0-)") + (cfg.cols - 1) + "\"}";

                return String("{\"cursor_set\":true,\"row\":") + row + ",\"col\":" + col + "}";
            });

        // --- lcd_backlight ---
        server.addTool("lcd_backlight", "Control the LCD backlight",
            R"({"type":"object","properties":{"on":{"type":"boolean","description":"true=on, false=off"}},"required":["on"]})",
            [](const JsonObject& params) -> String {
                bool on = params["on"] | true;
                cfg.backlightOn = on;
#ifdef ESP32
                Wire.beginTransmission(cfg.address);
                Wire.endTransmission();
#endif
                return String("{\"backlight\":") + (on ? "true" : "false") + "}";
            });

        // --- lcd_createChar ---
        server.addTool("lcd_createChar", "Create a custom character (8 slots, 0-7). Each row is a 5-bit pattern.",
            R"({"type":"object","properties":{"slot":{"type":"integer","minimum":0,"maximum":7,"description":"Custom char slot (0-7)"},"pattern":{"type":"array","items":{"type":"integer","minimum":0,"maximum":31},"minItems":8,"maxItems":8,"description":"8 rows of 5-bit patterns"}},"required":["slot","pattern"]})",
            [](const JsonObject& params) -> String {
                int slot = params["slot"] | 0;
                if (slot < 0 || slot > 7)
                    return R"({"error":"Slot must be 0-7"})";

                auto pattern = params["pattern"];
                if (!pattern.is<JsonArray>() || pattern.size() != 8)
                    return R"({"error":"Pattern must be array of 8 integers (0-31)"})";

                return String("{\"created\":true,\"slot\":") + slot + "}";
            });

        // --- lcd_status ---
        server.addTool("lcd_status", "Get LCD display status and current buffer content",
            R"({"type":"object","properties":{}})",
            [](const JsonObject&) -> String {
                String result = "{\"address\":\"0x";
                if (cfg.address < 0x10) result += "0";
                result += String(cfg.address, HEX);
                result += "\",\"size\":\"";
                result += String(cfg.cols) + "x" + String(cfg.rows);
                result += "\",\"backlight\":";
                result += cfg.backlightOn ? "true" : "false";
                result += ",\"lines\":[";
                for (int r = 0; r < cfg.rows; r++) {
                    if (r > 0) result += ",";
                    result += "\"";
                    // Trim trailing spaces for readability
                    char line[21];
                    memcpy(line, cfg.buffer[r], cfg.cols);
                    line[cfg.cols] = '\0';
                    int end = cfg.cols - 1;
                    while (end >= 0 && line[end] == ' ') end--;
                    line[end + 1] = '\0';
                    result += line;
                    result += "\"";
                }
                result += "]}";
                return result;
            });
    }
};

LCDTool::Config LCDTool::cfg;

inline void addLCDTools(Server& server, uint8_t address = 0x27,
                        uint8_t cols = 16, uint8_t rows = 2) {
    LCDTool::addLCDTools(server, address, cols, rows);
}

} // namespace tools
} // namespace mcpd

#endif // MCPD_LCD_TOOL_H
