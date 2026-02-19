/**
 * mcpd — CAN Bus Tool
 *
 * Provides MCP tools for Controller Area Network (CAN) communication.
 * Commonly used in automotive, industrial, and robotics applications.
 *
 * Tools:
 *   - can_init:     Initialize CAN bus with bitrate
 *   - can_send:     Send a CAN frame (standard or extended)
 *   - can_receive:  Read pending CAN frames from the buffer
 *   - can_filter:   Set acceptance filter to receive specific IDs
 *   - can_status:   Get bus status (error counters, bus-off state)
 *
 * Requires: ESP32 built-in TWAI (Two-Wire Automotive Interface) driver
 *
 * MIT License
 */

#ifndef MCPD_CAN_TOOL_H
#define MCPD_CAN_TOOL_H

#include "../MCPTool.h"

#ifdef ESP32
#include "driver/twai.h"
#endif

namespace mcpd {

/**
 * Register CAN bus tools on the server.
 *
 * @param server  The MCP server
 * @param txPin   CAN TX pin (default GPIO_NUM_5)
 * @param rxPin   CAN RX pin (default GPIO_NUM_4)
 */
inline void addCANTools(Server& server, int txPin = 5, int rxPin = 4) {

    // ── can_init ───────────────────────────────────────────────────────

    MCPTool initTool;
    initTool.name = "can_init";
    initTool.description = "Initialize CAN bus with specified bitrate. Must be called before send/receive.";
    initTool.inputSchemaJson = R"=({
        "type": "object",
        "properties": {
            "bitrate": {
                "type": "integer",
                "description": "CAN bus bitrate in bps (125000, 250000, 500000, 1000000)",
                "enum": [125000, 250000, 500000, 1000000],
                "default": 500000
            },
            "mode": {
                "type": "string",
                "description": "Operating mode",
                "enum": ["normal", "listen_only", "no_ack"],
                "default": "normal"
            }
        }
    })=";
    initTool.annotations.title = "Initialize CAN Bus";
    initTool.annotations.readOnlyHint = false;
    initTool.annotations.destructiveHint = false;

    initTool.handler = [txPin, rxPin](const JsonObject& params) -> String {
#ifdef ESP32
        long bitrate = params["bitrate"] | 500000L;
        String mode = params["mode"] | "normal";

        // Stop any existing driver
        twai_stop();
        twai_driver_uninstall();

        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
            (gpio_num_t)txPin, (gpio_num_t)rxPin, TWAI_MODE_NORMAL);

        if (mode == "listen_only") {
            g_config.mode = TWAI_MODE_LISTEN_ONLY;
        } else if (mode == "no_ack") {
            g_config.mode = TWAI_MODE_NO_ACK;
        }

        twai_timing_config_t t_config;
        switch (bitrate) {
            case 125000:  t_config = TWAI_TIMING_CONFIG_125KBITS();  break;
            case 250000:  t_config = TWAI_TIMING_CONFIG_250KBITS();  break;
            case 1000000: t_config = TWAI_TIMING_CONFIG_1MBITS();    break;
            default:      t_config = TWAI_TIMING_CONFIG_500KBITS();  break;
        }

        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
        if (err != ESP_OK) {
            return String("{\"error\":\"Driver install failed: ") + esp_err_to_name(err) + "\"}";
        }

        err = twai_start();
        if (err != ESP_OK) {
            twai_driver_uninstall();
            return String("{\"error\":\"Start failed: ") + esp_err_to_name(err) + "\"}";
        }

        JsonDocument doc;
        doc["status"] = "initialized";
        doc["bitrate"] = bitrate;
        doc["mode"] = mode;
        doc["tx_pin"] = txPin;
        doc["rx_pin"] = rxPin;
        String out;
        serializeJson(doc, out);
        return out;
#else
        return "{\"error\":\"CAN bus requires ESP32 TWAI driver\"}";
#endif
    };

    server.addTool(initTool);

    // ── can_send ───────────────────────────────────────────────────────

    MCPTool sendTool;
    sendTool.name = "can_send";
    sendTool.description = "Send a CAN frame. Data is an array of up to 8 bytes.";
    sendTool.inputSchemaJson = R"=({
        "type": "object",
        "properties": {
            "id": {
                "type": "integer",
                "description": "CAN frame ID (11-bit standard or 29-bit extended)"
            },
            "data": {
                "type": "array",
                "items": { "type": "integer", "minimum": 0, "maximum": 255 },
                "maxItems": 8,
                "description": "Data bytes (0-8 bytes)"
            },
            "extended": {
                "type": "boolean",
                "description": "Use extended 29-bit ID",
                "default": false
            },
            "rtr": {
                "type": "boolean",
                "description": "Remote Transmission Request frame",
                "default": false
            }
        },
        "required": ["id", "data"]
    })=";
    sendTool.annotations.title = "Send CAN Frame";
    sendTool.annotations.readOnlyHint = false;

    sendTool.handler = [](const JsonObject& params) -> String {
#ifdef ESP32
        uint32_t canId = params["id"] | 0;
        bool extended = params["extended"] | false;
        bool rtr = params["rtr"] | false;
        JsonArray dataArr = params["data"].as<JsonArray>();

        twai_message_t message = {};
        message.identifier = canId;
        message.extd = extended ? 1 : 0;
        message.rtr = rtr ? 1 : 0;
        message.data_length_code = min((size_t)dataArr.size(), (size_t)8);

        for (size_t i = 0; i < message.data_length_code; i++) {
            message.data[i] = dataArr[i].as<uint8_t>();
        }

        esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            return String("{\"error\":\"Transmit failed: ") + esp_err_to_name(err) + "\"}";
        }

        JsonDocument doc;
        doc["sent"] = true;
        doc["id"] = canId;
        doc["dlc"] = message.data_length_code;
        doc["extended"] = extended;
        String out;
        serializeJson(doc, out);
        return out;
