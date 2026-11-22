#include "HidKeyboard.h"
#include "Keyboard.h"
#include "src/multikey/LinkBle.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

namespace {

// Report 1 (id 1): classic 8-byte boot report.
// Report 2 (id 2): NKRO - 8 modifier bits, 8 reserved bits, 128-bit key bitmap.
const uint8_t KEYBOARD_REPORT_MAP[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0xE7, 0x75, 0x08, 0x95, 0x06, 0x81, 0x00,
    0x85, 0x02,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
    0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x01,
    0x19, 0x00, 0x29, 0x7F, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x80, 0x81, 0x02,
    0xC0
};

class ServerCallbacks : public BLEServerCallbacks {
public:
    explicit ServerCallbacks(HidKeyboard &keyboard) : _keyboard(keyboard) {}
    void onConnect(BLEServer *) override { _keyboard.onHostConnect(); }
    void onDisconnect(BLEServer *) override { _keyboard.onHostDisconnect(); }
private:
    HidKeyboard &_keyboard;
};

class KeymapWriteCallbacks : public BLECharacteristicCallbacks {
public:
    explicit KeymapWriteCallbacks(MultiKey &keyboard) : _keyboard(keyboard) {}

    void onWrite(BLECharacteristic *characteristic) override {
        const String value = characteristic->getValue();
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(value.c_str());
        const size_t length = value.length();

        // Backward-compatible packet: [index][usage][modifiers], layer 0.
        if (length == 3) {
            if (bytes[0] == 0xFF) {
                _keyboard.resetKeymaps();
                _keyboard.saveKeymaps();
                return;
            }
            if (bytes[0] >= Config::NUM_KEYS) return;
            _keyboard.setKeyAction(0, bytes[0], hidAction(bytes[1], bytes[2]));
            _keyboard.saveKeymaps(0);
            return;
        }

        // Extended packet: [layer][index][type][usage][mods][target][mode].
        if (length == 7 && bytes[0] < Config::MAX_LAYERS && bytes[1] < Config::NUM_KEYS) {
            KeyAction action = {bytes[2], bytes[3], bytes[4], bytes[5], bytes[6]};
            if (_keyboard.setKeyAction(bytes[0], bytes[1], action)) {
                _keyboard.saveKeymaps(bytes[0]);
            }
        }
    }

private:
    MultiKey &_keyboard;
};

} // namespace

void HidKeyboard::onHostConnect() {
    _connected = true;
    _advertising = false;
    _forceReport = true;
}

void HidKeyboard::onHostDisconnect() {
    _connected = false;
    _advertising = true;
    _forceReport = true;
    BLEDevice::startAdvertising();
}

void HidKeyboard::begin(MultiKey &owner) {
    _owner = &owner;

    // Slaves advertise as MK-S<id> so the master can find them.
    char name[32];
    const GlobalSettings &globals = owner.globals();
    if (globals.multikeyEnabled && globals.multikeyId > 0) {
        snprintf(name, sizeof(name), "MK-S%u", globals.multikeyId);
    } else {
        snprintf(name, sizeof(name), "%s", Config::BLE_DEVICE_NAME);
    }

    BLEDevice::init(name);
    BLEDevice::setMTU(256);

    BLESecurity *security = new BLESecurity();
    security->setCapability(ESP_IO_CAP_NONE);
    security->setAuthenticationMode(true, false, true);

    _server = BLEDevice::createServer();
    _server->setCallbacks(new ServerCallbacks(*this));
    _hid = new BLEHIDDevice(_server);
    _hid->manufacturer()->setValue(Config::BLE_MANUFACTURER);
    _hid->pnp(0x02, Config::BLE_VENDOR_ID, Config::BLE_PRODUCT_ID, Config::BLE_VERSION);
    _hid->hidInfo(0x00, 0x01);
    _hid->reportMap(const_cast<uint8_t *>(KEYBOARD_REPORT_MAP), sizeof(KEYBOARD_REPORT_MAP));
    _inputReport = _hid->inputReport(1);
    _inputReportNkro = _hid->inputReport(2);
    _hid->setBatteryLevel(100);

    BLEService *service = _server->createService(Config::KEYMAP_SERVICE_UUID);
    BLECharacteristic *set = service->createCharacteristic(
        Config::KEYMAP_SET_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    set->setCallbacks(new KeymapWriteCallbacks(owner));
    _keymapDump = service->createCharacteristic(Config::KEYMAP_DUMP_UUID,
                                                 BLECharacteristic::PROPERTY_READ);
    updateDumpCharacteristic();

    _hid->startServices();
    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->setAppearance(0x03C1);
    advertising->addServiceUUID(_hid->hidService()->getUUID());
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
    _advertising = true;
}

void HidKeyboard::updateDumpCharacteristic() {
    if (_keymapDump == nullptr || _owner == nullptr) return;

    // Legacy 70-byte dump: two bytes per key for the active layer. Layer
    // actions dump as 00/00; use USB CDC for complete multi-layer data.
    uint8_t dump[Config::NUM_KEYS * 2] = {};
    const uint8_t layer = _owner->activeLayer();
    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        const KeyAction action = _owner->keyAction(layer, index);
        if (action.type == ACTION_HID) {
            dump[index * 2] = action.usage;
            dump[index * 2 + 1] = action.modifiers;
        }
    }
    _keymapDump->setValue(dump, sizeof(dump));
}

