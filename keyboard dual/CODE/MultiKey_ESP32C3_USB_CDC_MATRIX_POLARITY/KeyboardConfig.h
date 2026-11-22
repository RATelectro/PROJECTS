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

constexpr uint8_t LED_PIN = 2;
constexpr uint8_t DEFAULT_BRIGHTNESS = 50;
constexpr uint8_t LED_FPS = 60;
constexpr uint32_t HELLO_SWEEP_SPEED_MS = 45; // hello sweep totals ~1 s

// SSD1306 OLED. SDA/SCL share matrix rows 3/4; the display driver bit-bangs
// I2C with plain digitalWrite() on open-drain pins, so the hardware I2C
// peripheral is never attached and the matrix scanner keeps full control of
// the pins between display chunks (open-drain HIGH = released = active row
// via the module's pull-ups, LOW = inactive row).
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_I2C_ADDR = 0x3C;
constexpr uint8_t OLED_PIN_SDA = 8; // shared with ROW_PINS[3]
constexpr uint8_t OLED_PIN_SCL = 7; // shared with ROW_PINS[4]
constexpr uint16_t OLED_CHUNK_BYTES = 128; // sent per update() slice
// Rotate the framebuffer 180 degrees (panel mounted with the yellow band at
// the bottom). 1 = rotated, 0 = normal.
#ifndef DISPLAY_ROTATE_180
#define DISPLAY_ROTATE_180 1
#endif

// Multikey configuration slots stored on the master (NVS data, not program
// space). Set to 1 if the build does not fit FLASH.
#ifndef MULTIKEY_SLOT_COUNT
#define MULTIKEY_SLOT_COUNT 6
#endif
constexpr uint8_t MULTIKEY_MAX_SLAVES = 5;

// Games compiled into the firmware (each lives in games/ and reports its
// estimated flash cost in a comment at the top of its file).
#ifndef GAME_SNAKE
#define GAME_SNAKE 1
#endif
#ifndef GAME_LIGHTS_OUT
#define GAME_LIGHTS_OUT 1
#endif
#ifndef GAME_SIMON
#define GAME_SIMON 1
#endif
#ifndef GAME_PONG
#define GAME_PONG 1
#endif
#ifndef GAME_REACTION
#define GAME_REACTION 1
#endif
#ifndef GAME_WHACK_A_MOLE
#define GAME_WHACK_A_MOLE 1
#endif

// USB HID over the native USB port. Requires "USB Mode: OTG" (TinyUSB);
// with the default Hardware CDC build the setting is stored but reports
// still travel over BLE HID.
#ifndef MULTIKEY_ENABLE_USB_HID
#define MULTIKEY_ENABLE_USB_HID 1
#endif

constexpr uint8_t MATRIX_PRESSED_LEVEL = HIGH;
constexpr uint8_t MATRIX_ROW_ACTIVE_LEVEL = HIGH;
constexpr uint8_t MATRIX_ROW_INACTIVE_LEVEL = LOW;
constexpr uint32_t ROW_SETTLE_US = 30;
constexpr uint32_t DEBOUNCE_MS = 5;

#ifndef HOLD_DUR_MS
#define HOLD_DUR_MS 500
#endif

constexpr uint32_t DEFAULT_SLEEP_TIMEOUT_MS = 60000; // LED sleep after 1 min idle.

constexpr uint32_t LED_SETTINGS_SAVE_DELAY_MS = 2000; // quiet time before LED settings are written to NVS
constexpr uint32_t LED_SETTINGS_SAVE_RETRY_MS = 1000; // minimum interval between NVS write attempts

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
// Multikey group link (master<->slaves) and the BLE COM port.
constexpr const char *LINK_SERVICE_UUID = "7A0C0001-3F45-4E5A-8C42-00A100000001";
constexpr const char *LINK_TX_UUID = "7A0C0002-3F45-4E5A-8C42-00A100000001";
constexpr const char *LINK_RX_UUID = "7A0C0003-3F45-4E5A-8C42-00A100000001";
constexpr const char *COM_SERVICE_UUID = "0xFFE0";
constexpr const char *COM_TX_UUID = "0xFFE1";
constexpr const char *COM_RX_UUID = "0xFFE2";

constexpr const char *FIRMWARE_VERSION = "3.0.0";
constexpr uint16_t SERIAL_PROTOCOL_VERSION = 2;

} // namespace Config
