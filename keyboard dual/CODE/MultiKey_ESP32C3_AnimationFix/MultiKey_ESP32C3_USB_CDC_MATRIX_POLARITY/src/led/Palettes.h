#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "../../Settings.h"

// Built-in palettes are the initial content of the 16 user palettes stored in
// the keymap; all of them can be edited from the webapp palette editor.

void initDefaultPalettes(Palette *palettes);

// Colour at 0..255 position with crossfade between adjacent stops.
CRGB paletteColor(const Palette &palette, uint8_t position);

// Short display name for a built-in palette (index 0..15).
const char *paletteName(uint8_t index);

// HUE helper: position on the full hue wheel.
inline CRGB hueColor(uint8_t position) {
    return CHSV(position, 255, 255);
}