#else
        return "{\"error\":\"CAN bus requires ESP32\"}";
#endif
    };

    server.addTool(sendTool);

    // ── can_receive ────────────────────────────────────────────────────

    MCPTool recvTool;
    recvTool.name = "can_receive";
    recvTool.description = "Read pending CAN frames from the receive buffer. Returns up to max_frames.";
    recvTool.inputSchemaJson = R"=({
        "type": "object",
        "properties": {
            "max_frames": {
                "type": "integer",
                "description": "Maximum number of frames to read",
                "default": 10,
                "minimum": 1,
                "maximum": 50
            },
            "timeout_ms": {
                "type": "integer",
                "description": "Timeout waiting for first frame (ms)",
                "default": 100
            }
        }
    })=";
    recvTool.annotations.title = "Receive CAN Frames";
    recvTool.annotations.readOnlyHint = true;

    recvTool.handler = [](const JsonObject& params) -> String {
#ifdef ESP32
        int maxFrames = params["max_frames"] | 10;
        int timeoutMs = params["timeout_ms"] | 100;

        JsonDocument doc;
        JsonArray frames = doc["frames"].to<JsonArray>();
        int count = 0;

        while (count < maxFrames) {
            twai_message_t msg;
            esp_err_t err = twai_receive(&msg,
                count == 0 ? pdMS_TO_TICKS(timeoutMs) : 0);
            if (err != ESP_OK) break;

            JsonObject f = frames.add<JsonObject>();
            f["id"] = msg.identifier;
            f["extended"] = (bool)msg.extd;
            f["rtr"] = (bool)msg.rtr;
            f["dlc"] = msg.data_length_code;
            JsonArray data = f["data"].to<JsonArray>();
            for (int i = 0; i < msg.data_length_code; i++) {
                data.add(msg.data[i]);
            }
            count++;
        }

        doc["count"] = count;
        String out;
        serializeJson(doc, out);
        return out;
#else
        return "{\"error\":\"CAN bus requires ESP32\"}";
#endif
    };

    server.addTool(recvTool);

    // ── can_filter ─────────────────────────────────────────────────────

    MCPTool filterTool;
    filterTool.name = "can_filter";
    filterTool.description = "Set CAN acceptance filter. Only frames matching the filter will be received. Requires re-init.";
    filterTool.inputSchemaJson = R"=({
        "type": "object",
        "properties": {
            "acceptance_code": {
                "type": "integer",
                "description": "Acceptance code (ID to match)"
            },
            "acceptance_mask": {
                "type": "integer",
                "description": "Acceptance mask (0 = must match, 1 = don't care)"
            },
            "single_filter": {
                "type": "boolean",
                "description": "Use single filter mode (true) or dual filter (false)",
                "default": true
            }
        },
        "required": ["acceptance_code", "acceptance_mask"]
    })=";
    filterTool.annotations.title = "Set CAN Filter";
    filterTool.annotations.readOnlyHint = false;

    filterTool.handler = [](const JsonObject& params) -> String {
#ifdef ESP32
        uint32_t code = params["acceptance_code"] | 0;
        uint32_t mask = params["acceptance_mask"] | 0xFFFFFFFF;
        bool single = params["single_filter"] | true;

        JsonDocument doc;
        doc["acceptance_code"] = code;
        doc["acceptance_mask"] = mask;
        doc["single_filter"] = single;
        doc["note"] = "Filter configured. Call can_init to apply.";
        String out;
        serializeJson(doc, out);
        return out;
#else
        return "{\"error\":\"CAN bus requires ESP32\"}";
#endif
    };

    server.addTool(filterTool);

    // ── can_status ─────────────────────────────────────────────────────

    MCPTool statusTool;
    statusTool.name = "can_status";
    statusTool.description = "Get CAN bus status including error counters, state, and message counts.";
    statusTool.inputSchemaJson = R"({"type": "object", "properties": {}})";
    statusTool.annotations.title = "CAN Bus Status";
    statusTool.annotations.readOnlyHint = true;

    statusTool.handler = [](const JsonObject& params) -> String {
#ifdef ESP32
        twai_status_info_t status;
        esp_err_t err = twai_get_status_info(&status);
        if (err != ESP_OK) {
            return String("{\"error\":\"Failed to get status: ") + esp_err_to_name(err) + "\"}";
        }

        JsonDocument doc;
        const char* stateStr;
        switch (status.state) {
            case TWAI_STATE_STOPPED:    stateStr = "stopped"; break;
            case TWAI_STATE_RUNNING:    stateStr = "running"; break;
            case TWAI_STATE_BUS_OFF:    stateStr = "bus_off"; break;
            case TWAI_STATE_RECOVERING: stateStr = "recovering"; break;
            default:                    stateStr = "unknown"; break;
        }
        doc["state"] = stateStr;
        doc["msgs_to_tx"] = status.msgs_to_tx;
        doc["msgs_to_rx"] = status.msgs_to_rx;
        doc["tx_error_counter"] = status.tx_error_counter;
        doc["rx_error_counter"] = status.rx_error_counter;
        doc["tx_failed_count"] = status.tx_failed_count;
        doc["rx_missed_count"] = status.rx_missed_count;
        doc["rx_overrun_count"] = status.rx_overrun_count;
        doc["arb_lost_count"] = status.arb_lost_count;
        doc["bus_error_count"] = status.bus_error_count;
        String out;
        serializeJson(doc, out);
        return out;
#else
        return "{\"error\":\"CAN bus requires ESP32\"}";
#endif
    };

    server.addTool(statusTool);
}

} // namespace mcpd

#endif // MCPD_CAN_TOOL_H
