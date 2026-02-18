/**
 * mcpd — Built-in SPI Tools
 *
 * Provides: spi_transfer, spi_config
 * Allows AI to communicate with SPI devices (displays, flash, ADCs, etc.)
 */

#ifndef MCPD_SPI_TOOL_H
#define MCPD_SPI_TOOL_H

#include "../mcpd.h"
#include <SPI.h>

namespace mcpd {
namespace tools {

class SPITool {
public:
    /**
     * Attach SPI tools to the MCP server.
     *
     * @param server  The MCP server instance
     * @param spi     SPI bus instance (default: SPI)
     */
    static void attach(Server& server, SPIClass& spi = SPI) {
        // spi_transfer — send and receive bytes over SPI
        server.addTool("spi_transfer",
            "Transfer bytes over SPI bus. Sends data and returns received bytes simultaneously. "
            "Use csPin to automatically assert/deassert chip select.",
            R"({
                "type": "object",
                "properties": {
                    "bytes": {
                        "type": "array",
                        "items": {"type": "integer", "minimum": 0, "maximum": 255},
                        "description": "Bytes to send (0x00 to read without sending)"
                    },
                    "csPin": {
                        "type": "integer",
                        "description": "Chip select GPIO pin (auto LOW before, HIGH after)"
                    },
                    "frequency": {
                        "type": "integer",
                        "description": "SPI clock frequency in Hz (default: 1000000)",
                        "default": 1000000
                    },
                    "mode": {
                        "type": "integer",
                        "description": "SPI mode 0-3 (default: 0)",
                        "minimum": 0,
                        "maximum": 3,
                        "default": 0
                    },
                    "bitOrder": {
                        "type": "string",
                        "enum": ["MSB", "LSB"],
                        "description": "Bit order (default: MSB)",
                        "default": "MSB"
                    }
                },
                "required": ["bytes"]
            })",
            [&spi](const JsonObject& args) -> String {
                JsonArray txBytes = args["bytes"];
                int csPin = args["csPin"] | -1;
                uint32_t freq = args["frequency"] | 1000000;
                uint8_t mode = args["mode"] | 0;
                String bitOrderStr = args["bitOrder"] | "MSB";

                uint8_t bitOrder = (bitOrderStr == "LSB") ? LSBFIRST : MSBFIRST;

                // Limit transfer size for MCU memory
                size_t count = txBytes.size();
                if (count > 256) {
                    return R"({"error":"Transfer too large (max 256 bytes)"})";
                }

                // Configure and begin
                spi.beginTransaction(SPISettings(freq, bitOrder, mode));

                if (csPin >= 0) {
                    ::pinMode(csPin, OUTPUT);
                    ::digitalWrite(csPin, LOW);
                }

                JsonDocument doc;
                JsonArray rxBytes = doc["received"].to<JsonArray>();
                doc["sent"] = count;

                for (size_t i = 0; i < count; i++) {
                    uint8_t tx = txBytes[i].as<uint8_t>();
                    uint8_t rx = spi.transfer(tx);
                    rxBytes.add(rx);
                }

                if (csPin >= 0) {
                    ::digitalWrite(csPin, HIGH);
                }

                spi.endTransaction();

                doc["csPin"] = csPin;
                doc["frequency"] = freq;
                doc["mode"] = mode;

                String result;
                serializeJson(doc, result);
                return result;
            });

        // spi_config — configure SPI bus parameters
        server.addTool("spi_config",
            "Initialize and configure the SPI bus. Call before spi_transfer if using non-default pins.",
            R"({
                "type": "object",
                "properties": {
                    "frequency": {
                        "type": "integer",
                        "description": "Default SPI clock frequency in Hz",
                        "default": 1000000
                    },
                    "sckPin": {
                        "type": "integer",
                        "description": "Clock pin (platform-specific, omit for default)"
                    },
                    "mosiPin": {
                        "type": "integer",
                        "description": "MOSI pin (platform-specific, omit for default)"
                    },
                    "misoPin": {
                        "type": "integer",
                        "description": "MISO pin (platform-specific, omit for default)"
                    }
                }
            })",
            [&spi](const JsonObject& args) -> String {
                uint32_t freq = args["frequency"] | 1000000;

                // Some platforms support custom pins
#if defined(ESP32)
                int sck = args["sckPin"] | -1;
                int mosi = args["mosiPin"] | -1;
                int miso = args["misoPin"] | -1;
                if (sck >= 0 && mosi >= 0 && miso >= 0) {
                    spi.begin(sck, miso, mosi);
                } else {
                    spi.begin();
                }
#else
                spi.begin();
#endif
                (void)freq;

                JsonDocument doc;
                doc["status"] = "configured";
                doc["frequency"] = freq;

                String result;
                serializeJson(doc, result);
                return result;
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_SPI_TOOL_H
