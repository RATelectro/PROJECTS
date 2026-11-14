#pragma once

#include <Arduino.h>

// HID usages below 0xE0 and modifier usages 0xE0..0xE7 are stored in usage.
enum ActionType : uint8_t {
    ACTION_HID = 0,
    ACTION_LAYER = 1,
    ACTION_TRANSPARENT = 2
};

enum LayerSwitchMode : uint8_t {
    LAYER_MOMENTARY = 0, // Active while the physical key is held.
    LAYER_SWITCH = 1,    // Set the persistent base layer on press.
    LAYER_MODAL = 2      // Toggle target/previous base layer on each press.
};

struct __attribute__((packed)) KeyAction {
    uint8_t type;
    uint8_t usage;
    uint8_t modifiers;
    uint8_t layer;
    uint8_t layerMode;
};

static_assert(sizeof(KeyAction) == 5, "KeyAction wire/storage size changed");

constexpr KeyAction hidAction(uint8_t usage = 0, uint8_t modifiers = 0) {
    return {ACTION_HID, usage, modifiers, 0, 0};
}

constexpr KeyAction layerAction(uint8_t layer, LayerSwitchMode mode = LAYER_MOMENTARY) {
    return {ACTION_LAYER, 0, 0, layer, static_cast<uint8_t>(mode)};
}

constexpr KeyAction transparentAction() {
    return {ACTION_TRANSPARENT, 0, 0, 0, 0};
}
