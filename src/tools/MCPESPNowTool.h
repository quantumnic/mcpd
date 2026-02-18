/**
 * mcpd — ESP-NOW Peer-to-Peer Communication Tool
 *
 * Enables ESP-NOW mesh communication between microcontrollers.
 * AI can send/receive short messages to peer MCUs without WiFi infrastructure.
 *
 * ESP-NOW supports up to 250-byte payloads, 20 encrypted peers (or 6 unencrypted
 * in station mode). Range: ~200m line-of-sight at 1Mbps.
 *
 * Tools registered:
 *   - espnow_init      — initialize ESP-NOW subsystem
 *   - espnow_add_peer  — register a peer by MAC address
 *   - espnow_send      — send data to a peer (up to 250 bytes)
 *   - espnow_receive   — read received messages from buffer
 *   - espnow_peers     — list registered peers with delivery stats
 *   - espnow_broadcast — send to all peers (FF:FF:FF:FF:FF:FF)
 *
 * MIT License
 */

#ifndef MCPD_ESPNOW_TOOL_H
#define MCPD_ESPNOW_TOOL_H

#include "../MCPTool.h"

#ifdef ESP32
#include <esp_now.h>
#include <WiFi.h>
#endif

namespace mcpd {

namespace {

// ── Receive buffer ─────────────────────────────────────────────────
struct ESPNowMessage {
    uint8_t mac[6];
    uint8_t data[250];
    size_t len;
    unsigned long timestamp;
};

static const size_t ESPNOW_BUFFER_SIZE = 32;
static ESPNowMessage _espnowBuffer[ESPNOW_BUFFER_SIZE];
static volatile size_t _espnowBufferHead = 0;
static volatile size_t _espnowBufferCount = 0;
static bool _espnowInitialized = false;

// ── Delivery tracking ──────────────────────────────────────────────
struct ESPNowPeerStats {
    uint8_t mac[6];
    uint32_t sent;
    uint32_t delivered;
    uint32_t failed;
    unsigned long lastSendMs;
};

static std::vector<ESPNowPeerStats> _espnowPeerStats;

static String _macToString(const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static bool _parseMac(const char* str, uint8_t* mac) {
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6;
}

static ESPNowPeerStats* _findPeerStats(const uint8_t* mac) {
    for (auto& ps : _espnowPeerStats) {
        if (memcmp(ps.mac, mac, 6) == 0) return &ps;
    }
    return nullptr;
}

#ifdef ESP32
// ── ESP-NOW callbacks ──────────────────────────────────────────────
static void _espnowOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    if (len <= 0 || len > 250) return;
    size_t idx = (_espnowBufferHead + _espnowBufferCount) % ESPNOW_BUFFER_SIZE;
    if (_espnowBufferCount >= ESPNOW_BUFFER_SIZE) {
        // Overwrite oldest
        _espnowBufferHead = (_espnowBufferHead + 1) % ESPNOW_BUFFER_SIZE;
    } else {
        _espnowBufferCount++;
    }
    memcpy(_espnowBuffer[idx].mac, info->src_addr, 6);
    memcpy(_espnowBuffer[idx].data, data, len);
    _espnowBuffer[idx].len = len;
    _espnowBuffer[idx].timestamp = millis();
}

static void _espnowOnSend(const uint8_t* mac, esp_now_send_status_t status) {
    auto* ps = _findPeerStats(mac);
    if (ps) {
        if (status == ESP_NOW_SEND_SUCCESS) ps->delivered++;
        else ps->failed++;
    }
}
#endif

} // anonymous namespace

/**
 * Register ESP-NOW tools on the server.
 */
inline void addESPNowTools(Server& server) {

    // ── espnow_init ──────────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_init",
            "Initialize ESP-NOW subsystem. WiFi must be in STA or AP+STA mode. "
            "Call once before using other espnow_* tools.",
            R"j({"type":"object","properties":{"channel":{"type":"integer","minimum":0,"maximum":14,"description":"WiFi channel (0=current)","default":0}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (_espnowInitialized) {
                    return "{\"status\":\"already_initialized\"}";
                }

                // Ensure WiFi is in station mode
                if (WiFi.getMode() == WIFI_OFF) {
                    WiFi.mode(WIFI_STA);
                }

                int channel = args["channel"] | 0;
                if (channel > 0) {
                    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
                }

