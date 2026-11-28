#include "KeymapStore.h"
#include "src/led/Palettes.h"
#include <stddef.h>
#include <string.h>

namespace {

const char *slotKey(uint8_t slot) {
    return slot == 0 ? "map_a" : "map_b";
}

const char *LED_KEY = "ledcfg";
const char *GLOBAL_KEY = "globals";

void presetKey(uint8_t slot, char *out) {
    out[0] = 'p'; out[1] = 'r'; out[2] = 's'; out[3] = 't';
    out[4] = static_cast<char>('0' + slot);
    out[5] = '\0';
}

void multikeyKey(uint8_t slot, char *out) {
    out[0] = 'm'; out[1] = 'k'; out[2] = 's'; out[3] = 'l';
    out[4] = static_cast<char>('0' + slot);
    out[5] = '\0';
}

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

template <typename Payload>
bool putBlob(Preferences &preferences, const char *key, uint32_t magic,
             uint16_t version, const Payload &payload) {
    struct Blob {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        Payload payload;
        uint32_t crc32;
    } blob;
    blob.magic = magic;
    blob.version = version;
    blob.reserved = 0;
    blob.payload = payload;
    blob.crc32 = crc32Of(reinterpret_cast<const uint8_t *>(&blob),
                         offsetof(Blob, crc32));
    if (preferences.putBytes(key, &blob, sizeof(blob)) != sizeof(blob)) return false;

    Blob verify;
    return preferences.getBytesLength(key) == sizeof(blob) &&
           preferences.getBytes(key, &verify, sizeof(verify)) == sizeof(verify) &&
           verify.crc32 == blob.crc32 &&
           memcmp(&verify, &blob, sizeof(blob)) == 0;
}

template <typename Payload>
bool getBlob(Preferences &preferences, const char *key, uint32_t magic,
             uint16_t version, Payload &out) {
    struct Blob {
        uint32_t magic;
        uint16_t version;
        uint16_t reserved;
        Payload payload;
        uint32_t crc32;
    } blob;
    if (preferences.getBytesLength(key) != sizeof(blob)) return false;
    if (preferences.getBytes(key, &blob, sizeof(blob)) != sizeof(blob)) return false;
    if (blob.magic != magic || blob.version != version || blob.reserved != 0) return false;
    if (blob.crc32 != crc32Of(reinterpret_cast<const uint8_t *>(&blob),
                              offsetof(Blob, crc32))) return false;
    out = blob.payload;
    return true;
}

} // namespace

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
        if (data.layerLed[layer].effect > LED_CUSTOM &&
            data.layerLed[layer].effect != LAYER_LED_FOLLOW_GLOBAL) {
            return false;
        }
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
    for (uint8_t i = 0; i < NUM_PALETTES; ++i) {
        if (data.palettes[i].count > PALETTE_MAX_STOPS) return false;
    }
    return true;
}

bool KeymapStore::upgradeV2(KeymapData &data) const {
    // Version 2 blobs end right before layerLed; sanity-check the old header.
    if (data.magic != MAGIC || data.layerCount == 0 ||
        data.layerCount > Config::MAX_LAYERS || data.baseLayer >= data.layerCount) {
        return false;
    }
    for (uint8_t layer = 0; layer < Config::MAX_LAYERS; ++layer) {
        data.layerLed[layer] = defaultLayerLedSettings();
    }
    initDefaultPalettes(data.palettes);
    data.version = VERSION;
    data.crc32 = checksum(data);
    return true;
}

bool KeymapStore::readSlot(uint8_t slot, KeymapData &out) {
    const size_t length = _preferences.getBytesLength(slotKey(slot));
    if (length != sizeof(KeymapData)) {
        if (length == offsetof(KeymapData, layerLed)) {
            // Migrate a v2 slot in place.
            if (_preferences.getBytes(slotKey(slot), &out, length) != length) return false;
            if (!upgradeV2(out)) return false;
            save(out);
            _activeSlot = slot;
            return true;
        }
        return false;
    }
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

bool KeymapStore::saveLed(const LedGlobalSettings &settings) {
    return putBlob(_preferences, LED_KEY, LED_MAGIC, LED_VERSION, settings);
}

bool KeymapStore::loadLed(LedGlobalSettings &out) {
    return getBlob(_preferences, LED_KEY, LED_MAGIC, LED_VERSION, out);
}

bool KeymapStore::saveGlobal(const GlobalSettings &settings) {
    return putBlob(_preferences, GLOBAL_KEY, GLOBAL_MAGIC, GLOBAL_VERSION, settings);
}

bool KeymapStore::loadGlobal(GlobalSettings &out) {
    return getBlob(_preferences, GLOBAL_KEY, GLOBAL_MAGIC, GLOBAL_VERSION, out);
}

bool KeymapStore::savePreset(uint8_t slot, const PresetBlob &preset) {
    if (slot >= MULTIKEY_SLOT_COUNT) return false;
    char key[6];
    presetKey(slot, key);
    return putBlob(_preferences, key, PRESET_MAGIC, 1, preset);
}

bool KeymapStore::loadPreset(uint8_t slot, PresetBlob &out) {
    if (slot >= MULTIKEY_SLOT_COUNT) return false;
    char key[6];
    presetKey(slot, key);
    return getBlob(_preferences, key, PRESET_MAGIC, 1, out);
}

bool KeymapStore::presetExists(uint8_t slot) const {
    if (slot >= MULTIKEY_SLOT_COUNT) return false;
    char key[6];
    presetKey(slot, key);
    // Blob header is 12 bytes; just probe with a minimal header read.
    // Preferences::getBytes() is non-const, hence the cast.
    struct Header { uint32_t magic; uint16_t version; uint16_t reserved; } header;
    return const_cast<Preferences &>(_preferences)
                   .getBytes(key, &header, sizeof(header)) == sizeof(header) &&
           header.magic == PRESET_MAGIC;
}

bool KeymapStore::saveMultikeySlot(uint8_t slot, const MultikeySlotBlob &blob) {
    if (slot >= MULTIKEY_SLOT_COUNT) return false;
    char key[6];
    multikeyKey(slot, key);
    return putBlob(_preferences, key, MKSLOT_MAGIC, 1, blob);
}

bool KeymapStore::loadMultikeySlot(uint8_t slot, MultikeySlotBlob &out) {
    if (slot >= MULTIKEY_SLOT_COUNT) return false;
    char key[6];
    multikeyKey(slot, key);
    return getBlob(_preferences, key, MKSLOT_MAGIC, 1, out);
}
