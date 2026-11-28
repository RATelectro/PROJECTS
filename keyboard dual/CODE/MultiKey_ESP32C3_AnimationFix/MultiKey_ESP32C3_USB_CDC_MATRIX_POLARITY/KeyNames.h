#pragma once

#include <Arduino.h>
#include "Settings.h"

// Display labels for HID usages used by the on-device key editor.
// RU labels show the Russian layout legend at the same physical position
// (HID usages are layout-independent).

struct KeyNameEntry {
    uint8_t usage;
    const char *en;
    const char *ru;
};

static const KeyNameEntry KEY_NAMES[] = {
    { 0x04, "A", "F" }, { 0x05, "B", "I" }, { 0x06, "C", "C" }, { 0x07, "D", "V" },
    { 0x08, "E", "T" }, { 0x09, "F", "A" }, { 0x0A, "G", "P" }, { 0x0B, "H", "R" },
    { 0x0C, "I", "SH" }, { 0x0D, "J", "O" }, { 0x0E, "K", "L" }, { 0x0F, "L", "D" },
    { 0x10, "M", "X" }, { 0x11, "N", "Y" }, { 0x12, "O", "SHC" }, { 0x13, "P", "Z" },
    { 0x14, "Q", "J" }, { 0x15, "R", "K" }, { 0x16, "S", "Y" }, { 0x17, "T", "E" },
    { 0x18, "U", "G" }, { 0x19, "V", "M" }, { 0x1A, "W", "TS" }, { 0x1B, "X", "CH" },
    { 0x1C, "Y", "N" }, { 0x1D, "Z", "JA" },
    { 0x1E, "1", "1" }, { 0x1F, "2", "2" }, { 0x20, "3", "3" }, { 0x21, "4", "4" },
    { 0x22, "5", "5" }, { 0x23, "6", "6" }, { 0x24, "7", "7" }, { 0x25, "8", "8" },
    { 0x26, "9", "9" }, { 0x27, "0", "0" },
    { 0x28, "ENTER", "ENTER" }, { 0x29, "ESC", "ESC" }, { 0x2A, "BKSP", "BKSP" },
    { 0x2B, "TAB", "TAB" }, { 0x2C, "SPACE", "SPACE" },
    { 0x2D, "-", "-" }, { 0x2E, "=", "=" }, { 0x2F, "[", "X" },
    { 0x30, "]", "]" }, { 0x31, "\\", "\\" }, { 0x33, ";", "ZH" },
    { 0x34, "'", "E" }, { 0x35, "`", "JO" }, { 0x36, ",", "B" },
    { 0x37, ".", "JU" }, { 0x38, "/", "." },
    { 0x39, "CAPS", "CAPS" },
    { 0x3A, "F1", "F1" }, { 0x3B, "F2", "F2" }, { 0x3C, "F3", "F3" }, { 0x3D, "F4", "F4" },
    { 0x3E, "F5", "F5" }, { 0x3F, "F6", "F6" }, { 0x40, "F7", "F7" }, { 0x41, "F8", "F8" },
    { 0x42, "F9", "F9" }, { 0x43, "F10", "F10" }, { 0x44, "F11", "F11" }, { 0x45, "F12", "F12" },
    { 0x46, "PRSC", "PRSC" }, { 0x47, "SCLK", "SCLK" }, { 0x48, "PAUS", "PAUS" },
    { 0x49, "INS", "INS" }, { 0x4A, "HOME", "HOME" }, { 0x4B, "PGUP", "PGUP" },
    { 0x4C, "DEL", "DEL" }, { 0x4D, "END", "END" }, { 0x4E, "PGDN", "PGDN" },
    { 0x4F, "RGHT", "RGHT" }, { 0x50, "LEFT", "LEFT" }, { 0x51, "DOWN", "DOWN" },
    { 0x52, "UP", "UP" },
    { 0x53, "NLCK", "NLCK" }, { 0x54, "KP /", "KP /" }, { 0x55, "KP *", "KP *" },
    { 0x56, "KP -", "KP -" }, { 0x57, "KP +", "KP +" }, { 0x58, "KPEN", "KPEN" },
    { 0x65, "MENU", "MENU" },
    { 0xE0, "LCTL", "LCTL" }, { 0xE1, "LSFT", "LSFT" }, { 0xE2, "LALT", "LALT" },
    { 0xE3, "LGUI", "LGUI" }, { 0xE4, "RCTL", "RCTL" }, { 0xE5, "RSFT", "RSFT" },
    { 0xE6, "RALT", "RALT" }, { 0xE7, "RGUI", "RGUI" },
};

constexpr uint8_t KEY_NAMES_COUNT = sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]);

inline const char *hidKeyName(uint8_t usage, uint8_t lang) {
    for (uint8_t i = 0; i < KEY_NAMES_COUNT; ++i) {
        if (KEY_NAMES[i].usage == usage) {
            return lang == HID_LABEL_RU ? KEY_NAMES[i].ru : KEY_NAMES[i].en;
        }
    }
    return "?";
}