                esp_err_t err = esp_now_init();
                if (err != ESP_OK) {
                    return String("{\"error\":\"ESP-NOW init failed\",\"code\":\"0x") +
                           String(err, HEX) + "\"}";
                }

                esp_now_register_recv_cb(_espnowOnRecv);
                esp_now_register_send_cb(_espnowOnSend);

                _espnowInitialized = true;
                _espnowBufferHead = 0;
                _espnowBufferCount = 0;
                _espnowPeerStats.clear();

                String mac = WiFi.macAddress();
                return String("{\"status\":\"initialized\",\"mac\":\"") + mac +
                       "\",\"channel\":" + String(WiFi.channel()) + "}";
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        tool.markIdempotent();
        server.addTool(tool);
    }

    // ── espnow_add_peer ──────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_add_peer",
            "Register an ESP-NOW peer by MAC address. Required before sending directed messages.",
            R"j({"type":"object","properties":{"mac":{"type":"string","description":"Peer MAC address (AA:BB:CC:DD:EE:FF)"},"channel":{"type":"integer","minimum":0,"maximum":14,"default":0},"encrypt":{"type":"boolean","default":false}},"required":["mac"]})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_espnowInitialized) {
                    return "{\"error\":\"ESP-NOW not initialized. Call espnow_init first.\"}";
                }

                const char* macStr = args["mac"];
                if (!macStr) return "{\"error\":\"MAC address required\"}";

                uint8_t mac[6];
                if (!_parseMac(macStr, mac)) {
                    return "{\"error\":\"Invalid MAC format. Use AA:BB:CC:DD:EE:FF\"}";
                }

                esp_now_peer_info_t peerInfo = {};
                memcpy(peerInfo.peer_addr, mac, 6);
                peerInfo.channel = args["channel"] | 0;
                peerInfo.encrypt = args["encrypt"] | false;

                esp_err_t err = esp_now_add_peer(&peerInfo);
                if (err == ESP_ERR_ESPNOW_EXIST) {
                    return String("{\"status\":\"already_registered\",\"mac\":\"") + macStr + "\"}";
                }
                if (err != ESP_OK) {
                    return String("{\"error\":\"Failed to add peer\",\"code\":\"0x") +
                           String(err, HEX) + "\"}";
                }

                // Init stats
                ESPNowPeerStats ps = {};
                memcpy(ps.mac, mac, 6);
                _espnowPeerStats.push_back(ps);

                return String("{\"status\":\"added\",\"mac\":\"") + macStr + "\"}";
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        server.addTool(tool);
    }

    // ── espnow_send ──────────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_send",
            "Send data to an ESP-NOW peer. Max 250 bytes. Data is sent as UTF-8 text.",
            R"j({"type":"object","properties":{"mac":{"type":"string","description":"Peer MAC address"},"data":{"type":"string","description":"Text data to send (max 250 bytes)"}},"required":["mac","data"]})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_espnowInitialized) {
                    return "{\"error\":\"ESP-NOW not initialized\"}";
                }

                const char* macStr = args["mac"];
                const char* data = args["data"];
                if (!macStr || !data) return "{\"error\":\"mac and data required\"}";

                uint8_t mac[6];
                if (!_parseMac(macStr, mac)) {
                    return "{\"error\":\"Invalid MAC format\"}";
                }

                size_t len = strlen(data);
                if (len > 250) {
                    return "{\"error\":\"Data exceeds 250 byte limit\",\"size\":" + String(len) + "}";
                }

                auto* ps = _findPeerStats(mac);
                if (ps) { ps->sent++; ps->lastSendMs = millis(); }

                esp_err_t err = esp_now_send(mac, (const uint8_t*)data, len);
                if (err != ESP_OK) {
                    return String("{\"error\":\"Send failed\",\"code\":\"0x") +
                           String(err, HEX) + "\"}";
                }

                return String("{\"status\":\"sent\",\"mac\":\"") + macStr +
                       "\",\"bytes\":" + String(len) + "}";
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        server.addTool(tool);
    }

    // ── espnow_receive ───────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_receive",
            "Read received ESP-NOW messages from the buffer. Returns up to 'limit' messages "
            "(default 10). Messages are removed from buffer after reading.",
            R"j({"type":"object","properties":{"limit":{"type":"integer","minimum":1,"maximum":32,"default":10}}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_espnowInitialized) {
                    return "{\"error\":\"ESP-NOW not initialized\"}";
                }

                int limit = args["limit"] | 10;
                JsonDocument doc;
                JsonArray messages = doc["messages"].to<JsonArray>();

                int count = 0;
                while (_espnowBufferCount > 0 && count < limit) {
                    size_t idx = _espnowBufferHead;
                    ESPNowMessage& msg = _espnowBuffer[idx];

                    JsonObject m = messages.add<JsonObject>();
                    m["from"] = _macToString(msg.mac);
                    // Try to read as text
                    char textBuf[251];
                    memcpy(textBuf, msg.data, msg.len);
                    textBuf[msg.len] = '\0';
                    m["data"] = textBuf;
                    m["bytes"] = msg.len;
                    m["timestamp_ms"] = msg.timestamp;

                    _espnowBufferHead = (_espnowBufferHead + 1) % ESPNOW_BUFFER_SIZE;
                    _espnowBufferCount--;
                    count++;
                }

                doc["count"] = count;
                doc["remaining"] = (int)_espnowBufferCount;

                String result;
                serializeJson(doc, result);
                return result;
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        tool.markReadOnly();
        server.addTool(tool);
    }

    // ── espnow_peers ─────────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_peers",
            "List registered ESP-NOW peers with delivery statistics.",
            R"j({"type":"object","properties":{}})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                (void)args;
                if (!_espnowInitialized) {
                    return "{\"error\":\"ESP-NOW not initialized\"}";
                }

                JsonDocument doc;
                doc["peerCount"] = _espnowPeerStats.size();
                doc["bufferUsed"] = (int)_espnowBufferCount;
                doc["bufferCapacity"] = (int)ESPNOW_BUFFER_SIZE;

                JsonArray peers = doc["peers"].to<JsonArray>();
                for (const auto& ps : _espnowPeerStats) {
                    JsonObject p = peers.add<JsonObject>();
                    p["mac"] = _macToString(ps.mac);
                    p["sent"] = ps.sent;
                    p["delivered"] = ps.delivered;
                    p["failed"] = ps.failed;
                    float rate = ps.sent > 0 ? (float)ps.delivered / ps.sent * 100.0f : 0;
                    p["delivery_rate_pct"] = serialized(String(rate, 1));
                    p["last_send_ms"] = ps.lastSendMs;
                }

                String result;
                serializeJson(doc, result);
                return result;
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        tool.markReadOnly();
        server.addTool(tool);
    }

    // ── espnow_broadcast ─────────────────────────────────────────────
    {
        MCPTool tool(
            "espnow_broadcast",
            "Broadcast data to all ESP-NOW peers (FF:FF:FF:FF:FF:FF). Max 250 bytes.",
            R"j({"type":"object","properties":{"data":{"type":"string","description":"Text data to broadcast (max 250 bytes)"}},"required":["data"]})j",
            [](const JsonObject& args) -> String {
#ifdef ESP32
                if (!_espnowInitialized) {
                    return "{\"error\":\"ESP-NOW not initialized\"}";
                }

                const char* data = args["data"];
                if (!data) return "{\"error\":\"data required\"}";

                size_t len = strlen(data);
                if (len > 250) {
                    return "{\"error\":\"Data exceeds 250 byte limit\",\"size\":" + String(len) + "}";
                }

                // Broadcast address
                uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

                // Ensure broadcast peer exists
                esp_now_peer_info_t peerInfo = {};
                memcpy(peerInfo.peer_addr, broadcastMac, 6);
                peerInfo.channel = 0;
                peerInfo.encrypt = false;
                esp_now_add_peer(&peerInfo);  // Ignore if already exists

                esp_err_t err = esp_now_send(broadcastMac, (const uint8_t*)data, len);
                if (err != ESP_OK) {
                    return String("{\"error\":\"Broadcast failed\",\"code\":\"0x") +
                           String(err, HEX) + "\"}";
                }

                return String("{\"status\":\"broadcast\",\"bytes\":") + String(len) + "}";
#else
                (void)args;
                return "{\"error\":\"ESP-NOW only supported on ESP32\"}";
#endif
            });
        server.addTool(tool);
    }
}

} // namespace mcpd

#endif // MCPD_ESPNOW_TOOL_H
