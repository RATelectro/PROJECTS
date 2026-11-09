#pragma once

#include <Arduino.h>

namespace Config {

constexpr uint8_t ROWS = 7;
constexpr uint8_t COLS = 5;
constexpr uint8_t NUM_KEYS = ROWS * COLS;
constexpr uint8_t MAX_LAYERS = 8;
constexpr uint8_t DEFAULT_LAYER_COUNT = 4;
constexpr uint8_t LAYER_NAME_LENGTH = 16;

extern const uint8_t ROW_PINS[ROWS];
extern const uint8_t COL_PINS[COLS];

constexpr uint8_t LED_PIN = 4;
constexpr uint8_t DEFAULT_BRIGHTNESS = 50;
constexpr uint32_t LED_UPDATE_INTERVAL_MS = 30;
constexpr uint32_t HELLO_SWEEP_SPEED_MS = 100;

constexpr uint8_t MATRIX_PRESSED_LEVEL = HIGH;
constexpr uint8_t MATRIX_ROW_ACTIVE_LEVEL = HIGH;
constexpr uint8_t MATRIX_ROW_INACTIVE_LEVEL = LOW;
constexpr uint32_t ROW_SETTLE_US = 30;
constexpr uint32_t DEBOUNCE_MS = 5;

#ifndef HOLD_DUR_MS
#define HOLD_DUR_MS 500
#endif

constexpr uint32_t DEFAULT_SLEEP_TIMEOUT_MS = 0; // 0 disables LED sleep.

#ifndef MULTIKEY_USE_NKRO
#define MULTIKEY_USE_NKRO 0
#endif
constexpr uint8_t USE_NKRO = MULTIKEY_USE_NKRO;
constexpr const char *BLE_DEVICE_NAME = "MultiKey C3 Keyboard";
constexpr const char *BLE_MANUFACTURER = "Custom MultiKey";
constexpr uint16_t BLE_VENDOR_ID = 0x1209;
constexpr uint16_t BLE_PRODUCT_ID = 0xC301;
constexpr uint16_t BLE_VERSION = 0x0100;

constexpr const char *NVS_NAMESPACE = "multikey";
constexpr const char *KEYMAP_SERVICE_UUID = "7A0B0001-3F45-4E5A-8C42-00A100000001";
constexpr const char *KEYMAP_SET_UUID = "7A0B0002-3F45-4E5A-8C42-00A100000001";
constexpr const char *KEYMAP_DUMP_UUID = "7A0B0003-3F45-4E5A-8C42-00A100000001";

constexpr const char *FIRMWARE_VERSION = "2.0.0";
constexpr uint16_t SERIAL_PROTOCOL_VERSION = 1;

} // namespace Config
