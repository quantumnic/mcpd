// EEPROM mock for native tests
#pragma once
#include <cstdint>
#include <cstring>

class EEPROMClass {
    uint8_t _data[4096];
    bool _begun = false;
public:
    EEPROMClass() { memset(_data, 0xFF, sizeof(_data)); }
    void begin(int size) { (void)size; _begun = true; memset(_data, 0, sizeof(_data)); }
    uint8_t read(int addr) { return (addr >= 0 && addr < 4096) ? _data[addr] : 0xFF; }
    void write(int addr, uint8_t val) { if (addr >= 0 && addr < 4096) _data[addr] = val; }
    bool commit() { return true; }
    void end() { _begun = false; }
};

static EEPROMClass EEPROM;
