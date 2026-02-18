/**
 * MCPTransportBLE — BLE GATT transport for MCP on ESP32
 *
 * Exposes the MCP server over Bluetooth Low Energy using a custom GATT service.
 * Clients write JSON-RPC requests to the RX characteristic and receive responses
 * via notifications on the TX characteristic.
 *
 * BLE MTU is small (~512 bytes negotiated), so large messages are chunked
 * automatically with a simple framing protocol:
 *   - Each chunk is prefixed with a 1-byte header:
 *     0x00 = single complete message
 *     0x01 = first chunk (more follow)
 *     0x02 = continuation chunk
 *     0x03 = final chunk
 *   - Receiver reassembles before processing.
 *
 * Usage:
 *   server.enableBLE("my-device-ble");  // call before begin()
 *
 * Requires: ESP32 with BLE support (ESP-IDF BLE stack).
 */

#ifndef MCP_TRANSPORT_BLE_H
#define MCP_TRANSPORT_BLE_H

#ifdef ESP32

#include <Arduino.h>
#include <functional>
#include <vector>

// Forward-declare ESP32 BLE classes (user must include BLE libs)
class BLEServer;
class BLEService;
class BLECharacteristic;
class BLEServerCallbacks;
class BLECharacteristicCallbacks;
class BLEAdvertising;

namespace mcpd {

// Custom UUIDs for MCP BLE service
#define MCP_BLE_SERVICE_UUID        "4d435000-0001-1000-8000-00805f9b34fb"
#define MCP_BLE_CHAR_RX_UUID        "4d435001-0001-1000-8000-00805f9b34fb"
#define MCP_BLE_CHAR_TX_UUID        "4d435002-0001-1000-8000-00805f9b34fb"
#define MCP_BLE_CHAR_STATUS_UUID    "4d435003-0001-1000-8000-00805f9b34fb"

// Chunk header bytes
static constexpr uint8_t BLE_CHUNK_SINGLE   = 0x00;
static constexpr uint8_t BLE_CHUNK_FIRST    = 0x01;
static constexpr uint8_t BLE_CHUNK_CONTINUE = 0x02;
static constexpr uint8_t BLE_CHUNK_FINAL    = 0x03;

using BLEMessageCallback = std::function<String(const String&)>;
using BLEConnectionCallback = std::function<void(bool connected)>;

/**
 * BLE Transport for MCP Server
 *
 * Manages a BLE GATT server with:
 *   - RX characteristic: clients write JSON-RPC requests
 *   - TX characteristic: server sends responses via notify
 *   - Status characteristic: readable connection state
 */
class BLETransport {
public:
    explicit BLETransport(const char* deviceName, uint16_t mtu = 512);
    ~BLETransport();

    /** Set handler for incoming JSON-RPC messages. Returns response string. */
    void onMessage(BLEMessageCallback cb) { _messageCallback = cb; }

    /** Set handler for connect/disconnect events */
    void onConnection(BLEConnectionCallback cb) { _connectionCallback = cb; }

    /** Start BLE advertising and GATT server */
    void begin();

    /** Process pending operations (call in loop()) */
    void loop();

    /** Stop BLE server and advertising */
    void stop();

    /** Check if a client is currently connected */
    bool isConnected() const { return _connected; }

    /** Get number of connected clients */
    int clientCount() const { return _clientCount; }

    /** Send a notification message to the connected client (server-push) */
    void sendNotification(const String& json);

    // ── Internal callbacks (called by BLE stack) ───────────────────────
    void _onConnect();
    void _onDisconnect();
    void _onWrite(const uint8_t* data, size_t len);

private:
    const char* _deviceName;
    uint16_t _mtu;
    bool _connected = false;
    int _clientCount = 0;

    BLEServer* _server = nullptr;
    BLEService* _service = nullptr;
    BLECharacteristic* _rxChar = nullptr;
    BLECharacteristic* _txChar = nullptr;
    BLECharacteristic* _statusChar = nullptr;

    BLEMessageCallback _messageCallback;
    BLEConnectionCallback _connectionCallback;

    // Reassembly buffer for chunked incoming messages
    String _rxBuffer;
    bool _rxAssembling = false;

    // Outgoing queue (responses + notifications)
    std::vector<String> _txQueue;

    /** Send data with chunking if needed */
    void _sendChunked(const String& data);

    /** Update the status characteristic */
    void _updateStatus();

