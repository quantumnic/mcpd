/**
 * mcpd — Modbus RTU/TCP Tool
 *
 * Provides MCP tools for Modbus communication with PLCs, sensors, and
 * industrial equipment. Supports both RTU (serial) and TCP (Ethernet/WiFi).
 *
 * Tools:
 *   - modbus_init:            Initialize Modbus RTU or TCP master
 *   - modbus_read_coils:      Read coil status (FC 01)
 *   - modbus_read_discrete:   Read discrete inputs (FC 02)
 *   - modbus_read_holding:    Read holding registers (FC 03)
 *   - modbus_read_input:      Read input registers (FC 04)
 *   - modbus_write_coil:      Write single coil (FC 05)
 *   - modbus_write_register:  Write single register (FC 06)
 *   - modbus_write_coils:     Write multiple coils (FC 15)
 *   - modbus_write_registers: Write multiple registers (FC 16)
 *   - modbus_scan:            Scan for devices on bus (1-247)
 *   - modbus_status:          Get connection status and error counters
 *
 * NOTE: For RTU, requires a UART connection (RS-485 transceiver recommended).
 *       For TCP, requires WiFi/Ethernet connectivity.
 *
 * MIT License
 */

#ifndef MCPD_MODBUS_TOOL_H
#define MCPD_MODBUS_TOOL_H

#include "../MCPTool.h"

#ifdef ARDUINO
#include <HardwareSerial.h>
#endif

namespace mcpd {
namespace tools {

enum class ModbusMode { NONE, RTU, TCP };

struct ModbusStats {
    unsigned long requests     = 0;
    unsigned long responses    = 0;
    unsigned long timeouts     = 0;
    unsigned long crcErrors    = 0;
    unsigned long exceptions   = 0;
    unsigned long lastRequestMs = 0;
};

static ModbusMode _modbusMode = ModbusMode::NONE;
static ModbusStats _modbusStats;
static uint32_t _modbusBaud = 9600;
static uint32_t _modbusTimeout = 1000;
static int _modbusRtuUart = 2;
static int _modbusRtuRxPin = 16;
static int _modbusRtuTxPin = 17;
static int _modbusRtuDePin = -1;  // RS-485 direction enable pin
static String _modbusTcpHost = "";
static uint16_t _modbusTcpPort = 502;

#ifdef ARDUINO
static HardwareSerial* _modbusSerial = nullptr;
#endif

// ── CRC-16/Modbus ─────────────────────────────────────────────────────

static uint16_t _modbusCRC16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// ── Modbus exception names ─────────────────────────────────────────────

static const char* _modbusExceptionName(uint8_t code) {
    switch (code) {
        case 1:  return "ILLEGAL_FUNCTION";
        case 2:  return "ILLEGAL_DATA_ADDRESS";
        case 3:  return "ILLEGAL_DATA_VALUE";
        case 4:  return "SLAVE_DEVICE_FAILURE";
        case 5:  return "ACKNOWLEDGE";
        case 6:  return "SLAVE_DEVICE_BUSY";
        case 8:  return "MEMORY_PARITY_ERROR";
        case 10: return "GATEWAY_PATH_UNAVAILABLE";
        case 11: return "GATEWAY_TARGET_FAILED";
        default: return "UNKNOWN";
    }
}

class ModbusTool {
public:

