/**
 * mcpd — WebSocket Transport
 *
 * Provides WebSocket-based MCP transport as an alternative to HTTP+SSE.
 * Many MCP clients (e.g. Cline, Continue) prefer WebSocket connections.
 *
 * Protocol: JSON-RPC 2.0 messages over WebSocket frames.
 * Each frame contains a single JSON-RPC request/response/notification.
 *
 * Requires: ArduinoWebsockets or similar library.
 * This implementation uses a minimal built-in WebSocket server over WiFiServer.
 *
 * Usage:
 *   mcpd::WebSocketTransport ws(8080);
 *   ws.onMessage([](const String& msg) -> String { return processJsonRpc(msg); });
 *   ws.begin();
 *   ws.loop(); // in loop()
 */

#ifndef MCPD_TRANSPORT_WS_H
#define MCPD_TRANSPORT_WS_H

#include <Arduino.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <functional>
#include <vector>

namespace mcpd {

/**
 * Callback: receives JSON-RPC message, returns response (empty = no response).
 */
using WSMessageHandler = std::function<String(const String& message)>;

/**
 * Represents a connected WebSocket client.
 */
struct WSClient {
    WiFiClient tcp;
    bool handshakeDone = false;
    String buffer;
    unsigned long connectedAt = 0;
    unsigned long lastActivity = 0;

    bool connected() const { return tcp.connected(); }
};

/**
 * Minimal WebSocket server for MCP transport.
 *
 * Implements RFC 6455 basics: handshake, text frames, ping/pong, close.
 * Sufficient for MCP JSON-RPC communication.
 */
class WebSocketTransport {
public:
    static constexpr size_t MAX_WS_CLIENTS = 4;
    static constexpr unsigned long PING_INTERVAL_MS = 30000;
    static constexpr unsigned long TIMEOUT_MS = 300000; // 5 min idle

    explicit WebSocketTransport(uint16_t port = 8080)
        : _port(port), _server(nullptr) {}

    ~WebSocketTransport() {
        stop();
    }

    /** Set the message handler */
    void onMessage(WSMessageHandler handler) { _handler = handler; }

    /** Start the WebSocket server */
    void begin() {
        _server = new WiFiServer(_port);
        _server->begin();
        Serial.printf("[mcpd] WebSocket server started on port %d\n", _port);
    }

    /** Process connections and messages — call in loop() */
    void loop() {
        if (!_server) return;

        // Accept new connections
        WiFiClient newClient = _server->available();
        if (newClient) {
            if (_clients.size() < MAX_WS_CLIENTS) {
                WSClient ws;
                ws.tcp = newClient;
                ws.connectedAt = millis();
                ws.lastActivity = millis();
                _clients.push_back(ws);
            } else {
                newClient.stop();
            }
        }

        unsigned long now = millis();

        // Process existing clients
        for (auto it = _clients.begin(); it != _clients.end();) {
            if (!it->connected() || (now - it->lastActivity > TIMEOUT_MS)) {
                it = _clients.erase(it);
                continue;
            }

            if (!it->handshakeDone) {
                _handleHandshake(*it);
            } else {
                _handleFrames(*it);
            }

            // Send ping periodically
            if (it->handshakeDone && now - it->lastActivity > PING_INTERVAL_MS) {
                _sendPing(*it);
            }

            ++it;
        }
    }

    /** Send a message to all connected clients */
    void broadcast(const String& message) {
        for (auto& c : _clients) {
            if (c.handshakeDone && c.connected()) {
                _sendTextFrame(c, message);
            }
        }
    }

    /** Stop the WebSocket server */
    void stop() {
        for (auto& c : _clients) {
            c.tcp.stop();
        }
        _clients.clear();
        if (_server) {
            _server->stop();
            delete _server;
            _server = nullptr;
        }
    }

    /** Number of connected clients */
    size_t clientCount() const { return _clients.size(); }

