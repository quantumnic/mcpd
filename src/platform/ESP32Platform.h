/**
 * mcpd — ESP32 Platform Implementation
 */

#ifndef MCPD_ESP32_PLATFORM_H
#define MCPD_ESP32_PLATFORM_H

#ifdef ESP32

#include "Platform.h"
#include <WiFi.h>
#include <esp_system.h>

namespace mcpd {
namespace hal {

class ESP32WiFi : public WiFiHAL {
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
            case WL_IDLE_STATUS: return WiFiStatus::DISCONNECTED;
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

class ESP32GPIO : public GPIOHAL {
public:
    void pinMode(uint8_t pin, PinMode_ mode) override {
        switch (mode) {
            case PinMode_::INPUT_MODE:        ::pinMode(pin, INPUT); break;
            case PinMode_::OUTPUT_MODE:        ::pinMode(pin, OUTPUT); break;
            case PinMode_::INPUT_PULLUP_MODE:  ::pinMode(pin, INPUT_PULLUP); break;
            case PinMode_::INPUT_PULLDOWN_MODE: ::pinMode(pin, INPUT_PULLDOWN); break;
        }
    }
    void digitalWrite(uint8_t pin, uint8_t value) override { ::digitalWrite(pin, value); }
    int digitalRead(uint8_t pin) override { return ::digitalRead(pin); }
    void analogWrite(uint8_t pin, uint16_t value) override { ledcWrite(pin, value); }
    uint16_t analogRead(uint8_t pin) override { return ::analogRead(pin); }
    void setAnalogReadResolution(uint8_t bits) override { analogReadResolution(bits); }
    void setPWMFrequency(uint8_t pin, uint32_t freq) override {
        // ESP32 Arduino 3.x: ledcAttach handles channel assignment
        ledcAttach(pin, freq, 8);
    }
};

class ESP32System : public SystemHAL {
public:
    uint32_t freeHeap() override { return ESP.getFreeHeap(); }
    uint32_t totalHeap() override { return ESP.getHeapSize(); }
    uint32_t cpuFreqMHz() override { return ESP.getCpuFreqMHz(); }
    const char* platformName() override { return "ESP32"; }
    String chipId() override {
        uint64_t mac = ESP.getEfuseMac();
        char id[18];
        snprintf(id, sizeof(id), "%04X%08X",
                 (uint16_t)(mac >> 32), (uint32_t)mac);
        return String(id);
    }
    uint32_t random() override { return esp_random(); }
    void restart() override { ESP.restart(); }
};

class ESP32Platform : public Platform {
public:
    WiFiHAL& wifi() override { return _wifi; }
    GPIOHAL& gpio() override { return _gpio; }
    SystemHAL& system() override { return _system; }

private:
    ESP32WiFi _wifi;
    ESP32GPIO _gpio;
    ESP32System _system;
};

} // namespace hal
} // namespace mcpd

#endif // ESP32
#endif // MCPD_ESP32_PLATFORM_H