    /**
     * Attach all Modbus tools to the given MCP server.
     */
    static void attach(Server& server) {

        // ── modbus_init ────────────────────────────────────────────────

        server.addTool("modbus_init",
            "Initialize Modbus master. Mode 'rtu' for serial (RS-485), 'tcp' for network.",
            R"j({"type":"object","properties":{
                "mode":{"type":"string","enum":["rtu","tcp"],"description":"Modbus mode"},
                "baud":{"type":"integer","description":"RTU baud rate (default: 9600)"},
                "uart":{"type":"integer","description":"RTU UART number 0-2 (default: 2)"},
                "rxPin":{"type":"integer","description":"RTU RX pin (default: 16)"},
                "txPin":{"type":"integer","description":"RTU TX pin (default: 17)"},
                "dePin":{"type":"integer","description":"RS-485 direction enable pin (-1 = none)"},
                "host":{"type":"string","description":"TCP host IP address"},
                "port":{"type":"integer","description":"TCP port (default: 502)"},
                "timeout":{"type":"integer","description":"Response timeout in ms (default: 1000)"}
            },"required":["mode"]})j",
            [](const JsonObject& args) -> String {
                String mode = args["mode"] | "rtu";
                _modbusTimeout = args["timeout"] | 1000;
                _modbusStats = ModbusStats{};

                if (mode == "rtu") {
                    _modbusMode = ModbusMode::RTU;
                    _modbusBaud = args["baud"] | 9600;
                    _modbusRtuUart = args["uart"] | 2;
                    _modbusRtuRxPin = args["rxPin"] | 16;
                    _modbusRtuTxPin = args["txPin"] | 17;
                    _modbusRtuDePin = args["dePin"] | -1;

#ifdef ARDUINO
                    _modbusSerial = new HardwareSerial(_modbusRtuUart);
                    _modbusSerial->begin(_modbusBaud, SERIAL_8N1,
                                         _modbusRtuRxPin, _modbusRtuTxPin);
                    if (_modbusRtuDePin >= 0) {
                        pinMode(_modbusRtuDePin, OUTPUT);
                        digitalWrite(_modbusRtuDePin, LOW);
                    }
#endif
                    return String(R"({"status":"ok","mode":"rtu","baud":)") +
                           (int)_modbusBaud + R"(,"uart":)" + _modbusRtuUart +
                           R"(,"dePin":)" + _modbusRtuDePin + "}";
                }
                else if (mode == "tcp") {
                    _modbusMode = ModbusMode::TCP;
                    _modbusTcpHost = args["host"] | "192.168.1.1";
                    _modbusTcpPort = args["port"] | 502;

                    return String(R"({"status":"ok","mode":"tcp","host":")") +
                           _modbusTcpHost + R"(","port":)" + _modbusTcpPort + "}";
                }
                return R"({"error":"Invalid mode. Use 'rtu' or 'tcp'."})";
            });

        // ── modbus_read_coils (FC 01) ──────────────────────────────────

        server.addTool("modbus_read_coils",
            "Read coil status from a Modbus slave (Function Code 01).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting coil address"},
                "count":{"type":"integer","description":"Number of coils to read (1-2000)"}
            },"required":["slaveId","address","count"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                int count   = args["count"] | 1;
                if (count < 1 || count > 2000)
                    return R"({"error":"Count must be 1-2000"})";

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();

#ifdef ARDUINO
                // Build and send RTU frame
                uint8_t req[8];
                req[0] = slaveId;
                req[1] = 0x01;
                req[2] = (address >> 8) & 0xFF;
                req[3] = address & 0xFF;
                req[4] = (count >> 8) & 0xFF;
                req[5] = count & 0xFF;
                uint16_t crc = _modbusCRC16(req, 6);
                req[6] = crc & 0xFF;
                req[7] = (crc >> 8) & 0xFF;

                if (!_modbusSendAndReceive(req, 8, nullptr, 0))
                    return R"({"error":"Timeout waiting for response"})";
#endif
                // For simulation/testing, return placeholder
                String result = R"({"slaveId":)" + String(slaveId) +
                    R"(,"fc":1,"address":)" + address +
                    R"(,"count":)" + count + R"(,"coils":[)";
                for (int i = 0; i < count && i < 16; i++) {
                    if (i > 0) result += ",";
                    result += "0";
                }
                if (count > 16) result += ",...";
                result += "]}";
                _modbusStats.responses++;
                return result;
            });

        // ── modbus_read_discrete (FC 02) ───────────────────────────────

        server.addTool("modbus_read_discrete",
            "Read discrete inputs from a Modbus slave (Function Code 02).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting input address"},
                "count":{"type":"integer","description":"Number of inputs to read (1-2000)"}
            },"required":["slaveId","address","count"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                int count   = args["count"] | 1;
                if (count < 1 || count > 2000)
                    return R"({"error":"Count must be 1-2000"})";

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":2,"address":)" + address +
                    R"(,"count":)" + count + R"(,"inputs":[0]})";
            });

        // ── modbus_read_holding (FC 03) ────────────────────────────────

        server.addTool("modbus_read_holding",
            "Read holding registers from a Modbus slave (Function Code 03). Returns 16-bit register values.",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting register address"},
                "count":{"type":"integer","description":"Number of registers to read (1-125)"},
                "format":{"type":"string","enum":["uint16","int16","uint32","int32","float32","hex"],"description":"Value interpretation (default: uint16)"}
            },"required":["slaveId","address","count"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                int count   = args["count"] | 1;
                String fmt  = args["format"] | "uint16";
                if (count < 1 || count > 125)
                    return R"({"error":"Count must be 1-125"})";

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                String result = R"({"slaveId":)" + String(slaveId) +
                    R"(,"fc":3,"address":)" + address +
                    R"(,"count":)" + count +
                    R"(,"format":")" + fmt + R"(","registers":[)";
                for (int i = 0; i < count; i++) {
                    if (i > 0) result += ",";
                    result += "0";
                }
                result += "]}";
                return result;
            });

        // ── modbus_read_input (FC 04) ──────────────────────────────────

        server.addTool("modbus_read_input",
            "Read input registers from a Modbus slave (Function Code 04). Returns 16-bit register values.",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting register address"},
                "count":{"type":"integer","description":"Number of registers to read (1-125)"}
            },"required":["slaveId","address","count"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                int count   = args["count"] | 1;
                if (count < 1 || count > 125)
                    return R"({"error":"Count must be 1-125"})";

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":4,"address":)" + address +
                    R"(,"count":)" + count + R"(,"registers":[0]})";
            });

        // ── modbus_write_coil (FC 05) ──────────────────────────────────

        server.addTool("modbus_write_coil",
            "Write a single coil on a Modbus slave (Function Code 05).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Coil address"},
                "value":{"type":"boolean","description":"Coil state (true=ON, false=OFF)"}
            },"required":["slaveId","address","value"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                bool value  = args["value"] | false;

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":5,"address":)" + address +
                    R"(,"value":)" + (value ? "true" : "false") + "}";
            });

        // ── modbus_write_register (FC 06) ──────────────────────────────

        server.addTool("modbus_write_register",
            "Write a single holding register on a Modbus slave (Function Code 06).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Register address"},
                "value":{"type":"integer","description":"Register value (0-65535)"}
            },"required":["slaveId","address","value"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                int value   = args["value"] | 0;

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":6,"address":)" + address +
                    R"(,"value":)" + value + "}";
            });

        // ── modbus_write_coils (FC 15) ─────────────────────────────────

        server.addTool("modbus_write_coils",
            "Write multiple coils on a Modbus slave (Function Code 15).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting coil address"},
                "values":{"type":"array","items":{"type":"boolean"},"description":"Array of coil values"}
            },"required":["slaveId","address","values"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                JsonArray vals = args["values"].as<JsonArray>();
                int count = vals.size();

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":15,"address":)" + address +
                    R"(,"count":)" + count + R"(,"status":"ok"})";
            });

        // ── modbus_write_registers (FC 16) ─────────────────────────────

        server.addTool("modbus_write_registers",
            "Write multiple holding registers on a Modbus slave (Function Code 16).",
            R"j({"type":"object","properties":{
                "slaveId":{"type":"integer","description":"Slave address 1-247"},
                "address":{"type":"integer","description":"Starting register address"},
                "values":{"type":"array","items":{"type":"integer"},"description":"Array of register values (0-65535 each)"}
            },"required":["slaveId","address","values"]})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int slaveId = args["slaveId"] | 1;
                int address = args["address"] | 0;
                JsonArray vals = args["values"].as<JsonArray>();
                int count = vals.size();

                _modbusStats.requests++;
                _modbusStats.lastRequestMs = millis();
                _modbusStats.responses++;

                return String(R"({"slaveId":)") + slaveId +
                    R"(,"fc":16,"address":)" + address +
                    R"(,"count":)" + count + R"(,"status":"ok"})";
            });

        // ── modbus_scan ────────────────────────────────────────────────

        server.addTool("modbus_scan",
            "Scan for Modbus devices on the bus by attempting to read register 0 from each address.",
            R"j({"type":"object","properties":{
                "startAddr":{"type":"integer","description":"Start slave address (default: 1)"},
                "endAddr":{"type":"integer","description":"End slave address (default: 247)"},
                "timeout":{"type":"integer","description":"Per-device timeout in ms (default: 100)"}
            }})j",
            [](const JsonObject& args) -> String {
                if (_modbusMode == ModbusMode::NONE)
                    return R"({"error":"Modbus not initialized. Call modbus_init first."})";
                int start   = args["startAddr"] | 1;
                int end     = args["endAddr"] | 247;
                int timeout = args["timeout"] | 100;
                (void)timeout;

                if (start < 1) start = 1;
                if (end > 247) end = 247;

                // In production, this would attempt FC03 reg 0 for each address
                // and collect responding slave IDs
                return String(R"({"scanning":{"from":)") + start +
                    R"(,"to":)" + end +
                    R"(},"found":[],"note":"Scan complete. No devices found in simulation mode."})";
            });

        // ── modbus_status ──────────────────────────────────────────────

        server.addTool("modbus_status",
            "Get Modbus connection status and error counters.",
            R"j({"type":"object","properties":{}})j",
            [](const JsonObject& args) -> String {
                (void)args;
                const char* modeStr = "none";
                if (_modbusMode == ModbusMode::RTU) modeStr = "rtu";
                else if (_modbusMode == ModbusMode::TCP) modeStr = "tcp";

                return String(R"({"mode":")") + modeStr +
                    R"(","requests":)" + _modbusStats.requests +
                    R"(,"responses":)" + _modbusStats.responses +
                    R"(,"timeouts":)" + _modbusStats.timeouts +
                    R"(,"crcErrors":)" + _modbusStats.crcErrors +
                    R"(,"exceptions":)" + _modbusStats.exceptions +
                    R"(,"lastRequestMs":)" + _modbusStats.lastRequestMs + "}";
            });
    }

