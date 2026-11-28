#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "KeyboardConfig.h"

// ---------------------------------------------------------------------------
// Shared packed settings structs (LED globals, device globals, palettes,
// per-layer LED). All of them are CRC-blob stored through KeymapStore or
// embedded into KeymapData, so sizes are compile-time checked.
// ---------------------------------------------------------------------------

enum LedEffect : uint8_t {
    LED_OFF = 0,
    LED_STATIC = 1,
    LED_RAINBOW = 2,
    LED_BREATHE = 3,
    LED_CUSTOM = 4
};

enum KeypressAnimation : uint8_t {
    KEYPRESS_NONE = 0,
    KEYPRESS_PULSE = 1,
    KEYPRESS_SPARKLE = 2,
    KEYPRESS_RIPPLE = 3
};

enum SleepAnimation : uint8_t {
    SLEEP_OFF = 0,
    SLEEP_BREATHE = 1,
    SLEEP_RAINBOW = 2
};

enum RolloverMode : uint8_t {
    ROLLOVER_6KRO = 0,
    ROLLOVER_NKRO = 1
};

enum UsbMode : uint8_t {
    USB_MODE_HID = 0, // USB presents a HID keyboard (TinyUSB, see MULTIKEY_ENABLE_USB_HID)
    USB_MODE_CFG = 1  // USB is the config/flashing CDC port (default)
};

enum BtMode : uint8_t {
    BT_MODE_HID = 0, // BLE HID keyboard (default)
    BT_MODE_COM = 1  // BLE COM port (virtual UART service bridged to the CLI)
};

enum HidLabelLang : uint8_t {
    HID_LABEL_EN = 0,
    HID_LABEL_RU = 1
};

enum LedMixMode : uint8_t {
    MIX_ADD_NORM = 0, // add + proportional scale: keeps hue, never clips to white
    MIX_ADD = 1,      // saturating add
    MIX_MAX = 2,      // channelwise max
    MIX_SCREEN = 3,   // screen blend: brightens smoothly, avoids flat white
    MIX_HUE = 4       // hue of the brighter colour, value = combined brightness
};

// Maximum number of colour stops inside one palette.
constexpr uint8_t PALETTE_MAX_STOPS = 8;
constexpr uint8_t NUM_PALETTES = 16;

struct __attribute__((packed)) Palette {
    uint8_t count;                     // 2..PALETTE_MAX_STOPS, 0 = unused
    uint8_t color[PALETTE_MAX_STOPS][3];
};

static_assert(sizeof(Palette) == 25, "Palette wire/storage size changed");

// Per-layer LED settings; embedded into KeymapData so they travel with a keymap
// (presets and dumps include them). effect == 255 means "follow the global
// effect", otherwise it overrides it for this layer.
constexpr uint8_t LAYER_LED_FOLLOW_GLOBAL = 255;

struct __attribute__((packed)) LayerLedSettings {
    uint8_t brightness; // 0..255 layer brightness (multiplied with global)
    uint8_t effect;     // LedEffect or LAYER_LED_FOLLOW_GLOBAL
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
};

static_assert(sizeof(LayerLedSettings) == 5, "LayerLedSettings size changed");

// Global (device-wide) LED settings. Stored as its own CRC blob in NVS and
// also inside the keyboard preset slots.
struct __attribute__((packed)) LedGlobalSettings {
    uint8_t enabled;
    uint8_t brightness;      // global total multiplier 0..255 (nonlinear fader in UI)
    uint8_t effect;          // LedEffect
    uint8_t effectSpeed;     // 1..255 (128 = 1x)
    uint8_t keypressAnimation;
    uint8_t keypressSpeed;   // 1..255
    uint8_t sleepAnimation;
    uint8_t sleepSpeed;      // 1..255
    uint32_t sleepTimeoutMs; // 0 = never sleep
    uint8_t bleIndicators;
    uint8_t mixMode;         // LedMixMode used to stack animations
    uint8_t colorR;          // default/static colour
    uint8_t colorG;
    uint8_t colorB;
    uint8_t backlightBrightness; // idle-key glow, 0 = off
    uint8_t backlightR;
    uint8_t backlightG;
    uint8_t backlightB;
    uint8_t layerFlashEnabled;
    uint8_t nextLayerColorR; // flash colour when moving to a higher layer
    uint8_t nextLayerColorG;
    uint8_t nextLayerColorB;
    uint8_t prevLayerColorR; // flash colour when moving to a lower layer
    uint8_t prevLayerColorG;
    uint8_t prevLayerColorB;
    uint16_t layerFlashFadeMs;
    uint8_t custom[Config::NUM_KEYS][3]; // per-key colours for LED_CUSTOM
};

static_assert(sizeof(LedGlobalSettings) == 135, "LedGlobalSettings size changed");

