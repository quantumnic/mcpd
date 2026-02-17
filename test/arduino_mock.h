/**
 * Arduino API Mock for host-side unit testing of mcpd.
 *
 * Provides minimal stubs for String, JsonDocument, Serial, GPIO, etc.
 * so that mcpd's JSON-RPC logic can be tested without hardware.
 */

#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <functional>
#include <vector>
#include <map>
#include <algorithm>

// ── Arduino type stubs ─────────────────────────────────────────────────

using byte = uint8_t;

// Minimal String class mimicking Arduino String
class String {
public:
    std::string _s;

    String() = default;
    String(const char* s) : _s(s ? s : "") {}
    String(const std::string& s) : _s(s) {}
    String(int v) : _s(std::to_string(v)) {}
    String(unsigned int v) : _s(std::to_string(v)) {}
    String(long v) : _s(std::to_string(v)) {}
    String(unsigned long v) : _s(std::to_string(v)) {}
    String(float v) : _s(std::to_string(v)) {}
    String(double v) : _s(std::to_string(v)) {}

    const char* c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }

    // ArduinoJson compatibility - Reader needs these on const String
    mutable size_t _readPos = 0;
    int read() const {
        if (_readPos < _s.size()) return (unsigned char)_s[_readPos++];
        return -1;
    }
    size_t readBytes(char* buf, size_t n) const {
        size_t avail = _s.size() - _readPos;
        size_t toRead = n < avail ? n : avail;
        memcpy(buf, _s.data() + _readPos, toRead);
        _readPos += toRead;
        return toRead;
    }
    // Writer needs these
    size_t write(uint8_t c) { _s += (char)c; return 1; }
    size_t write(const uint8_t* buf, size_t n) { _s.append((const char*)buf, n); return n; }

    // Arduino String methods needed by ArduinoJson
    bool concat(const char* s) { if (s) _s += s; return true; }
    bool concat(char c) { _s += c; return true; }
    void remove(unsigned int index, unsigned int count = 1) {
        if (index < _s.size()) _s.erase(index, count);
    }
    char charAt(unsigned int i) const { return i < _s.size() ? _s[i] : 0; }
    char operator[](unsigned int i) const { return charAt(i); }
    char& operator[](unsigned int i) { return _s[i]; }
    void reserve(unsigned int size) { _s.reserve(size); }

    // indexOf / substring needed by MCPResourceTemplate
    int indexOf(char ch, unsigned int from = 0) const {
        auto pos = _s.find(ch, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const String& str, unsigned int from = 0) const {
        auto pos = _s.find(str._s, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(const char* str, unsigned int from = 0) const {
        auto pos = _s.find(str, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    String substring(unsigned int from) const {
        if (from >= _s.size()) return String("");
        return String(_s.substr(from));
    }
    String substring(unsigned int from, unsigned int to) const {
        if (from >= _s.size()) return String("");
        return String(_s.substr(from, to - from));
    }
    bool startsWith(const String& prefix) const {
        return _s.compare(0, prefix._s.size(), prefix._s) == 0;
    }
    bool startsWith(const char* prefix) const {
        return _s.compare(0, strlen(prefix), prefix) == 0;
    }

    // Needed for ArduinoJson's Writer to reset the string
    String& operator=(const char* s) { _s = s ? s : ""; return *this; }
    String& operator=(std::nullptr_t) { _s.clear(); return *this; }

    bool operator<(const String& rhs) const { return _s < rhs._s; }

    String operator+(const String& rhs) const { return String(_s + rhs._s); }
    String operator+(const char* rhs) const { return String(_s + (rhs ? rhs : "")); }
    String operator+(int v) const { return String(_s + std::to_string(v)); }
    String operator+(float v) const { return String(_s + std::to_string(v)); }
    String& operator+=(const String& rhs) { _s += rhs._s; return *this; }
    String& operator+=(const char* rhs) { _s += (rhs ? rhs : ""); return *this; }
    String& operator+=(char c) { _s += c; return *this; }

    bool operator==(const String& rhs) const { return _s == rhs._s; }
    bool operator==(const char* rhs) const { return _s == (rhs ? rhs : ""); }
    bool operator!=(const String& rhs) const { return _s != rhs._s; }
    bool operator!=(const char* rhs) const { return _s != (rhs ? rhs : ""); }

    friend String operator+(const char* lhs, const String& rhs) {
        return String(std::string(lhs ? lhs : "") + rhs._s);
    }
};

// ── GPIO Mock ──────────────────────────────────────────────────────────

#define INPUT       0x0
#define OUTPUT      0x1
#define INPUT_PULLUP 0x2
#define INPUT_PULLDOWN 0x9
#define HIGH        1
#define LOW         0

// Mock pin state storage
inline std::map<int, int>& _mockPinModes() { static std::map<int, int> m; return m; }
inline std::map<int, int>& _mockPinValues() { static std::map<int, int> m; return m; }
inline std::map<int, int>& _mockAnalogValues() { static std::map<int, int> m; return m; }

inline void pinMode(int pin, int mode) { _mockPinModes()[pin] = mode; }
inline void digitalWrite(int pin, int value) { _mockPinValues()[pin] = value; }
inline int digitalRead(int pin) { return _mockPinValues().count(pin) ? _mockPinValues()[pin] : 0; }
inline int analogRead(int pin) { return _mockAnalogValues().count(pin) ? _mockAnalogValues()[pin] : 0; }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// LEDC stubs
inline void ledcSetup(int ch, int freq, int res) {}
inline void ledcAttachPin(int pin, int ch) {}
inline void ledcWrite(int ch, int duty) {}
inline void ledcDetachPin(int pin) {}

// ── Timing ─────────────────────────────────────────────────────────────

inline unsigned long millis() { return 12345; }
inline void delay(unsigned long ms) {}

// ── Random ─────────────────────────────────────────────────────────────

inline void randomSeed(unsigned long seed) {}
inline long random(long max) { return rand() % max; }
inline long random(long min, long max) { return min + rand() % (max - min); }

// ── Serial Mock ────────────────────────────────────────────────────────

struct SerialMock {
    void begin(int baud) {}
    void print(const char* s) { fprintf(stderr, "%s", s); }
    void print(const String& s) { fprintf(stderr, "%s", s.c_str()); }
    void println(const char* s = "") { fprintf(stderr, "%s\n", s); }
    void println(const String& s) { fprintf(stderr, "%s\n", s.c_str()); }
    template<typename... Args>
    void printf(const char* fmt, Args... args) { fprintf(stderr, fmt, args...); }
};
inline SerialMock Serial;

// ── ESP Mock ───────────────────────────────────────────────────────────

struct ESPMock {
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getHeapSize() { return 320000; }
    const char* getChipModel() { return "ESP32-MOCK"; }
    int getChipRevision() { return 1; }
    int getCpuFreqMHz() { return 240; }
    uint32_t getFlashChipSize() { return 4194304; }
};
inline ESPMock ESP;

inline uint32_t esp_random() { return (uint32_t)rand(); }

// ── WiFi Mock ──────────────────────────────────────────────────────────

#define WL_CONNECTED 3

struct IPAddressMock {
    String toString() { return "192.168.1.100"; }
};

struct WiFiMock {
    int status() { return WL_CONNECTED; }
    String SSID() { return "MockSSID"; }
    String SSID(int i) { return String("Network") + String(i); }
    IPAddressMock localIP() { return {}; }
    IPAddressMock gatewayIP() { return {}; }
    IPAddressMock subnetMask() { return {}; }
    IPAddressMock dnsIP() { return {}; }
    int RSSI() { return -45; }
    int RSSI(int i) { return -50 - i * 5; }
    String macAddress() { return "AA:BB:CC:DD:EE:FF"; }
    int channel() { return 6; }
    int channel(int i) { return 1 + (i % 11); }
    int scanNetworks() { return 3; }
    void scanDelete() {}
    int encryptionType(int i) { return 4; }
    void begin(const char* ssid, const char* pass) {}
};
inline WiFiMock WiFi;

// ── WebServer Mock ─────────────────────────────────────────────────────

#define HTTP_POST   1
#define HTTP_GET    2
#define HTTP_DELETE 3
#define HTTP_OPTIONS 4

class WebServer {
public:
    // Store registered routes
    struct Route {
        const char* path;
        int method;
        std::function<void()> handler;
    };

    uint16_t _port;
    std::vector<Route> _routes;
    String _body;
    std::map<String, String> _headers;
    String _responseBody;
    int _responseCode = 0;
    String _lastContentType;
    std::map<String, String> _responseHeaders;

    WebServer(uint16_t port) : _port(port) {}

    void on(const char* path, int method, std::function<void()> handler) {
        _routes.push_back({path, method, handler});
    }

    void begin() {}
    void stop() {}
    void handleClient() {}

    void collectHeaders(const char** headers, int count) {}

    String arg(const char* name) {
        if (strcmp(name, "plain") == 0) return _body;
        return "";
    }

    String header(const char* name) {
        auto it = _headers.find(String(name));
        if (it != _headers.end()) return it->second;
        return "";
    }

    void sendHeader(const char* name, const String& value) {
        _responseHeaders[String(name)] = value;
    }

    void send(int code) { _responseCode = code; }
    void send(int code, const char* contentType, const String& body) {
        _responseCode = code;
        _lastContentType = contentType;
        _responseBody = body;
    }

    // Test helpers
    void _setBody(const String& body) { _body = body; }
    void _setHeader(const char* name, const String& value) { _headers[String(name)] = value; }
    void _simulateRequest(const char* path, int method) {
        for (auto& r : _routes) {
            if (strcmp(r.path, path) == 0 && r.method == method) {
                r.handler();
                return;
            }
        }
    }
};

// ── mDNS Mock ──────────────────────────────────────────────────────────

struct MDNSMock {
    bool begin(const char* name) { return true; }
    void addService(const char* service, const char* proto, uint16_t port) {}
    void addServiceTxt(const char* service, const char* proto, const char* key, const char* value) {}
};
inline MDNSMock MDNS;

// ── Wire Mock (I2C) ────────────────────────────────────────────────────

class TwoWire {
public:
    void begin() {}
    void beginTransmission(uint8_t addr) {}
    uint8_t endTransmission() { return 0; }
    uint8_t requestFrom(uint8_t addr, uint8_t count) { return count; }
    int available() { return 0; }
    int read() { return 0; }
    void write(uint8_t b) {}
};
inline TwoWire Wire;

#endif // ARDUINO_MOCK_H