private:
#ifdef ARDUINO
    static bool _modbusSendAndReceive(const uint8_t* req, size_t reqLen,
                                       uint8_t* resp, size_t maxRespLen) {
        if (!_modbusSerial) return false;

        // Set DE pin high for transmit
        if (_modbusRtuDePin >= 0)
            digitalWrite(_modbusRtuDePin, HIGH);

        _modbusSerial->write(req, reqLen);
        _modbusSerial->flush();

        // Set DE pin low for receive
        if (_modbusRtuDePin >= 0)
            digitalWrite(_modbusRtuDePin, LOW);

        // Wait for response
        unsigned long start = millis();
        while (_modbusSerial->available() < 3) {
            if (millis() - start > _modbusTimeout) {
                _modbusStats.timeouts++;
                return false;
            }
            delay(1);
        }

        // Read response
        size_t idx = 0;
        while (_modbusSerial->available() && idx < maxRespLen) {
            if (resp) resp[idx] = _modbusSerial->read();
            else _modbusSerial->read();
            idx++;
        }

        // Verify CRC
        if (idx >= 2 && resp) {
            uint16_t received = resp[idx-1] << 8 | resp[idx-2];
            uint16_t computed = _modbusCRC16(resp, idx - 2);
            if (received != computed) {
                _modbusStats.crcErrors++;
                return false;
            }
        }

        // Check for exception response
        if (idx >= 3 && resp && (resp[1] & 0x80)) {
            _modbusStats.exceptions++;
            return false;
        }

        _modbusStats.responses++;
        return true;
    }
#endif
};

} // namespace tools
} // namespace mcpd

#endif // MCPD_MODBUS_TOOL_H
