#pragma once

#include "KeyboardConfig.h"
#include "KeyAction.h"
#include "Settings.h"

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

    // Both report sizes are always compiled in; the active one is selected at
    // runtime through GlobalSettings.rollover.
    void setRollover(RolloverMode mode) { _rollover = mode; _forceReport = true; }
    RolloverMode rollover() const { return _rollover; }

    BLEServer *server() const { return _server; }

private:
    void buildReport(uint8_t *report, const bool pressed[],
                     const KeyAction latched[]) const;
    void sendReport(const uint8_t *report);
    void sendReleaseAll();

    MultiKey *_owner = nullptr;
    BLEServer *_server = nullptr;
    BLEHIDDevice *_hid = nullptr;
    BLECharacteristic *_inputReport = nullptr;   // 8-byte boot report (id 1)
    BLECharacteristic *_inputReportNkro = nullptr; // 18-byte NKRO report (id 2)
    BLECharacteristic *_keymapDump = nullptr;
    volatile bool _connected = false;
    volatile bool _advertising = false;
    volatile bool _forceReport = true;

    RolloverMode _rollover = ROLLOVER_6KRO;

    static constexpr size_t BOOT_REPORT_SIZE = 8;
    static constexpr size_t NKRO_REPORT_SIZE = 18; // mods + reserved + 128-bit bitmap
    uint8_t _lastReport[BOOT_REPORT_SIZE] = {};
    uint8_t _lastReportNkro[NKRO_REPORT_SIZE] = {};
};
