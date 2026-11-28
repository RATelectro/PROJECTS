#include "LinkBle.h"
#include "../../Keyboard.h"
#include "../../HidKeyboard.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <esp_bt.h>

namespace {

// Link protocol opcodes.
constexpr uint8_t OP_KEY = 0x4B;        // slave -> master: key event
constexpr uint8_t OP_CFG_CHUNK = 0x63;  // 'c' master -> slave: config chunk
constexpr uint8_t OP_SAVE_CHUNK = 0x73; // 's' slave -> master: config save
constexpr uint8_t CHUNK_PAYLOAD = 48;

constexpr uint16_t CONFIG_BYTES = sizeof(GlobalSettings) + sizeof(LedGlobalSettings);

esp_power_level_t powerLevel(uint8_t index) {
    static const esp_power_level_t levels[8] = {
        ESP_PWR_LVL_N12, ESP_PWR_LVL_N9, ESP_PWR_LVL_N6, ESP_PWR_LVL_N3,
        ESP_PWR_LVL_N0, ESP_PWR_LVL_P3, ESP_PWR_LVL_P6, ESP_PWR_LVL_P9 };
    return levels[index > 7 ? 4 : index];
}

} // namespace

// ---------------------------------------------------------------------------
// BLE callback adapters

class LinkClientCallbacks : public BLEClientCallbacks {
public:
    explicit LinkClientCallbacks(LinkBle &link) : _link(link) {}
    void onConnect(BLEClient *client) override { _link.onClientConnect(client); }
    void onDisconnect(BLEClient *client) override { _link.onClientDisconnect(client); }
private:
    LinkBle &_link;
};

class LinkScanCallbacks : public BLEAdvertisedDeviceCallbacks {
public:
    explicit LinkScanCallbacks(LinkBle &link) : _link(link) {}
    void onResult(BLEAdvertisedDevice device) override { _link.onScanResult(device); }
private:
    LinkBle &_link;
};