void HidKeyboard::buildReport(uint8_t *report, const bool pressed[],
                              const KeyAction latched[]) const {
    memset(report, 0, NKRO_REPORT_SIZE);
    uint8_t count = 0;

    const bool nkro = _rollover == ROLLOVER_NKRO;
    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        if (!pressed[index] || latched[index].type != ACTION_HID) continue;
        const KeyAction &action = latched[index];
        report[0] |= action.modifiers;
        const uint8_t usage = action.usage;
        if (usage == 0) continue;
        if (usage >= 0xE0 && usage <= 0xE7) {
            report[0] |= static_cast<uint8_t>(1U << (usage - 0xE0));
            continue;
        }
        if (nkro) {
            report[2 + usage / 8] |= static_cast<uint8_t>(1U << (usage % 8));
        } else if (count < 6) {
            report[2 + count++] = usage;
        } else {
            // Standard 6KRO rollover error instead of silently dropping keys.
            for (uint8_t slot = 0; slot < 6; ++slot) report[2 + slot] = 0x01;
        }
    }

    // Merge slave keyboards (multikey master).
    if (_owner != nullptr) {
        const LinkBle &link = _owner->link();
        for (uint8_t s = 0; s < Config::MULTIKEY_MAX_SLAVES; ++s) {
            const LinkBle::SlaveState &slave = link.slave(s);
            if (!slave.connected) continue;
            report[0] |= slave.modifiers;
            for (uint8_t i = 0; i < slave.count; ++i) {
                const uint8_t usage = slave.keys[i];
                if (usage >= 0xE0 && usage <= 0xE7) {
                    report[0] |= static_cast<uint8_t>(1U << (usage - 0xE0));
                    continue;
                }
                if (nkro) {
                    if (usage <= 0x7F) report[2 + usage / 8] |= static_cast<uint8_t>(1U << (usage % 8));
                } else if (count < 6) {
                    report[2 + count++] = usage;
                } else {
                    for (uint8_t slot = 0; slot < 6; ++slot) report[2 + slot] = 0x01;
                }
            }
        }
    }
}

void HidKeyboard::sendReport(const uint8_t *report) {
    if (!_connected) return;
    if (_rollover == ROLLOVER_NKRO) {
        if (_inputReportNkro == nullptr) return;
        _inputReportNkro->setValue(const_cast<uint8_t *>(report), NKRO_REPORT_SIZE);
        _inputReportNkro->notify();
        memcpy(_lastReportNkro, report, NKRO_REPORT_SIZE);
    } else {
        if (_inputReport == nullptr) return;
        _inputReport->setValue(const_cast<uint8_t *>(report), BOOT_REPORT_SIZE);
        _inputReport->notify();
        memcpy(_lastReport, report, BOOT_REPORT_SIZE);
    }
}

void HidKeyboard::sendReleaseAll() {
    uint8_t zero[NKRO_REPORT_SIZE] = {};
    sendReport(zero);
}

void HidKeyboard::update(const bool pressed[], const KeyAction latched[]) {
    if (!_connected) return;
    uint8_t report[NKRO_REPORT_SIZE];
    buildReport(report, pressed, latched);
    if (_rollover == ROLLOVER_NKRO) {
        if (!_forceReport && memcmp(report, _lastReportNkro, NKRO_REPORT_SIZE) == 0) return;
    } else {
        if (!_forceReport && memcmp(report, _lastReport, BOOT_REPORT_SIZE) == 0) return;
    }
    _forceReport = false;
    sendReport(report);
}