    uint16_t port() const { return _port; }

private:
    uint16_t _port;
    WiFiServer* _server;
    std::vector<WSClient> _clients;
    WSMessageHandler _handler;

    /**
     * Handle the WebSocket upgrade handshake (HTTP → WS).
     */
    void _handleHandshake(WSClient& client) {
        if (!client.tcp.available()) return;

        String request = "";
        unsigned long start = millis();
        while (client.tcp.available() && millis() - start < 1000) {
            request += (char)client.tcp.read();
            if (request.endsWith("\r\n\r\n")) break;
        }

        // Find the Sec-WebSocket-Key
        int keyStart = request.indexOf("Sec-WebSocket-Key: ");
        if (keyStart < 0) {
            client.tcp.stop();
            return;
        }
        keyStart += 19;
        int keyEnd = request.indexOf("\r\n", keyStart);
        String key = request.substring(keyStart, keyEnd);

        // Compute accept hash: SHA1(key + magic) → base64
        String acceptKey = _computeAcceptKey(key);

        // Send upgrade response
        client.tcp.print(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + acceptKey + "\r\n"
            "\r\n"
        );

        client.handshakeDone = true;
        client.lastActivity = millis();
        Serial.println("[mcpd] WebSocket client connected");
    }

    /**
     * Read and process WebSocket frames.
     */
    void _handleFrames(WSClient& client) {
        if (!client.tcp.available()) return;
        client.lastActivity = millis();

        // Read frame header
        uint8_t b0 = client.tcp.read();
        uint8_t b1 = client.tcp.read();

        uint8_t opcode = b0 & 0x0F;
        bool masked = b1 & 0x80;
        uint64_t payloadLen = b1 & 0x7F;

        if (payloadLen == 126) {
            payloadLen = ((uint64_t)client.tcp.read() << 8) | client.tcp.read();
        } else if (payloadLen == 127) {
            payloadLen = 0;
            for (int i = 0; i < 8; i++) {
                payloadLen = (payloadLen << 8) | client.tcp.read();
            }
        }

        uint8_t mask[4] = {0};
        if (masked) {
            for (int i = 0; i < 4; i++) mask[i] = client.tcp.read();
        }

        // Read payload (limit to reasonable size for MCU)
        if (payloadLen > 16384) {
            client.tcp.stop();
            return;
        }

        String payload;
        payload.reserve(payloadLen);
        for (uint64_t i = 0; i < payloadLen; i++) {
            uint8_t byte = client.tcp.read();
            if (masked) byte ^= mask[i % 4];
            payload += (char)byte;
        }

        switch (opcode) {
            case 0x1: // Text frame
                if (_handler) {
                    String response = _handler(payload);
                    if (response.length() > 0) {
                        _sendTextFrame(client, response);
                    }
                }
                break;

            case 0x8: // Close
                _sendCloseFrame(client);
                client.tcp.stop();
                break;

            case 0x9: // Ping
                _sendPongFrame(client, payload);
                break;

            case 0xA: // Pong — just update activity
                break;
        }
    }

    void _sendTextFrame(WSClient& client, const String& data) {
        size_t len = data.length();

        // FIN + text opcode
        client.tcp.write((uint8_t)0x81);

        // Payload length (server→client: no mask)
        if (len < 126) {
            client.tcp.write((uint8_t)len);
        } else if (len < 65536) {
            client.tcp.write((uint8_t)126);
            client.tcp.write((uint8_t)(len >> 8));
            client.tcp.write((uint8_t)(len & 0xFF));
        } else {
            client.tcp.write((uint8_t)127);
            for (int i = 7; i >= 0; i--) {
                client.tcp.write((uint8_t)((len >> (i * 8)) & 0xFF));
            }
        }

        client.tcp.write((const uint8_t*)data.c_str(), len);
        client.tcp.flush();
    }

