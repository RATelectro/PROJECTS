#pragma once

#include <Preferences.h>
#include "KeyboardConfig.h"
#include "KeyAction.h"

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
  uint32_t crc32;
};

class KeymapStore {
public:
  static constexpr uint32_t MAGIC = 0x4D4B4D32UL;  // "MKM2"
  static constexpr uint16_t VERSION = 2;

  bool begin();
  bool load(KeymapData &out);
  bool save(KeymapData &data);
  bool saveLayer(KeymapData &ram, uint8_t layer);

private:
  bool readSlot(uint8_t slot, KeymapData &out);
  bool valid(const KeymapData &data) const;
  uint32_t checksum(const KeymapData &data) const;
  static bool newer(uint32_t a, uint32_t b) {
    return static_cast<int32_t>(a - b) > 0;
  }

  Preferences _preferences;
  int8_t _activeSlot = -1;
};
