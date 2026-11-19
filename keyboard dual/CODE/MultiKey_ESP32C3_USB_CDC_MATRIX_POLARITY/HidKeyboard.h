#pragma once

#include "KeyboardConfig.h"
#include "KeyAction.h"

class MultiKey;
class BLEServer;
class BLEHIDDevice;
class BLECharacteristic;

class HidKeyboard {
public:
    void begin(MultiKey &owner);
    void update(const bool pressed[Config::NUM_KEYS],
                const KeyAction latched[Config::NUM_KEYS]);
    void requestReport() { _forceReport = true; }
    void updateDumpCharacteristic();

    bool isConnected() const { return _connected; }
    bool isAdvertising() const { return _advertising; }
    void onHostConnect();
    void onHostDisconnect();

private:
    void buildReport(uint8_t *report, const bool pressed[],
                     const KeyAction latched[]) const;
    void sendReport(const uint8_t *report);

    MultiKey *_owner = nullptr;
    BLEServer *_server = nullptr;
    BLEHIDDevice *_hid = nullptr;
    BLECharacteristic *_inputReport = nullptr;
    BLECharacteristic *_keymapDump = nullptr;
    volatile bool _connected = false;
    volatile bool _advertising = false;
    volatile bool _forceReport = true;

#if MULTIKEY_USE_NKRO
    static constexpr size_t REPORT_SIZE = 31;
#else
    static constexpr size_t REPORT_SIZE = 8;
#endif
    uint8_t _lastReport[REPORT_SIZE] = {};
};
