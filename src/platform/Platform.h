/**
 * mcpd — Hardware Abstraction Layer (HAL)
 *
 * Abstracts platform-specific APIs (WiFi, GPIO, Analog, System) so mcpd
 * can run on ESP32 and RP2040/Pico W without #ifdef spaghetti in business logic.
 *
 * Each platform provides a concrete implementation of this interface.
 */

#ifndef MCPD_PLATFORM_H
#define MCPD_PLATFORM_H

#include <Arduino.h>
#include <functional>

namespace mcpd {
namespace hal {

// ── WiFi Abstraction ───────────────────────────────────────────────────

enum class WiFiStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    AP_MODE,
    ERROR
};

struct WiFiInfo {
    String ip;
    String mac;
    String ssid;
    int32_t rssi;
};

class WiFiHAL {
public:
    virtual ~WiFiHAL() = default;

    /** Connect to a WiFi network in station mode */
    virtual bool connect(const char* ssid, const char* password, unsigned long timeoutMs = 15000) = 0;

    /** Start a soft access point */
    virtual bool startAP(const char* ssid, const char* password = nullptr) = 0;

    /** Stop the soft access point */
    virtual void stopAP() = 0;

    /** Disconnect from WiFi */
    virtual void disconnect() = 0;

    /** Get current WiFi status */
    virtual WiFiStatus status() = 0;

    /** Get WiFi info (IP, MAC, SSID, RSSI) */
    virtual WiFiInfo info() = 0;

    /** Get local IP as string */
    virtual String localIP() = 0;
};

// ── GPIO Abstraction ───────────────────────────────────────────────────

enum class PinMode_ {
    INPUT_MODE,
    OUTPUT_MODE,
    INPUT_PULLUP_MODE,
    INPUT_PULLDOWN_MODE
};

class GPIOHAL {
public:
    virtual ~GPIOHAL() = default;

    virtual void pinMode(uint8_t pin, PinMode_ mode) = 0;
    virtual void digitalWrite(uint8_t pin, uint8_t value) = 0;
    virtual int digitalRead(uint8_t pin) = 0;
    virtual void analogWrite(uint8_t pin, uint16_t value) = 0;
    virtual uint16_t analogRead(uint8_t pin) = 0;

    /** Set analog read resolution in bits (default: 12 for ESP32, 12 for Pico W) */
    virtual void setAnalogReadResolution(uint8_t bits) = 0;

    /** Set PWM frequency for a pin (platform-specific) */
    virtual void setPWMFrequency(uint8_t pin, uint32_t freq) = 0;
};

// ── System Abstraction ─────────────────────────────────────────────────

class SystemHAL {
public:
    virtual ~SystemHAL() = default;

    /** Free heap memory in bytes */
    virtual uint32_t freeHeap() = 0;

    /** Total heap memory in bytes */
    virtual uint32_t totalHeap() = 0;

    /** CPU frequency in MHz */
    virtual uint32_t cpuFreqMHz() = 0;

    /** Platform name (e.g. "ESP32", "RP2040") */
    virtual const char* platformName() = 0;

    /** Chip/board identifier */
    virtual String chipId() = 0;

    /** Hardware random number */
    virtual uint32_t random() = 0;

    /** Restart the device */
    virtual void restart() = 0;

    /** Uptime in milliseconds */
    virtual unsigned long uptimeMs() { return millis(); }
};

// ── Platform (combines all HALs) ──────────────────────────────────────

class Platform {
public:
    virtual ~Platform() = default;

    virtual WiFiHAL& wifi() = 0;
    virtual GPIOHAL& gpio() = 0;
    virtual SystemHAL& system() = 0;
};

/** Get the platform singleton (set during setup) */
Platform& platform();

/** Set the platform singleton */
void setPlatform(Platform* p);

} // namespace hal
} // namespace mcpd

#endif // MCPD_PLATFORM_H
