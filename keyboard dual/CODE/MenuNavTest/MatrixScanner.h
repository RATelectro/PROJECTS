#pragma once

#include "KeyboardConfig.h"

class MatrixScanner {
public:
    void begin();
    void update(uint32_t nowMs);

    bool isPressed(uint8_t index) const;
    bool rawIsPressed(uint8_t index) const;
    bool changed(uint8_t index) const;
    uint64_t changedMask() const { return _changedMask; }
    uint64_t pressedMask() const;

    // Test-only (MenuNavTest): when false, rows 3/4 (the OLED-shared SDA/SCL
    // lines) are never driven during scans, so scanning produces no edges on
    // the I2C bus.
    bool scanSharedRows = true;

private:
    bool _raw[Config::NUM_KEYS] = {};
    bool _stable[Config::NUM_KEYS] = {};
    uint32_t _rawChangedAt[Config::NUM_KEYS] = {};
    uint64_t _changedMask = 0;
};
