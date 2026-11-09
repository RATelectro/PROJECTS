#pragma once

#include "KeyboardConfig.h"
#include "KeyAction.h"

// Edit this file to change compiled defaults. Higher layers are transparent
// by default and therefore fall through toward layer 0.
extern const KeyAction DEFAULT_BASE_KEYMAP[Config::NUM_KEYS];
extern const char DEFAULT_LAYER_NAMES[Config::MAX_LAYERS][Config::LAYER_NAME_LENGTH];

void loadDefaultKeymaps(KeyAction maps[Config::MAX_LAYERS][Config::NUM_KEYS]);