// Device-wide (non-LED) settings: HID/connection, multikey, OLED behaviour.
struct __attribute__((packed)) GlobalSettings {
    uint8_t rollover;            // RolloverMode
    uint8_t usbMode;             // UsbMode
    uint8_t btMode;              // BtMode
    uint8_t btTxPower;           // 0..7 index into esp power table (see LinkBle)
    uint8_t multikeyEnabled;
    uint8_t multikeyId;          // 0 = master (keyboard_0), 1..5 = slave
    uint8_t multikeyLayoutMode;  // remembered for the webapp view
    uint8_t saveSlaveChanges;    // slave pushes local changes to the master slot
    uint8_t showAllKeyboards;    // webapp main-view mode (survives dump/load)
    uint8_t hidLang;             // HidLabelLang for on-device key labels
    uint32_t oledSleepTimeoutMs; // 0 = display never sleeps
    uint8_t oledNavKeys[5];      // up, right, left, down, enter (matrix indices)
    uint8_t bootMenuEnabled;
    uint16_t bootMenuTimeMs;     // bootup chooser show time, min 1000
    uint16_t menuNavBrightness;  // LED brightness of the menu nav keys (direct 0..255, default 128)
};

static_assert(sizeof(GlobalSettings) == 24, "GlobalSettings size changed");

// One preset slot: the complete global configuration of the keyboard.
struct __attribute__((packed)) PresetBlob {
    LedGlobalSettings led;
    GlobalSettings global;
};

static_assert(sizeof(PresetBlob) == 159, "PresetBlob size changed");

// One multikey configuration slot on the master keyboard. The multikey config
// is the bundle of global settings shared across the keyboard group; the
// keymaps themselves stay on each keyboard. Compact (168 B) so all
// MULTIKEY_SLOT_COUNT slots fit NVS even with the default partition.
struct __attribute__((packed)) MultikeySlotBlob {
    char name[8];
    PresetBlob config;
};

static_assert(sizeof(MultikeySlotBlob) == 167, "MultikeySlotBlob size changed");

// Defaults -------------------------------------------------------------------

inline LedGlobalSettings defaultLedGlobalSettings() {
    LedGlobalSettings s = {};
    s.enabled = 1;
    s.brightness = Config::DEFAULT_BRIGHTNESS;
    s.effect = LED_STATIC;
    s.effectSpeed = 128;
    s.keypressAnimation = KEYPRESS_PULSE;
    s.keypressSpeed = 128;
    s.sleepAnimation = SLEEP_BREATHE;
    s.sleepSpeed = 128;
    s.sleepTimeoutMs = Config::DEFAULT_SLEEP_TIMEOUT_MS;
    s.bleIndicators = 1;
    s.mixMode = MIX_ADD_NORM;
    s.colorR = 0; s.colorG = 20; s.colorB = 0;
    s.backlightBrightness = 0;
    s.backlightR = 0; s.backlightG = 60; s.backlightB = 40;
    s.layerFlashEnabled = 1;
    s.nextLayerColorR = 0; s.nextLayerColorG = 160; s.nextLayerColorB = 0;
    s.prevLayerColorR = 160; s.prevLayerColorG = 60; s.prevLayerColorB = 0;
    s.layerFlashFadeMs = 700;
    return s;
}

inline GlobalSettings defaultGlobalSettings() {
    GlobalSettings s = {};
    s.rollover = ROLLOVER_6KRO;
    s.usbMode = USB_MODE_CFG;
    s.btMode = BT_MODE_HID;
    s.btTxPower = 3; // ~0 dBm
    s.multikeyEnabled = 0;
    s.multikeyId = 0;
    s.multikeyLayoutMode = 0;
    s.saveSlaveChanges = 1;
    s.showAllKeyboards = 0;
    s.hidLang = HID_LABEL_EN;
    s.oledSleepTimeoutMs = 0;
    // Matrix indices mapped from the physical key numbering on the schematic
    // (picture cols 1..7 = ROW_PINS, rows 1..5 = COL_PINS, keys 1..35
    // left-to-right top-to-bottom): up=11, left=17, right=19, down=25,
    // enter=18 in picture notation.
    s.oledNavKeys[0] = 16; // up    (picture key 11)
    s.oledNavKeys[1] = 12; // right (picture key 17) - swapped: index 1 = RIGHT
    s.oledNavKeys[2] = 22; // left  (picture key 19) - swapped: index 2 = LEFT
    s.oledNavKeys[3] = 18; // down  (picture key 25)
    s.oledNavKeys[4] = 17; // enter (picture key 18)
    s.bootMenuEnabled = 1;
    s.bootMenuTimeMs = 2000;
    s.menuNavBrightness = 128;
    return s;
}

inline LayerLedSettings defaultLayerLedSettings() {
    LayerLedSettings s = {};
    s.brightness = 255;
    s.effect = LAYER_LED_FOLLOW_GLOBAL;
    s.colorR = 0; s.colorG = 0; s.colorB = 0; // 0,0,0 = follow global colour
    return s;
}

// Nonlinear (gamma) brightness mapping used by menus and the webapp fader:
// perceptually even steps, returns 0..255. Quadratic curve: precise at the
// low end (where control matters), reaches full scale at the top.
inline uint8_t gammaBrightness(uint8_t value) {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * value + 127) / 255);
}
