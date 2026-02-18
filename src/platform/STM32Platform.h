/**
 * mcpd — STM32 Platform Implementation
 *
 * HAL implementation for STM32 boards (Blue Pill, Nucleo, etc.)
 * using the STM32duino framework.
 *
 * Tested with: STM32F103C8 (Blue Pill), STM32F401RE (Nucleo)
 * Requires: STM32duino core + STM32Ethernet or WiFi shield
 */

#ifndef MCPD_STM32_PLATFORM_H
#define MCPD_STM32_PLATFORM_H

#include "Platform.h"

#ifdef ARDUINO_ARCH_STM32

#include <WiFi.h>  // or STM32Ethernet — depends on shield

namespace mcpd {
namespace hal {

// ── STM32 WiFi HAL ────────────────────────────────────────────────────

class STM32WiFiHAL : public WiFiHAL {
public:
    bool connect(const char* ssid, const char* password,
                 unsigned long timeoutMs = 15000) override {
        WiFi.begin(ssid, password);
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > timeoutMs) return false;
            delay(100);
        }
        return true;
    }

    bool startAP(const char* ssid, const char* password = nullptr) override {
        if (password) {
            WiFi.beginAP(ssid, password);
        } else {
            WiFi.beginAP(ssid);
        }
        return true;
    }

    void stopAP() override {
        // STM32duino doesn't have a direct stopAP — disconnect suffices
        WiFi.disconnect();
    }

    void disconnect() override {
        WiFi.disconnect();
    }

    WiFiStatus status() override {
        switch (WiFi.status()) {
            case WL_CONNECTED:    return WiFiStatus::CONNECTED;
            case WL_DISCONNECTED: return WiFiStatus::DISCONNECTED;
            case WL_IDLE_STATUS:  return WiFiStatus::CONNECTING;
            default:              return WiFiStatus::ERROR;
        }
    }

    WiFiInfo info() override {
        WiFiInfo i;
        i.ip = WiFi.localIP().toString();
        // MAC address
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        i.mac = macStr;
        i.ssid = WiFi.SSID();
        i.rssi = WiFi.RSSI();
        return i;
    }

    String localIP() override {
        return WiFi.localIP().toString();
    }
};

// ── STM32 GPIO HAL ───────────────────────────────────────────────────

class STM32GPIOHAL : public GPIOHAL {
public:
    void pinMode(uint8_t pin, PinMode_ mode) override {
        switch (mode) {
            case PinMode_::INPUT_MODE:         ::pinMode(pin, INPUT); break;
            case PinMode_::OUTPUT_MODE:        ::pinMode(pin, OUTPUT); break;
            case PinMode_::INPUT_PULLUP_MODE:  ::pinMode(pin, INPUT_PULLUP); break;
            case PinMode_::INPUT_PULLDOWN_MODE: ::pinMode(pin, INPUT_PULLDOWN); break;
        }
    }

    void digitalWrite(uint8_t pin, uint8_t value) override {
        ::digitalWrite(pin, value);
    }

    int digitalRead(uint8_t pin) override {
        return ::digitalRead(pin);
    }

    void analogWrite(uint8_t pin, uint16_t value) override {
        ::analogWrite(pin, value);
    }

    uint16_t analogRead(uint8_t pin) override {
        return ::analogRead(pin);
    }

    void setAnalogReadResolution(uint8_t bits) override {
        ::analogReadResolution(bits);
    }

    void setPWMFrequency(uint8_t pin, uint32_t freq) override {
        // STM32duino: use HardwareTimer API for frequency control
        // For basic usage, analogWriteFrequency is available on some cores
#ifdef analogWriteFrequency
        analogWriteFrequency(freq);
#else
        (void)pin;
        (void)freq;
        // Advanced: use HardwareTimer directly
        // TIM_TypeDef *instance = (TIM_TypeDef *)pinmap_peripheral(
        //     digitalPinToPinName(pin), PinMap_PWM);
        // HardwareTimer *timer = new HardwareTimer(instance);
        // timer->setOverflow(freq, HERTZ_FORMAT);
#endif
    }
};

// ── STM32 System HAL ─────────────────────────────────────────────────

class STM32SystemHAL : public SystemHAL {
public:
    uint32_t freeHeap() override {
        // STM32duino doesn't have a direct API; estimate from mallinfo or stack
        // Use a conservative estimate based on available RAM
        extern char _end;
        extern char _estack;
        char stackVar;
        return (uint32_t)(&stackVar - &_end);
    }

    uint32_t totalHeap() override {
        // Depends on linker script — typical STM32F103: 20KB, F401: 96KB
#if defined(STM32F1xx)
        return 20 * 1024;
#elif defined(STM32F4xx)
        return 96 * 1024;
#elif defined(STM32H7xx)
        return 512 * 1024;
#else
        return 64 * 1024;  // Conservative default
#endif
    }

    uint32_t cpuFreqMHz() override {
        return HAL_RCC_GetSysClockFreq() / 1000000;
    }

    const char* platformName() override {
        return "STM32";
    }

    String chipId() override {
        // STM32 has a 96-bit unique device ID at a fixed address
        uint32_t* uid = (uint32_t*)UID_BASE;
        char id[25];
        snprintf(id, sizeof(id), "%08lX%08lX%08lX", uid[0], uid[1], uid[2]);
        return String(id);
    }

    uint32_t random() override {
#if defined(HAL_RNG_MODULE_ENABLED)
        // Use hardware RNG if available (F4, F7, H7, L4, etc.)
        RNG_HandleTypeDef hrng;
        hrng.Instance = RNG;
        HAL_RNG_Init(&hrng);
        uint32_t val;
        HAL_RNG_GenerateRandomNumber(&hrng, &val);
        return val;
#else
        // Fallback: seed from ADC noise
        return (uint32_t)::random(0, UINT32_MAX);
#endif
    }

    void restart() override {
        HAL_NVIC_SystemReset();
    }
};

// ── Combined Platform ────────────────────────────────────────────────

class STM32Platform : public Platform {
public:
    WiFiHAL& wifi() override { return _wifi; }
    GPIOHAL& gpio() override { return _gpio; }
    SystemHAL& system() override { return _system; }

private:
    STM32WiFiHAL _wifi;
    STM32GPIOHAL _gpio;
    STM32SystemHAL _system;
};

} // namespace hal
} // namespace mcpd

#endif // ARDUINO_ARCH_STM32
#endif // MCPD_STM32_PLATFORM_H
