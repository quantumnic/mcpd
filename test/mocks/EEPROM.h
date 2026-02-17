/**
 * EEPROM.h mock — RAM-backed EEPROM emulation
 */
#pragma once
#include <cstdint>
#include <cstring>

class EEPROMClass {
public:
    static constexpr size_t SIZE = 4096;
    uint8_t _data[SIZE] = {};
    bool _begun = false;

    void begin(size_t size) { _begun = true; }
    void end() { _begun = false; }

    uint8_t read(int addr) {
        if (addr >= 0 && (size_t)addr < SIZE) return _data[addr];
        return 0xFF;
    }

    void write(int addr, uint8_t val) {
        if (addr >= 0 && (size_t)addr < SIZE) _data[addr] = val;
    }

    bool commit() { return true; }

    template<typename T>
    T& get(int addr, T& t) {
        if (addr >= 0 && (size_t)(addr + sizeof(T)) <= SIZE)
            memcpy(&t, &_data[addr], sizeof(T));
        return t;
    }

    template<typename T>
    const T& put(int addr, const T& t) {
        if (addr >= 0 && (size_t)(addr + sizeof(T)) <= SIZE)
            memcpy(&_data[addr], &t, sizeof(T));
        return t;
    }

    uint8_t operator[](int addr) const {
        if (addr >= 0 && (size_t)addr < SIZE) return _data[addr];
        return 0xFF;
    }
};

inline EEPROMClass EEPROM;
