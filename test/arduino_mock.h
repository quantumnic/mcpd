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
    String(float v, int decimals) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, (double)v);
        _s = buf;
    }
    String(double v) : _s(std::to_string(v)) {}
    String(double v, int decimals) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        _s = buf;
    }

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
    bool endsWith(const String& suffix) const {
        if (suffix.length() > length()) return false;
        return _s.compare(length() - suffix.length(), suffix.length(), suffix._s) == 0;
    }
    bool endsWith(const char* suffix) const {
        size_t slen = strlen(suffix);
        if (slen > length()) return false;
        return _s.compare(length() - slen, slen, suffix) == 0;
    }

    // Needed for ArduinoJson's Writer to reset the string
    String& operator=(const char* s) { _s = s ? s : ""; return *this; }
    String& operator=(std::nullptr_t) { _s.clear(); return *this; }

    bool operator<(const String& rhs) const { return _s < rhs._s; }

    bool equalsIgnoreCase(const String& other) const {
        if (_s.size() != other._s.size()) return false;
        for (size_t i = 0; i < _s.size(); i++) {
            if (tolower(_s[i]) != tolower(other._s[i])) return false;
        }
        return true;
    }

    int toInt() const { return std::atoi(_s.c_str()); }
    float toFloat() const { return std::atof(_s.c_str()); }
    double toDouble() const { return std::strtod(_s.c_str(), nullptr); }

    void replace(const String& find, const String& repl) {
        size_t pos = 0;
        while ((pos = _s.find(find._s, pos)) != std::string::npos) {
            _s.replace(pos, find._s.length(), repl._s);
            pos += repl._s.length();
        }
    }
    void replace(const char* find, const char* repl) {
        replace(String(find), String(repl));
    }

    String operator+(const String& rhs) const { return String(_s + rhs._s); }
    String operator+(const char* rhs) const { return String(_s + (rhs ? rhs : "")); }
    String operator+(int v) const { return String(_s + std::to_string(v)); }
    String operator+(unsigned int v) const { return String(_s + std::to_string(v)); }
    String operator+(unsigned long v) const { return String(_s + std::to_string(v)); }
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
inline long constrain(long x, long lo, long hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline long map(long x, long in_min, long in_max, long out_min, long out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// LEDC stubs
inline void ledcSetup(int ch, int freq, int res) {}
inline void ledcAttachPin(int pin, int ch) {}
inline void ledcWrite(int ch, int duty) {}
inline void ledcDetachPin(int pin) {}

// ── Timing ─────────────────────────────────────────────────────────────

inline unsigned long& _mockMillis() { static unsigned long m = 12345; return m; }
inline unsigned long millis() { return _mockMillis()++; }
inline void delay(unsigned long ms) { _mockMillis() += ms; }
inline void delayMicroseconds(unsigned int us) { (void)us; }
inline void tone(int pin, unsigned int frequency, unsigned long duration = 0) { (void)pin; (void)frequency; (void)duration; }
inline void noTone(int pin) { (void)pin; }
inline unsigned long pulseIn(int pin, int state, unsigned long timeout = 1000000UL) { (void)pin; (void)state; (void)timeout; return 500; }

// ── Random ─────────────────────────────────────────────────────────────

inline void randomSeed(unsigned long seed) {}
inline long random(long max) { return rand() % max; }
inline long random(long min, long max) { return min + rand() % (max - min); }

// ── Interrupt Mock ──────────────────────────────────────────────────────

#define RISING    1
#define FALLING   2
#define CHANGE    3
#define IRAM_ATTR

inline int digitalPinToInterrupt(int pin) { return pin; }

// Store attached interrupt handlers for testing
inline std::map<int, std::function<void()>>& _mockInterruptHandlers() {
    static std::map<int, std::function<void()>> m;
    return m;
}

inline void attachInterrupt(int pin, void (*isr)(), int mode) {
    _mockInterruptHandlers()[pin] = isr;
}

inline void detachInterrupt(int pin) {
    _mockInterruptHandlers().erase(pin);
}

inline void noInterrupts() {}
inline void interrupts() {}

// ── Touch Mock ─────────────────────────────────────────────────────────

inline std::map<int, uint16_t>& _mockTouchValues() {
    static std::map<int, uint16_t> m;
    return m;
}
inline uint16_t touchRead(int pin) {
    return _mockTouchValues().count(pin) ? _mockTouchValues()[pin] : 50;
}

// ── Timer Mock ─────────────────────────────────────────────────────────

struct hw_timer_t {};
inline hw_timer_t _mockTimers[4];
inline hw_timer_t* timerBegin(int num, int divider, bool countUp) {
    (void)divider; (void)countUp;
    return &_mockTimers[num];
}
inline void timerAttachInterrupt(hw_timer_t* t, void (*fn)(), bool edge) { (void)t; (void)fn; (void)edge; }
inline void timerAlarmWrite(hw_timer_t* t, uint64_t val, bool autoreload) { (void)t; (void)val; (void)autoreload; }
inline void timerAlarmEnable(hw_timer_t* t) { (void)t; }
inline void timerAlarmDisable(hw_timer_t* t) { (void)t; }
inline void timerDetachInterrupt(hw_timer_t* t) { (void)t; }
inline void timerEnd(hw_timer_t* t) { (void)t; }

inline unsigned long micros() { return 12345000; }

// ── Power/Sleep Mock ───────────────────────────────────────────────────

inline void esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; }
inline void esp_deep_sleep_start() {}
inline void esp_light_sleep_start() {}
inline void esp_restart() {}
inline int esp_sleep_get_wakeup_cause() { return 0; }

// ── HardwareSerial Mock ────────────────────────────────────────────────

#define SERIAL_8N1 0x800001c

struct HardwareSerial {
    std::string _rxBuffer;
    size_t _rxPos = 0;
    unsigned long _timeout = 1000;

    void begin(long baud) { (void)baud; }
    void begin(long baud, int config, int rxPin, int txPin) { (void)baud; (void)config; (void)rxPin; (void)txPin; }
    void end() {}
    void setTimeout(unsigned long t) { _timeout = t; }
    int available() { return (int)(_rxBuffer.size() - _rxPos); }
    int read() {
        if (_rxPos < _rxBuffer.size()) return (unsigned char)_rxBuffer[_rxPos++];
        return -1;
    }
    size_t write(uint8_t b) { (void)b; return 1; }
    size_t print(const String& s) { return s.length(); }
    size_t print(const char* s) { return strlen(s); }
    template<typename... Args>
    void printf(const char* fmt, Args... args) { (void)fmt; }

    // Test helper
    void _setRxBuffer(const std::string& data) { _rxBuffer = data; _rxPos = 0; }
};

inline HardwareSerial Serial1;
inline HardwareSerial Serial2;

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

typedef int esp_reset_reason_t;
#define ESP_RST_POWERON 1

struct ESPMock {
    uint32_t getFreeHeap() { return 200000; }
    uint32_t getMinFreeHeap() { return 150000; }
    uint32_t getHeapSize() { return 320000; }
    const char* getChipModel() { return "ESP32-MOCK"; }
    int getChipRevision() { return 1; }
    int getCpuFreqMHz() { return 240; }
    uint32_t getFlashChipSize() { return 4194304; }
    const char* getSdkVersion() { return "mock-sdk-1.0"; }
};
inline ESPMock ESP;

inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }

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

