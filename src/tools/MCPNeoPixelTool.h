/**
 * mcpd — Built-in NeoPixel Tool
 *
 * Provides: neopixel_set, neopixel_fill, neopixel_clear, neopixel_brightness
 * Uses bit-banging via the RMT peripheral (ESP32) or manual timing.
 *
 * NOTE: This tool requires the Adafruit NeoPixel library.
 *       Add to lib_deps: adafruit/Adafruit NeoPixel@^1.12
 */

#ifndef MCPD_NEOPIXEL_TOOL_H
#define MCPD_NEOPIXEL_TOOL_H

#include "../mcpd.h"
#include <Adafruit_NeoPixel.h>

namespace mcpd {
namespace tools {

class NeoPixelTool {
public:
    /**
     * Attach NeoPixel tools to the server.
     * @param server  MCP server instance
     * @param strip   Reference to an initialized Adafruit_NeoPixel strip
     */
    static void attach(Server& server, Adafruit_NeoPixel& strip) {
        // neopixel_set — set a single pixel
        server.addTool("neopixel_set",
            "Set a single NeoPixel LED to a specific color",
            R"({"type":"object","properties":{"index":{"type":"integer","description":"Pixel index (0-based)"},"r":{"type":"integer","description":"Red (0-255)"},"g":{"type":"integer","description":"Green (0-255)"},"b":{"type":"integer","description":"Blue (0-255)"},"show":{"type":"boolean","description":"Update strip immediately (default: true)","default":true}},"required":["index","r","g","b"]})",
            [&strip](const JsonObject& args) -> String {
                int idx = args["index"];
                int r = args["r"], g = args["g"], b = args["b"];
                bool show = args["show"] | true;

                if (idx < 0 || idx >= (int)strip.numPixels()) {
                    return String("{\"error\":\"Index out of range\",\"maxIndex\":") +
                           (strip.numPixels() - 1) + "}";
                }

                strip.setPixelColor(idx, strip.Color(r, g, b));
                if (show) strip.show();

                JsonDocument doc;
                doc["index"] = idx;
                doc["r"] = r; doc["g"] = g; doc["b"] = b;
                String result; serializeJson(doc, result); return result;
            });

        // neopixel_fill — fill all pixels with one color
        server.addTool("neopixel_fill",
            "Fill all NeoPixel LEDs with a single color",
            R"({"type":"object","properties":{"r":{"type":"integer","description":"Red (0-255)"},"g":{"type":"integer","description":"Green (0-255)"},"b":{"type":"integer","description":"Blue (0-255)"}},"required":["r","g","b"]})",
            [&strip](const JsonObject& args) -> String {
                int r = args["r"], g = args["g"], b = args["b"];
                strip.fill(strip.Color(r, g, b));
                strip.show();

                JsonDocument doc;
                doc["pixels"] = strip.numPixels();
                doc["r"] = r; doc["g"] = g; doc["b"] = b;
                String result; serializeJson(doc, result); return result;
            });

        // neopixel_clear — turn off all pixels
        server.addTool("neopixel_clear",
            "Turn off all NeoPixel LEDs",
            R"({"type":"object","properties":{}})",
            [&strip](const JsonObject& args) -> String {
                strip.clear();
                strip.show();
                return String("{\"cleared\":true,\"pixels\":") + strip.numPixels() + "}";
            });

        // neopixel_brightness — set global brightness
        server.addTool("neopixel_brightness",
            "Set the global NeoPixel brightness",
            R"({"type":"object","properties":{"brightness":{"type":"integer","description":"Brightness (0-255)"}},"required":["brightness"]})",
            [&strip](const JsonObject& args) -> String {
                int brightness = args["brightness"];
                if (brightness < 0) brightness = 0;
                if (brightness > 255) brightness = 255;
                strip.setBrightness(brightness);
                strip.show();
                return String("{\"brightness\":") + brightness + "}";
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_NEOPIXEL_TOOL_H
