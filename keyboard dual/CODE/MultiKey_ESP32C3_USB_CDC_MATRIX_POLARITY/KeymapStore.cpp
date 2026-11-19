#include "KeymapStore.h"
#include <stddef.h>
#include <string.h>

namespace {
const char *slotKey(uint8_t slot) {
    return slot == 0 ? "map_a" : "map_b";
}

const char *LED_KEY = "ledcfg";

struct LedSettingsBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    LedSettings settings;
    uint32_t crc32;
};

uint32_t crc32Of(const uint8_t *bytes, size_t length) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1UL)));
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}
}

bool KeymapStore::begin() {
    return _preferences.begin(Config::NVS_NAMESPACE, false);
}

uint32_t KeymapStore::checksum(const KeymapData &data) const {
    return crc32Of(reinterpret_cast<const uint8_t *>(&data), offsetof(KeymapData, crc32));
}

bool KeymapStore::valid(const KeymapData &data) const {
    if (data.magic != MAGIC || data.version != VERSION ||
        data.layerCount == 0 || data.layerCount > Config::MAX_LAYERS ||
        data.baseLayer >= data.layerCount || data.holdMs == 0 ||
        data.crc32 != checksum(data)) {
        return false;
    }
    for (uint8_t layer = 0; layer < Config::MAX_LAYERS; ++layer) {
        for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
            const KeyAction &action = data.layers[layer][index];
            if (action.type > ACTION_TRANSPARENT) return false;
            if (action.type == ACTION_HID && action.usage > 0xE7) return false;
            if (action.type == ACTION_LAYER &&
                (action.layer >= Config::MAX_LAYERS || action.layerMode > LAYER_MODAL)) {
                return false;
            }
        }
    }
    return true;
}

bool KeymapStore::readSlot(uint8_t slot, KeymapData &out) {
    if (_preferences.getBytesLength(slotKey(slot)) != sizeof(KeymapData)) return false;
    if (_preferences.getBytes(slotKey(slot), &out, sizeof(out)) != sizeof(out)) return false;
    if (!valid(out)) return false;
    for (uint8_t layer = 0; layer < Config::MAX_LAYERS; ++layer) {
        out.layerNames[layer][Config::LAYER_NAME_LENGTH - 1] = '\0';
    }
    return true;
}

bool KeymapStore::load(KeymapData &out) {
    KeymapData candidate;
    bool found = false;
    uint32_t newest = 0;

    for (uint8_t slot = 0; slot < 2; ++slot) {
        if (!readSlot(slot, candidate)) continue;
        if (!found || newer(candidate.generation, newest)) {
            out = candidate;
            newest = candidate.generation;
            _activeSlot = slot;
            found = true;
        }
    }
    return found;
}

bool KeymapStore::save(KeymapData &data) {
    KeymapData previous;
    uint32_t generation = 1;
    if (load(previous)) generation = previous.generation + 1;
    const uint8_t targetSlot = _activeSlot == 0 ? 1 : 0;

    data.magic = MAGIC;
    data.version = VERSION;
    data.reserved = 0;
    data.generation = generation;
    data.crc32 = 0;
    data.crc32 = checksum(data);

    if (_preferences.putBytes(slotKey(targetSlot), &data, sizeof(data)) != sizeof(data)) {
        return false;
    }

    KeymapData verify;
    if (!readSlot(targetSlot, verify) ||
        verify.generation != generation || verify.crc32 != data.crc32 ||
        memcmp(&verify, &data, sizeof(data)) != 0) {
        return false;
    }
    _activeSlot = targetSlot;
    return true;
}

bool KeymapStore::saveLayer(KeymapData &ram, uint8_t layer) {
    if (layer >= Config::MAX_LAYERS) return false;
    KeymapData stored;
    if (!load(stored)) stored = ram;

    memcpy(stored.layers[layer], ram.layers[layer], sizeof(stored.layers[layer]));
    return save(stored);
}

bool KeymapStore::saveLed(const LedSettings &settings) {
    LedSettingsBlob blob;
    blob.magic = LED_MAGIC;
    blob.version = LED_VERSION;
    blob.reserved = 0;
    blob.settings = settings;
    blob.crc32 = crc32Of(reinterpret_cast<const uint8_t *>(&blob),
                         offsetof(LedSettingsBlob, crc32));
    if (_preferences.putBytes(LED_KEY, &blob, sizeof(blob)) != sizeof(blob)) return false;

    LedSettingsBlob verify;
    return _preferences.getBytesLength(LED_KEY) == sizeof(blob) &&
           _preferences.getBytes(LED_KEY, &verify, sizeof(verify)) == sizeof(verify) &&
           verify.crc32 == blob.crc32 &&
           memcmp(&verify, &blob, sizeof(blob)) == 0;
}

bool KeymapStore::loadLed(LedSettings &out) {
    LedSettingsBlob blob;
    if (_preferences.getBytesLength(LED_KEY) != sizeof(blob)) return false;
    if (_preferences.getBytes(LED_KEY, &blob, sizeof(blob)) != sizeof(blob)) return false;
    if (blob.magic != LED_MAGIC || blob.version != LED_VERSION || blob.reserved != 0) return false;
    if (blob.crc32 != crc32Of(reinterpret_cast<const uint8_t *>(&blob),
                              offsetof(LedSettingsBlob, crc32))) return false;
    out = blob.settings;
    return true;
}