    /** Process one complete incoming message */
    void _processMessage(const String& message);
};

// ════════════════════════════════════════════════════════════════════════
// Implementation
// ════════════════════════════════════════════════════════════════════════

#ifndef MCPD_TEST

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

class MCPBLEServerCallbacks : public BLEServerCallbacks {
    BLETransport* _transport;
public:
    MCPBLEServerCallbacks(BLETransport* t) : _transport(t) {}
    void onConnect(BLEServer* s) override { _transport->_onConnect(); }
    void onDisconnect(BLEServer* s) override { _transport->_onDisconnect(); }
};

class MCPBLERxCallbacks : public BLECharacteristicCallbacks {
    BLETransport* _transport;
public:
    MCPBLERxCallbacks(BLETransport* t) : _transport(t) {}
    void onWrite(BLECharacteristic* c) override {
        std::string val = c->getValue();
        _transport->_onWrite((const uint8_t*)val.data(), val.size());
    }
};

#endif // !MCPD_TEST

BLETransport::BLETransport(const char* deviceName, uint16_t mtu)
    : _deviceName(deviceName), _mtu(mtu) {}

BLETransport::~BLETransport() {
    stop();
}

void BLETransport::begin() {
#ifndef MCPD_TEST
    BLEDevice::init(_deviceName);
    BLEDevice::setMTU(_mtu);

    _server = BLEDevice::createServer();
    _server->setCallbacks(new MCPBLEServerCallbacks(this));

    _service = _server->createService(MCP_BLE_SERVICE_UUID);

    // RX: client writes requests here
    _rxChar = _service->createCharacteristic(
        MCP_BLE_CHAR_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
    );
    _rxChar->setCallbacks(new MCPBLERxCallbacks(this));

    // TX: server sends responses via notify
    _txChar = _service->createCharacteristic(
        MCP_BLE_CHAR_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ
    );
    _txChar->addDescriptor(new BLE2902());

    // Status: readable connection state
    _statusChar = _service->createCharacteristic(
        MCP_BLE_CHAR_STATUS_UUID,
        BLECharacteristic::PROPERTY_READ
    );

    _service->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(MCP_BLE_SERVICE_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);  // functions that help with iPhone connections
    adv->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    _updateStatus();
    Serial.printf("[mcpd] BLE transport started: %s\n", _deviceName);
#endif
}

void BLETransport::loop() {
    // Send queued messages
    if (_connected && !_txQueue.empty()) {
        for (const auto& msg : _txQueue) {
            _sendChunked(msg);
        }
        _txQueue.clear();
    }
}

void BLETransport::stop() {
#ifndef MCPD_TEST
    if (_server) {
        BLEDevice::deinit(false);
        _server = nullptr;
        _service = nullptr;
        _rxChar = nullptr;
        _txChar = nullptr;
        _statusChar = nullptr;
    }
#endif
    _connected = false;
    _clientCount = 0;
}

void BLETransport::sendNotification(const String& json) {
    _txQueue.push_back(json);
}

void BLETransport::_onConnect() {
    _connected = true;
    _clientCount++;
    _updateStatus();
    Serial.println("[mcpd] BLE client connected");
    if (_connectionCallback) _connectionCallback(true);
}

void BLETransport::_onDisconnect() {
    _clientCount--;
    if (_clientCount <= 0) {
        _clientCount = 0;
        _connected = false;
    }
    _updateStatus();
    Serial.println("[mcpd] BLE client disconnected");
    if (_connectionCallback) _connectionCallback(false);

#ifndef MCPD_TEST
    // Restart advertising for new connections
    BLEDevice::startAdvertising();
#endif
}

void BLETransport::_onWrite(const uint8_t* data, size_t len) {
    if (len < 1) return;

    uint8_t header = data[0];
    String chunk;
    for (size_t i = 1; i < len; i++) {
        chunk += (char)data[i];
    }

    switch (header) {
        case BLE_CHUNK_SINGLE:
            // Complete message in one chunk
            _processMessage(chunk);
            break;

        case BLE_CHUNK_FIRST:
            _rxBuffer = chunk;
            _rxAssembling = true;
            break;

        case BLE_CHUNK_CONTINUE:
            if (_rxAssembling) {
                _rxBuffer += chunk;
            }
            break;

        case BLE_CHUNK_FINAL:
            if (_rxAssembling) {
                _rxBuffer += chunk;
                _rxAssembling = false;
                _processMessage(_rxBuffer);
                _rxBuffer = "";
            }
            break;

        default:
            Serial.printf("[mcpd] BLE unknown chunk header: 0x%02x\n", header);
            break;
    }
}

void BLETransport::_processMessage(const String& message) {
    if (!_messageCallback) return;

    String response = _messageCallback(message);
    if (!response.isEmpty()) {
        _txQueue.push_back(response);
    }
}

void BLETransport::_sendChunked(const String& data) {
#ifndef MCPD_TEST
    if (!_txChar || !_connected) return;

    // Effective payload per chunk = MTU - 3 (ATT overhead) - 1 (our header)
    size_t chunkPayload = _mtu - 4;
    if (chunkPayload < 20) chunkPayload = 20;

    if (data.length() <= chunkPayload) {
        // Single chunk
        uint8_t buf[data.length() + 1];
        buf[0] = BLE_CHUNK_SINGLE;
        memcpy(buf + 1, data.c_str(), data.length());
        _txChar->setValue(buf, data.length() + 1);
        _txChar->notify();
        return;
    }

    // Multi-chunk
    size_t offset = 0;
    bool first = true;
    while (offset < data.length()) {
        size_t remaining = data.length() - offset;
        size_t thisChunk = remaining > chunkPayload ? chunkPayload : remaining;
        bool last = (offset + thisChunk >= data.length());

        uint8_t header;
        if (first) { header = BLE_CHUNK_FIRST; first = false; }
        else if (last) { header = BLE_CHUNK_FINAL; }
        else { header = BLE_CHUNK_CONTINUE; }

        uint8_t buf[thisChunk + 1];
        buf[0] = header;
        memcpy(buf + 1, data.c_str() + offset, thisChunk);
        _txChar->setValue(buf, thisChunk + 1);
        _txChar->notify();

        offset += thisChunk;
        delay(20);  // BLE needs time between notifications
    }
#endif
}

void BLETransport::_updateStatus() {
#ifndef MCPD_TEST
    if (!_statusChar) return;
    String status = _connected ? "connected" : "advertising";
    _statusChar->setValue(status.c_str());
#endif
}

} // namespace mcpd

#endif // ESP32
#endif // MCP_TRANSPORT_BLE_H
