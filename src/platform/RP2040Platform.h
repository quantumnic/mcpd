/**
 * mcpd — RP2040 / Pico W Platform Implementation
 *
 * Uses the arduino-pico core by Earle F. Philhower III.
 * https://github.com/earlephilhower/arduino-pico
 */

#ifndef MCPD_RP2040_PLATFORM_H
#define MCPD_RP2040_PLATFORM_H

#if defined(ARDUINO_ARCH_RP2040)

#include "Platform.h"
#include <WiFi.h>
#include <hardware/adc.h>

namespace mcpd {
namespace hal {

class RP2040WiFi : public WiFiHAL {
public:
    bool connect(const char* ssid, const char* password, unsigned long timeoutMs = 15000) override {
        ::WiFi.mode(WIFI_STA);
        ::WiFi.begin(ssid, password);
        unsigned long start = millis();
        while (::WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
            delay(100);
        }
        return ::WiFi.status() == WL_CONNECTED;
    }

    bool startAP(const char* ssid, const char* password = nullptr) override {
        ::WiFi.mode(WIFI_AP);
        if (password && strlen(password) >= 8) {
            return ::WiFi.softAP(ssid, password);
        }
        return ::WiFi.softAP(ssid);
    }

    void stopAP() override { ::WiFi.softAPdisconnect(true); }
    void disconnect() override { ::WiFi.disconnect(); }

    WiFiStatus status() override {
        switch (::WiFi.status()) {
            case WL_CONNECTED: return WiFiStatus::CONNECTED;
            default: return WiFiStatus::DISCONNECTED;
        }
    }

    WiFiInfo info() override {
        return WiFiInfo{
            ::WiFi.localIP().toString(),
            ::WiFi.macAddress(),
            ::WiFi.SSID(),
            ::WiFi.RSSI()
        };
    }

    String localIP() override { return ::WiFi.localIP().toString(); }
};

class RP2040GPIO : public GPIOHAL {
public:
    void pinMode(uint8_t pin, PinMode_ mode) override {
        switch (mode) {
            case PinMode_::INPUT_MODE:         ::pinMode(pin, INPUT); break;
            case PinMode_::OUTPUT_MODE:         ::pinMode(pin, OUTPUT); break;
            case PinMode_::INPUT_PULLUP_MODE:   ::pinMode(pin, INPUT_PULLUP); break;
            case PinMode_::INPUT_PULLDOWN_MODE: ::pinMode(pin, INPUT_PULLDOWN); break;
        }
    }
    void digitalWrite(uint8_t pin, uint8_t value) override { ::digitalWrite(pin, value); }
    int digitalRead(uint8_t pin) override { return ::digitalRead(pin); }
    void analogWrite(uint8_t pin, uint16_t value) override { ::analogWrite(pin, value); }
    uint16_t analogRead(uint8_t pin) override { return ::analogRead(pin); }
    void setAnalogReadResolution(uint8_t bits) override { ::analogReadResolution(bits); }
    void setPWMFrequency(uint8_t pin, uint32_t freq) override {
        ::analogWriteFreq(freq);
    }
};

class RP2040System : public SystemHAL {
public:
    uint32_t freeHeap() override { return rp2040.getFreeHeap(); }
    uint32_t totalHeap() override { return rp2040.getTotalHeap(); }
    uint32_t cpuFreqMHz() override { return F_CPU / 1000000; }
    const char* platformName() override { return "RP2040"; }
    String chipId() override {
        // Use unique board ID from flash
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);
        char buf[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++) {
            snprintf(buf + 2 * i, 3, "%02x", id.id[i]);
        }
        return String(buf);
    }
    uint32_t random() override { return ::random(0, UINT32_MAX); }
    void restart() override { rp2040.reboot(); }
};

class RP2040Platform : public Platform {
public:
    WiFiHAL& wifi() override { return _wifi; }
    GPIOHAL& gpio() override { return _gpio; }
    SystemHAL& system() override { return _system; }

private:
    RP2040WiFi _wifi;
    RP2040GPIO _gpio;
    RP2040System _system;
};

} // namespace hal
} // namespace mcpd

#endif // ARDUINO_ARCH_RP2040
#endif // MCPD_RP2040_PLATFORM_H
