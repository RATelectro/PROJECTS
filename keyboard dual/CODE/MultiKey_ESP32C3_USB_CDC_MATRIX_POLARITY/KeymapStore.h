#pragma once

#include <Preferences.h>
#include "KeyboardConfig.h"
#include "KeyAction.h"
#include "Settings.h"

struct KeymapData {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  uint32_t generation;
  uint8_t layerCount;
  uint8_t baseLayer;
  uint16_t holdMs;
  char layerNames[Config::MAX_LAYERS][Config::LAYER_NAME_LENGTH];
  KeyAction layers[Config::MAX_LAYERS][Config::NUM_KEYS];
  LayerLedSettings layerLed[Config::MAX_LAYERS];
  Palette palettes[NUM_PALETTES];
  uint32_t crc32;
};

class KeymapStore {
public:
  static constexpr uint32_t MAGIC = 0x4D4B4D32UL;  // "MKM2"
  static constexpr uint16_t VERSION = 3;

  bool begin();
  bool load(KeymapData &out);
  bool save(KeymapData &data);
  bool saveLayer(KeymapData &ram, uint8_t layer);

  // LED settings persistence: separate CRC-checked blob, independent of the
  // keymap slots so the two can evolve (and be reset) independently.
  bool saveLed(const LedGlobalSettings &settings);
  bool loadLed(LedGlobalSettings &out);

  // Device-wide global settings (rollover, USB/BT mode, multikey, OLED...).
  bool saveGlobal(const GlobalSettings &settings);
  bool loadGlobal(GlobalSettings &out);

  // Keyboard preset slots: full global configuration snapshots.
  bool savePreset(uint8_t slot, const PresetBlob &preset);
  bool loadPreset(uint8_t slot, PresetBlob &out);
  bool presetExists(uint8_t slot) const;

  // Multikey configuration slots on the master keyboard.
  bool saveMultikeySlot(uint8_t slot, const MultikeySlotBlob &blob);
  bool loadMultikeySlot(uint8_t slot, MultikeySlotBlob &out);

private:
  bool readSlot(uint8_t slot, KeymapData &out);
  bool valid(const KeymapData &data) const;
  uint32_t checksum(const KeymapData &data) const;
  bool upgradeV2(KeymapData &data) const;

  static constexpr uint32_t LED_MAGIC = 0x4D4B4C32UL;  // "MKL2"
  static constexpr uint16_t LED_VERSION = 2;
  static constexpr uint32_t GLOBAL_MAGIC = 0x4D4B4731UL;  // "MKG1"
  // v2: nav-key defaults were remapped to the physical key notation and the
  // menu nav brightness default rose 30 -> 128; v1 blobs are discarded.
  static constexpr uint16_t GLOBAL_VERSION = 2;
  static constexpr uint32_t PRESET_MAGIC = 0x4D4B5031UL;  // "MKP1"
  static constexpr uint32_t MKSLOT_MAGIC = 0x4D4B5331UL;  // "MKS1"
  static bool newer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
  }

  Preferences _preferences;
  int8_t _activeSlot = -1;
};
