/**
 * mcpd — Built-in UART (Serial Communication) Tool
 *
 * Provides: uart_config, uart_write, uart_read, uart_available
 *
 * Enables AI agents to communicate with serial peripherals (GPS modules,
 * RFID readers, sensor modules with UART interfaces, etc.).
 *
 * Uses HardwareSerial ports. On ESP32: Serial1, Serial2.
 * On RP2040: Serial1, Serial2 (UART0, UART1).
 */

#ifndef MCPD_UART_TOOL_H
#define MCPD_UART_TOOL_H

#include "../mcpd.h"

namespace mcpd {
namespace tools {

class UARTTool {
public:
    static void attach(Server& server) {
        // uart_config — initialize a UART port
        server.addTool("uart_config", "Configure a hardware UART port (Serial1 or Serial2)",
            R"({"type":"object","properties":{"port":{"type":"integer","enum":[1,2],"description":"UART port number (1=Serial1, 2=Serial2)"},"baud":{"type":"integer","description":"Baud rate (e.g. 9600, 115200)","enum":[300,1200,2400,4800,9600,19200,38400,57600,115200,230400,460800,921600]},"rxPin":{"type":"integer","description":"RX GPIO pin (optional, uses default if omitted)"},"txPin":{"type":"integer","description":"TX GPIO pin (optional, uses default if omitted)"}},"required":["port","baud"]})",
            [](const JsonObject& args) -> String {
                int port = args["port"];
                int baud = args["baud"];
                int rxPin = args["rxPin"] | -1;
                int txPin = args["txPin"] | -1;

                HardwareSerial* serial = nullptr;
                if (port == 1) serial = &Serial1;
#ifdef Serial2
                else if (port == 2) serial = &Serial2;
#endif
                else return "{\"error\":\"Invalid port. Use 1 or 2\"}";

                if (rxPin >= 0 && txPin >= 0) {
                    serial->begin(baud, SERIAL_8N1, rxPin, txPin);
                } else {
                    serial->begin(baud);
                }

                return String("{\"port\":") + port +
                       ",\"baud\":" + baud +
                       ",\"configured\":true}";
            });

        // uart_write — send data over UART
        server.addTool("uart_write", "Write data to a UART port",
            R"({"type":"object","properties":{"port":{"type":"integer","enum":[1,2],"description":"UART port number"},"data":{"type":"string","description":"String data to send"},"hex":{"type":"string","description":"Hex-encoded bytes to send (e.g. 'FF01A0'). Use instead of data for binary."},"newline":{"type":"boolean","description":"Append \\r\\n after data (default false)"}},"required":["port"]})",
            [](const JsonObject& args) -> String {
                int port = args["port"];
                HardwareSerial* serial = nullptr;
                if (port == 1) serial = &Serial1;
#ifdef Serial2
                else if (port == 2) serial = &Serial2;
#endif
                else return "{\"error\":\"Invalid port\"}";

                size_t written = 0;

                if (args["hex"].is<const char*>()) {
                    // Send hex-encoded binary data
                    String hex = args["hex"].as<const char*>();
                    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
                        char hi = hex[i], lo = hex[i + 1];
                        auto hexVal = [](char c) -> uint8_t {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                            if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                            return 0;
                        };
                        uint8_t byte = (hexVal(hi) << 4) | hexVal(lo);
                        serial->write(byte);
                        written++;
                    }
                } else if (args["data"].is<const char*>()) {
                    String data = args["data"].as<const char*>();
                    written = serial->print(data);
                    if (args["newline"] | false) {
                        serial->print("\r\n");
                        written += 2;
                    }
                } else {
                    return "{\"error\":\"Provide either 'data' or 'hex'\"}";
                }

                return String("{\"port\":") + port +
                       ",\"bytesWritten\":" + written + "}";
            });

        // uart_read — read available data from UART
        server.addTool("uart_read", "Read available data from a UART port",
            R"({"type":"object","properties":{"port":{"type":"integer","enum":[1,2],"description":"UART port number"},"maxBytes":{"type":"integer","description":"Maximum bytes to read (default 256, max 1024)","minimum":1,"maximum":1024},"timeout":{"type":"integer","description":"Read timeout in milliseconds (default 100)","minimum":0,"maximum":5000},"asHex":{"type":"boolean","description":"Return data as hex string instead of text (for binary data)"}},"required":["port"]})",
            [](const JsonObject& args) -> String {
                int port = args["port"];
                HardwareSerial* serial = nullptr;
                if (port == 1) serial = &Serial1;
#ifdef Serial2
                else if (port == 2) serial = &Serial2;
#endif
                else return "{\"error\":\"Invalid port\"}";

                int maxBytes = args["maxBytes"] | 256;
                if (maxBytes > 1024) maxBytes = 1024;
                int timeout = args["timeout"] | 100;
                bool asHex = args["asHex"] | false;

                serial->setTimeout(timeout);

                // Read available bytes
                String data = "";
                String hexData = "";
                int bytesRead = 0;

                unsigned long start = millis();
                while (bytesRead < maxBytes) {
                    if (serial->available()) {
                        int c = serial->read();
                        if (c < 0) break;
                        bytesRead++;
                        if (asHex) {
                            char buf[3];
                            snprintf(buf, sizeof(buf), "%02X", (uint8_t)c);
                            hexData += buf;
                        } else {
                            // For text mode, handle printable + common control chars
                            if (c >= 32 && c < 127) {
                                data += (char)c;
                            } else if (c == '\n') {
                                data += "\\n";
                            } else if (c == '\r') {
                                data += "\\r";
                            } else if (c == '\t') {
                                data += "\\t";
                            } else {
                                char buf[5];
                                snprintf(buf, sizeof(buf), "\\x%02X", (uint8_t)c);
                                data += buf;
                            }
                        }
                    } else if ((int)(millis() - start) > timeout) {
                        break;
                    } else {
                        delay(1);
                    }
                }

                String result = "{\"port\":" + String(port) +
                                ",\"bytesRead\":" + String(bytesRead);
                if (asHex) {
                    result += ",\"hex\":\"" + hexData + "\"";
                } else {
                    // Escape quotes in data for JSON safety
                    data.replace("\"", "\\\"");
                    result += ",\"data\":\"" + data + "\"";
                }
                result += "}";
                return result;
            });

        // uart_available — check how many bytes are waiting
        server.addTool("uart_available", "Check how many bytes are available to read on a UART port",
            R"({"type":"object","properties":{"port":{"type":"integer","enum":[1,2],"description":"UART port number"}},"required":["port"]})",
            [](const JsonObject& args) -> String {
                int port = args["port"];
                HardwareSerial* serial = nullptr;
                if (port == 1) serial = &Serial1;
#ifdef Serial2
                else if (port == 2) serial = &Serial2;
#endif
                else return "{\"error\":\"Invalid port\"}";

                int available = serial->available();
                return String("{\"port\":") + port +
                       ",\"available\":" + available + "}";
            });
    }
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_UART_TOOL_H