class LinkRxWriteCallbacks : public BLECharacteristicCallbacks {
public:
    explicit LinkRxWriteCallbacks(LinkBle &link) : _link(link) {}
    void onWrite(BLECharacteristic *characteristic) override {
        const String value = characteristic->getValue();
        _link.onLinkRxWrite(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    }
private:
    LinkBle &_link;
};

class ComRxWriteCallbacks : public BLECharacteristicCallbacks {
public:
    explicit ComRxWriteCallbacks(LinkBle &link) : _link(link) {}
    void onWrite(BLECharacteristic *characteristic) override {
        const String value = characteristic->getValue();
        _link.onComRxWrite(reinterpret_cast<const uint8_t *>(value.c_str()), value.length());
    }
private:
    LinkBle &_link;
};

class ComTxSubscribeCallbacks : public BLECharacteristicCallbacks {
public:
    explicit ComTxSubscribeCallbacks(LinkBle &link) : _link(link) {}
    void onSubscribe(BLECharacteristic *, ble_gap_conn_desc *desc, uint16_t subValue) override {
        _link.onComSubscribe(subValue != 0 && desc != nullptr);
    }
private:
    LinkBle &_link;
};

// ---------------------------------------------------------------------------

void LinkBle::begin(MultiKey &keyboard) {
    _kbd = &keyboard;
    memset(_slaves, 0, sizeof(_slaves));

    BLEServer *server = _kbd->hid().server();
    if (server == nullptr) return;

    // Slave-side link service (master connects to it) plus the COM port.
    BLEService *link = server->createService(Config::LINK_SERVICE_UUID);
    _linkTx = link->createCharacteristic(Config::LINK_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    BLECharacteristic *rx = link->createCharacteristic(Config::LINK_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    rx->setCallbacks(new LinkRxWriteCallbacks(*this));

    BLEService *com = server->createService(Config::COM_SERVICE_UUID);
    _comTx = com->createCharacteristic(Config::COM_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY);
    _comTx->setCallbacks(new ComTxSubscribeCallbacks(*this));
    BLECharacteristic *comRx = com->createCharacteristic(Config::COM_RX_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    comRx->setCallbacks(new ComRxWriteCallbacks(*this));

    link->start();
    com->start();

    applySettings();
}

void LinkBle::applySettings() {
    if (_kbd == nullptr) return;
    const GlobalSettings &g = _kbd->globals();
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, powerLevel(g.btTxPower));

    if (isMaster()) {
        _lastScanMs = millis() - 3000; // scan soon
    }
}

bool LinkBle::isMaster() const {
    return _kbd != nullptr && _kbd->globals().multikeyEnabled &&
           _kbd->globals().multikeyId == 0;
}

uint8_t LinkBle::connectedSlaves() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < Config::MULTIKEY_MAX_SLAVES; ++i) {
        if (_slaves[i].connected) ++count;
    }
    return count;
}

void LinkBle::update() {
    if (_kbd == nullptr || !isMaster()) return;
    // Keep looking for unconnected slaves.
    bool freeSlot = false;
    for (uint8_t i = 0; i < Config::MULTIKEY_MAX_SLAVES; ++i) {
        if (!_slaves[i].connected && _clients[i] == nullptr) { freeSlot = true; break; }
    }
    if (!freeSlot) return;
    const uint32_t now = millis();
    if (_scanning) {
        if (static_cast<uint32_t>(now - _lastScanMs) > 1500) {
            BLEDevice::getScan()->stop();
            _scanning = false;
            _lastScanMs = now;
        }
        return;
    }
    if (static_cast<uint32_t>(now - _lastScanMs) > 4000) {
        BLEScan *scan = BLEDevice::getScan();
        scan->setAdvertisedDeviceCallbacks(new LinkScanCallbacks(*this), true);
        scan->setActiveScan(false);
        scan->start(1500, false);
        _scanning = true;
        _lastScanMs = now;
    }
}

// ---------------------------------------------------------------------------
// Master side

void LinkBle::onScanResult(BLEAdvertisedDevice &device) {
    if (!device.haveName()) return;
    const String name = device.getName().c_str();
    if (!name.startsWith("MK-S")) return;
    const int id = name.substring(4).toInt();
    if (id < 1 || id > Config::MULTIKEY_MAX_SLAVES) return;
    const uint8_t index = static_cast<uint8_t>(id - 1);
    if (_slaves[index].connected || _clients[index] != nullptr || _connecting[index]) return;
    _connecting[index] = true;
    BLEDevice::getScan()->stop();
    _scanning = false;

    BLEClient *client = BLEDevice::createClient();
    client->setClientCallbacks(new LinkClientCallbacks(*this));
    if (!client->connect(&device)) {
        _connecting[index] = false;
        client->disconnect();
        return;
    }
    BLERemoteService *service = client->getService(Config::LINK_SERVICE_UUID);
    if (service == nullptr) {
        client->disconnect();
        _connecting[index] = false;
        return;
    }
    BLERemoteCharacteristic *tx = service->getCharacteristic(Config::LINK_TX_UUID);
    BLERemoteCharacteristic *rx = service->getCharacteristic(Config::LINK_RX_UUID);
    if (tx == nullptr || rx == nullptr || !tx->canNotify()) {
        client->disconnect();
        _connecting[index] = false;
        return;
    }
    _clients[index] = client;
    _remoteTx[index] = tx;
    _remoteRx[index] = rx;
    _connecting[index] = false;
    tx->subscribe(true, [this, index](BLERemoteCharacteristic *,
                                      uint8_t *data, size_t length, bool) {
        onSlaveNotify(index, data, length);
    });
}

void LinkBle::onClientConnect(BLEClient *client) {
    for (uint8_t i = 0; i < Config::MULTIKEY_MAX_SLAVES; ++i) {
        if (_clients[i] == client) {
            memset(&_slaves[i], 0, sizeof(SlaveState));
            _slaves[i].connected = true;
        }
    }
}

void LinkBle::onClientDisconnect(BLEClient *client) {
    for (uint8_t i = 0; i < Config::MULTIKEY_MAX_SLAVES; ++i) {
        if (_clients[i] == client) {
            _clients[i] = nullptr;
            _remoteTx[i] = nullptr;
            _remoteRx[i] = nullptr;
            memset(&_slaves[i], 0, sizeof(SlaveState));
        }
    }
}

void LinkBle::onSlaveNotify(uint8_t index, const uint8_t *data, size_t length) {
    if (index >= Config::MULTIKEY_MAX_SLAVES || length < 1) return;

    if (data[0] == OP_KEY && length >= 4) {
        SlaveState &s = _slaves[index];
        const uint8_t usage = data[1];
        const uint8_t mods = data[2];
        const bool pressed = data[3] != 0;
        if (usage >= 0xE0 && usage <= 0xE7) {
            const uint8_t bit = static_cast<uint8_t>(1U << (usage - 0xE0));
            if (pressed) s.modifiers |= bit;
            else s.modifiers &= static_cast<uint8_t>(~bit);
        } else if (pressed) {
            bool present = false;
            for (uint8_t i = 0; i < s.count; ++i) present |= s.keys[i] == usage;
            if (!present && s.count < sizeof(s.keys)) s.keys[s.count++] = usage;
            (void)mods; // modifier bits travel as their own events
        } else {
            for (uint8_t i = 0; i < s.count; ++i) {
                if (s.keys[i] == usage) {
                    memmove(&s.keys[i], &s.keys[i + 1], s.count - i - 1);
                    --s.count;
                    break;
                }
            }
        }
        if (_kbd != nullptr) _kbd->hid().requestReport();
        return;
    }

    if (data[0] == OP_SAVE_CHUNK && length >= 2) {
        // Reassemble a MultikeySlotBlob from 48-byte chunks.
        static uint8_t rxBuffer[sizeof(MultikeySlotBlob)];
        static int16_t rxOffset = -1;
        const uint8_t seq = data[1];
        const size_t payload = length - 2;
        if (seq == 0) rxOffset = 0;
        if (rxOffset < 0 || static_cast<size_t>(seq) * CHUNK_PAYLOAD != static_cast<size_t>(rxOffset)) {
            rxOffset = -1;
            return;
        }
        if (rxOffset + payload > sizeof(rxBuffer)) { rxOffset = -1; return; }
        memcpy(rxBuffer + rxOffset, data + 2, payload);
        rxOffset += static_cast<int16_t>(payload);
        if (rxOffset >= static_cast<int16_t>(sizeof(MultikeySlotBlob))) {
            rxOffset = -1;
            if (_kbd != nullptr && _kbd->globals().saveSlaveChanges) {
                MultikeySlotBlob blob;
                memcpy(&blob, rxBuffer, sizeof(blob));
                _kbd->storeMultikeyBlob(static_cast<uint8_t>(index + 1), blob);
            }
        }
    }
}

void LinkBle::sendToSlave(uint8_t index, const uint8_t *data, size_t length) {
    if (index >= Config::MULTIKEY_MAX_SLAVES || _remoteRx[index] == nullptr) return;
    _remoteRx[index]->writeValue(const_cast<uint8_t *>(data), length);
}

void LinkBle::pushConfig() {
    if (_kbd == nullptr || !isMaster()) return;
    uint8_t buffer[2 + CHUNK_PAYLOAD];
    uint8_t payload[CONFIG_BYTES];
    const GlobalSettings &g = _kbd->globals();
    const LedGlobalSettings led = _kbd->led().captureSettings();
    memcpy(payload, &g, sizeof(GlobalSettings));
    memcpy(payload + sizeof(GlobalSettings), &led, sizeof(LedGlobalSettings));

    for (uint8_t index = 0; index < Config::MULTIKEY_MAX_SLAVES; ++index) {
        if (!_slaves[index].connected) continue;
        for (uint16_t offset = 0; offset < CONFIG_BYTES; offset += CHUNK_PAYLOAD) {
            const size_t chunk = (CONFIG_BYTES - offset) < CHUNK_PAYLOAD
                ? (CONFIG_BYTES - offset) : CHUNK_PAYLOAD;
            buffer[0] = OP_CFG_CHUNK;
            buffer[1] = static_cast<uint8_t>(offset / CHUNK_PAYLOAD);
            memcpy(buffer + 2, payload + offset, chunk);
            sendToSlave(index, buffer, chunk + 2);
            delay(15); // let the slave store between chunks
        }
    }
}

// ---------------------------------------------------------------------------
// Slave side

void LinkBle::onKeyEvent(uint8_t usage, uint8_t modifiers, bool pressed) {
    if (_kbd == nullptr || !isSlave() || _linkTx == nullptr) return;
    BLEServer *server = BLEDevice::getServer();
    if (server == nullptr || server->getConnectedCount() == 0) return;
    const uint8_t packet[4] = { OP_KEY, usage, modifiers, pressed ? 1 : 0 };
    _linkTx->setValue(const_cast<uint8_t *>(packet), sizeof(packet));
    _linkTx->notify();
}

void LinkBle::onLinkRxWrite(const uint8_t *data, size_t length) {
    if (_kbd == nullptr || length < 2) return;
    if (data[0] != OP_CFG_CHUNK) return;

    static uint8_t rxBuffer[CONFIG_BYTES];
    static int16_t rxOffset = -1;
    const uint8_t seq = data[1];
    const size_t payload = length - 2;
    if (seq == 0) rxOffset = 0;
    if (rxOffset < 0 || static_cast<size_t>(seq) * CHUNK_PAYLOAD != static_cast<size_t>(rxOffset)) {
        rxOffset = -1;
        return;
    }
    if (rxOffset + payload > sizeof(rxBuffer)) { rxOffset = -1; return; }
    memcpy(rxBuffer + rxOffset, data + 2, payload);
    rxOffset += static_cast<int16_t>(payload);
    if (rxOffset >= static_cast<int16_t>(CONFIG_BYTES)) {
        rxOffset = -1;
        GlobalSettings g;
        LedGlobalSettings led;
        memcpy(&g, rxBuffer, sizeof(GlobalSettings));
        memcpy(&led, rxBuffer + sizeof(GlobalSettings), sizeof(LedGlobalSettings));
        _kbd->applyReceivedConfig(g, led);
    }
}

// ---------------------------------------------------------------------------
// COM port bridge

void LinkBle::onComSubscribe(bool subscribed) {
    _comSubscribed = subscribed;
}

bool LinkBle::comActive() const {
    return _comSubscribed;
}

void LinkBle::comWrite(const uint8_t *data, size_t length) {
    if (!_comSubscribed || _comTx == nullptr) return;
    size_t offset = 0;
    while (offset < length) {
        const size_t chunk = length - offset > 200 ? 200 : length - offset;
        _comTx->setValue(const_cast<uint8_t *>(data + offset), chunk);
        _comTx->notify();
        offset += chunk;
    }
}

void LinkBle::onComRxWrite(const uint8_t *data, size_t length) {
    if (_kbd == nullptr) return;
    for (size_t i = 0; i < length; ++i) {
        const char ch = static_cast<char>(data[i]);
        if (ch == '\r' || ch == '\n') {
            if (_comLength > 0) {
                _comLine[_comLength] = '\0';
                _kbd->processSerialCommand(_comLine);
                _comLength = 0;
                _comLine[0] = '\0';
            }
            continue;
        }
        if (_comLength + 1 >= sizeof(_comLine)) {
            _comLength = 0;
            continue;
        }
        _comLine[_comLength++] = ch;
    }
}