    void _sendPing(WSClient& client) {
        client.tcp.write((uint8_t)0x89); // FIN + ping
        client.tcp.write((uint8_t)0x00); // no payload
        client.tcp.flush();
    }

    void _sendPongFrame(WSClient& client, const String& data) {
        client.tcp.write((uint8_t)0x8A); // FIN + pong
        uint8_t len = data.length();
        client.tcp.write(len);
        if (len > 0) {
            client.tcp.write((const uint8_t*)data.c_str(), len);
        }
        client.tcp.flush();
    }

    void _sendCloseFrame(WSClient& client) {
        client.tcp.write((uint8_t)0x88); // FIN + close
        client.tcp.write((uint8_t)0x00);
        client.tcp.flush();
    }

    /**
     * Compute Sec-WebSocket-Accept from the client key.
     * Uses a minimal SHA-1 + Base64 implementation.
     */
    String _computeAcceptKey(const String& key) {
        // WebSocket magic GUID
        String combined = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

        // SHA-1 hash
        uint8_t hash[20];
        _sha1((const uint8_t*)combined.c_str(), combined.length(), hash);

        // Base64 encode
        return _base64Encode(hash, 20);
    }

    // Minimal SHA-1 implementation for WebSocket handshake
    static void _sha1(const uint8_t* data, size_t len, uint8_t* hash) {
        uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
                 h3 = 0x10325476, h4 = 0xC3D2E1F0;

        // Pad message
        size_t newLen = len + 1;
        while (newLen % 64 != 56) newLen++;
        uint8_t* msg = (uint8_t*)calloc(newLen + 8, 1);
        memcpy(msg, data, len);
        msg[len] = 0x80;
        uint64_t bitLen = (uint64_t)len * 8;
        for (int i = 0; i < 8; i++) {
            msg[newLen + i] = (bitLen >> (56 - i * 8)) & 0xFF;
        }

        // Process blocks
        for (size_t offset = 0; offset < newLen + 8; offset += 64) {
            uint32_t w[80];
            for (int i = 0; i < 16; i++) {
                w[i] = ((uint32_t)msg[offset + i*4] << 24) |
                        ((uint32_t)msg[offset + i*4+1] << 16) |
                        ((uint32_t)msg[offset + i*4+2] << 8) |
                        msg[offset + i*4+3];
            }
            for (int i = 16; i < 80; i++) {
                uint32_t t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
                w[i] = (t << 1) | (t >> 31);
            }

            uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
            for (int i = 0; i < 80; i++) {
                uint32_t f, k;
                if (i < 20)      { f = (b & c) | (~b & d); k = 0x5A827999; }
                else if (i < 40) { f = b ^ c ^ d;          k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else              { f = b ^ c ^ d;          k = 0xCA62C1D6; }
                uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
                e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
            }
            h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
        }
        free(msg);

        uint32_t h[] = {h0, h1, h2, h3, h4};
        for (int i = 0; i < 5; i++) {
            hash[i*4]   = (h[i] >> 24) & 0xFF;
            hash[i*4+1] = (h[i] >> 16) & 0xFF;
            hash[i*4+2] = (h[i] >> 8) & 0xFF;
            hash[i*4+3] = h[i] & 0xFF;
        }
    }

    static String _base64Encode(const uint8_t* data, size_t len) {
        static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String result;
        result.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3) {
            uint32_t n = ((uint32_t)data[i]) << 16;
            if (i + 1 < len) n |= ((uint32_t)data[i+1]) << 8;
            if (i + 2 < len) n |= data[i+2];
            result += chars[(n >> 18) & 0x3F];
            result += chars[(n >> 12) & 0x3F];
            result += (i + 1 < len) ? chars[(n >> 6) & 0x3F] : '=';
            result += (i + 2 < len) ? chars[n & 0x3F] : '=';
        }
        return result;
    }
};

} // namespace mcpd

#endif // MCPD_TRANSPORT_WS_H