// ── WiFiClient Mock ────────────────────────────────────────────────────

class WiFiClient {
public:
    bool connected() const { return _connected; }
    operator bool() const { return _connected; }
    int available() const { return 0; }
    int read() { return -1; }
    size_t write(uint8_t b) { _buffer += (char)b; return 1; }
    size_t write(const uint8_t* buf, size_t len) {
        for (size_t i = 0; i < len; i++) _buffer += (char)buf[i];
        return len;
    }
    size_t print(const String& s) { _buffer += s; return s.length(); }
    size_t println(const String& s) { _buffer += s + "\n"; return s.length() + 1; }
    size_t println() { _buffer += "\n"; return 1; }
    void flush() {}
    void stop() { _connected = false; }

    // Test helpers
    String getBuffer() const { return _buffer; }
    void setConnected(bool c) { _connected = c; }

private:
    bool _connected = true;
    String _buffer;
};

// ── WiFiServer Mock ────────────────────────────────────────────────────

class WiFiServer {
public:
    WiFiServer(uint16_t port) : _port(port) {}
    void begin() {}
    void stop() {}
    WiFiClient available() { return WiFiClient(); }
private:
    uint16_t _port;
};

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
    std::map<String, String> _args;

    WebServer(uint16_t port) : _port(port) {}

    void on(const char* path, int method, std::function<void()> handler) {
        _routes.push_back({path, method, handler});
    }

    void begin() {}
    void stop() {}
    void handleClient() {}

    WiFiClient client() { return WiFiClient(); }

    void collectHeaders(const char** headers, int count) {}

    String arg(const char* name) {
        if (strcmp(name, "plain") == 0) return _body;
        auto it = _args.find(String(name));
        if (it != _args.end()) return it->second;
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
    void _testSetHeader(const char* name, const String& value) { _headers[String(name)] = value; }
    void _testSetArg(const char* name, const String& value) { _args[String(name)] = value; }
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
