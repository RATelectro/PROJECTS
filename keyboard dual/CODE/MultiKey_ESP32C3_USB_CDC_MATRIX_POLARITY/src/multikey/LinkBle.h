#pragma once

#include <Arduino.h>
#include "../../KeyboardConfig.h"
#include "../../Settings.h"
#include "../../SerialCli.h"

class MultiKey;
class BLECharacteristic;
class BLERemoteCharacteristic;
class BLEClient;
class BLEAdvertisedDevice;

// Multikey group linkage over BLE.
//
// keyboard_0 is the master: it is the HID device toward the PC, receives the
// PC configuration and merges key events from slave keyboards 1..5 into its
// HID reports. Slaves run their own keymaps and forward resolved HID events
// to the master; the master pushes global configuration to them and stores
// slave-side changes into its multikey configuration slots.
//
// The same module also carries the BLE COM port (BT_MODE_COM): a Nordic-UART
// style service that bridges the serial CLI over Bluetooth.
class LinkBle {
public:
    // Master-side per-slave merge state consumed by the HID report builder.
    struct SlaveState {
        bool connected = false;
        uint8_t modifiers = 0;
        uint8_t keys[16]; // pressed non-modifier usages
        uint8_t count = 0;
    };

    void begin(MultiKey &keyboard);
    void update();
    void applySettings(); // role + TX power from GlobalSettings
    void pushConfig();    // master: push global config to all connected slaves

    // Slave: forward a resolved HID key event to the master.
    void onKeyEvent(uint8_t usage, uint8_t modifiers, bool pressed);

    // COM port bridge (serial CLI over BLE).
    void comWrite(const uint8_t *data, size_t length);
    bool comActive() const;

    bool isMaster() const;
    bool isSlave() const { return !isMaster(); }
    uint8_t connectedSlaves() const;
    const SlaveState &slave(uint8_t index) const { return _slaves[index]; } // index 0..4 => ids 1..5

    // Internal entry points used by the BLE callback adapters.
    void onScanResult(BLEAdvertisedDevice &device);
    void onClientConnect(BLEClient *client);
    void onClientDisconnect(BLEClient *client);
    void onSlaveNotify(uint8_t index, const uint8_t *data, size_t length);
    void onLinkRxWrite(const uint8_t *data, size_t length);
    void onComRxWrite(const uint8_t *data, size_t length);
    void onComSubscribe(bool subscribed);

private:
    void startScan();
    void stopScan();
    void handleLinkPacket(const uint8_t *data, size_t length);      // master side
    void handleLinkWrite(const uint8_t *data, size_t length);       // slave side
    void handleComWrite(const uint8_t *data, size_t length);
    void sendToSlave(uint8_t index, const uint8_t *data, size_t length);

    friend class LinkClientCallbacks;
    friend class LinkScanCallbacks;
    friend class LinkServerCallbacks;

    MultiKey *_kbd = nullptr;
    SlaveState _slaves[Config::MULTIKEY_MAX_SLAVES];

    // COM port state.
    BLECharacteristic *_comTx = nullptr;
    bool _comSubscribed = false;
    char _comLine[SerialCli::LINE_CAPACITY] = {};
    size_t _comLength = 0;

    // Master client state (per slave).
    BLEClient *_clients[Config::MULTIKEY_MAX_SLAVES] = {};
    BLERemoteCharacteristic *_remoteTx[Config::MULTIKEY_MAX_SLAVES] = {};
    BLERemoteCharacteristic *_remoteRx[Config::MULTIKEY_MAX_SLAVES] = {};
    bool _connecting[Config::MULTIKEY_MAX_SLAVES] = {};

    // Slave server state.
    BLECharacteristic *_linkTx = nullptr;
    uint32_t _lastScanMs = 0;
    bool _scanning = false;
};
