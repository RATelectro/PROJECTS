/*
 * MultiKey.ino
 *
 * ESP32-C3 SuperMini BLE HID keyboard
 *
 * Port of the original STM32 + Keypad + Keyboard + FastLED sketch.
 *
 * Features
 * --------
 * - 7 x 5 matrix
 * - Custom matrix scanner (no Keypad.h dependency)
 * - Debouncing
 * - BLE HID keyboard
 * - NKRO bitmap HID report
 * - Persistent keymap in NVS
 * - BLE GATT keymap configuration
 * - USB Serial / USB-CDC keymap configuration
 * - FastLED WS2812B lighting
 * - Original physical LED coordinate transformation preserved
 *
 * Arduino framework
 *
 * Recommended:
 *   Arduino-ESP32 3.x
 *   FastLED 3.9.x or newer
 *
 * ESP32-C3 BLE HID uses the BLEHIDDevice implementation included
 * in the Arduino-ESP32 core.
 */

#include <Arduino.h>
#include <Preferences.h>
#include <FastLED.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

// ESP32 hardware RNG, used only for random LED colors.
#include <esp_system.h>


// ============================================================================
// USER CONFIGURATION
// ============================================================================

// --------------------------- Matrix dimensions -----------------------------

#define ROWS 7
#define COLS 5

#define NUM_KEYS (ROWS * COLS)
#define KEYMAP_STORAGE_BYTES (NUM_KEYS * sizeof(KeyAction))


// --------------------------- Matrix pins ------------------------------------
//
// Default arrangement for a common ESP32-C3 SuperMini.
//
// USB D-/D+:
//   GPIO18 / GPIO19
//
// RGB LED:
//   GPIO8
//
// Matrix:
//   Rows    GPIO0, GPIO1, GPIO3, GPIO4, GPIO5, GPIO6, GPIO7
//   Columns GPIO2, GPIO9, GPIO10, GPIO20, GPIO21
//
// These are compile-time configurable.

#define MATRIX_ROW_0 20
#define MATRIX_ROW_1 10
#define MATRIX_ROW_2 9
#define MATRIX_ROW_3 8
#define MATRIX_ROW_4 7
#define MATRIX_ROW_5 6
#define MATRIX_ROW_6 5

#define MATRIX_COL_0 21
#define MATRIX_COL_1 0
#define MATRIX_COL_2 1
#define MATRIX_COL_3 2
#define MATRIX_COL_4 3

#define LED_PIN 4


const uint8_t rowPins[ROWS] = {
    MATRIX_ROW_0,
    MATRIX_ROW_1,
    MATRIX_ROW_2,
    MATRIX_ROW_3,
    MATRIX_ROW_4,
    MATRIX_ROW_5,
    MATRIX_ROW_6
};

const uint8_t colPins[COLS] = {
    MATRIX_COL_0,
    MATRIX_COL_1,
    MATRIX_COL_2,
    MATRIX_COL_3,
    MATRIX_COL_4
};


// --------------------------- Matrix electrical configuration ---------------
//
// This scanner assumes:
//
//   columns = INPUT_PULLUP
//   one row = driven LOW at a time
//   pressed key = LOW on the corresponding column
//
// This is the normal arrangement for a simple switch matrix with the
// appropriate wiring/diodes.
//
// Rows are returned to INPUT after every scan. This prevents inactive rows
// from sourcing current and is also friendlier to strapping pins.

#define MATRIX_PRESSED_LEVEL LOW
#define MATRIX_ROW_ACTIVE_LEVEL LOW

#define ROW_SETTLE_US 30


// --------------------------- Debounce --------------------------------------

#define DEBOUNCE_MS 5


// --------------------------- BLE keyboard ----------------------------------

#define BLE_DEVICE_NAME "MultiKey C3 Keyboard"

#define BLE_MANUFACTURER "Custom MultiKey"

// USB HID vendor/product information.
// These are identifiers only; change them if you have your own USB/BLE
// identity requirements.
#define BLE_VENDOR_ID 0x1209
#define BLE_PRODUCT_ID 0xC301
#define BLE_VERSION 0x0100


// --------------------------- NKRO ------------------------------------------
//
// 1 = true NKRO bitmap report
// 0 = traditional 6-key rollover report
//
// NKRO is enabled by default.
//
// With NKRO enabled, the BLE host must negotiate an MTU large enough for
// the report. BLEDevice::setMTU(64) is requested below.
//
// The NKRO report contains:
//
//   byte 0   modifiers
//   byte 1   reserved
//   byte 2-30 = keyboard usages 0x00..0xE7 as bits
//
// Total = 31 bytes.

#define USE_NKRO 0


// --------------------------- FastLED ---------------------------------------

#define COLOR_ORDER GRB
#define CHIPSET WS2812B
#define BRIGHTNESS 50
#define LED_UPD_INTERVAL 50   //ms
#define HELLO_SWEEP_SPEED 100  // ms, delay

CRGB leds[NUM_KEYS];


// --------------------------- Startup animation ------------------------------

bool helloFlag = true;


// --------------------------- NVS --------------------------------------------

#define NVS_NAMESPACE "multikey"

#define NVS_KEYMAP_KEY "keymap"
#define NVS_MAGIC_KEY "magic"

#define KEYMAP_MAGIC 0x4D4B3031UL   // "MK01"


// --------------------------- BLE GATT configuration -------------------------
//
// Custom service used to modify the keymap over BLE.
//
// SET characteristic:
//
//   write exactly 3 bytes:
//
//   byte 0 = key index 0..34
//   byte 1 = HID usage
//   byte 2 = modifier byte
//
// Special commands:
//
//   index = 0xFF -> reset to compiled default keymap
//
// Example:
//
//   index 0 = row 0 / col 0
//   usage 0x4C = Delete
//   modifier 0
//
// So:
//
//   FF is reset
//

#define KEYMAP_SERVICE_UUID \
    "7A0B0001-3F45-4E5A-8C42-00A100000001"

#define KEYMAP_SET_UUID \
    "7A0B0002-3F45-4E5A-8C42-00A100000001"

#define KEYMAP_DUMP_UUID \
    "7A0B0003-3F45-4E5A-8C42-00A100000001"


// ============================================================================
// HID KEYCODES
// ============================================================================
//
// These are USB HID Keyboard/Keypad usage IDs.
//
// They are NOT the old Arduino Keyboard.h values from the STM32 sketch.
//
// This is important:
// Arduino's:
//
//   KEY_F1 = 0xC2
//
// does NOT mean USB HID F1.
//
// USB HID F1 is:
//
//   0x3A
//
// Keeping actual HID usage IDs here makes the keymap directly understandable.
// ============================================================================

enum : uint8_t {

    KEY_NONE = 0x00,

    // Letters
    KEY_A = 0x04,
    KEY_B = 0x05,
    KEY_C = 0x06,
    KEY_D = 0x07,
    KEY_E = 0x08,
    KEY_F = 0x09,
    KEY_G = 0x0A,
    KEY_H = 0x0B,
    KEY_I = 0x0C,
    KEY_J = 0x0D,
    KEY_K = 0x0E,
    KEY_L = 0x0F,
    KEY_M = 0x10,
    KEY_N = 0x11,
    KEY_O = 0x12,
    KEY_P = 0x13,
    KEY_Q = 0x14,
    KEY_R = 0x15,
    KEY_S = 0x16,
    KEY_T = 0x17,
    KEY_U = 0x18,
    KEY_V = 0x19,
    KEY_W = 0x1A,
    KEY_X = 0x1B,
    KEY_Y = 0x1C,
    KEY_Z = 0x1D,

    // Numbers
    KEY_1 = 0x1E,
    KEY_2 = 0x1F,
    KEY_3 = 0x20,
    KEY_4 = 0x21,
    KEY_5 = 0x22,
    KEY_6 = 0x23,
    KEY_7 = 0x24,
    KEY_8 = 0x25,
    KEY_9 = 0x26,
    KEY_0 = 0x27,

    // Main keyboard
    KEY_ENTER = 0x28,
    KEY_ESC = 0x29,
    KEY_BACKSPACE = 0x2A,
    KEY_TAB = 0x2B,
    KEY_SPACE = 0x2C,

    KEY_MINUS = 0x2D,
    KEY_EQUAL = 0x2E,
    KEY_LEFT_BRACKET = 0x2F,
    KEY_RIGHT_BRACKET = 0x30,
    KEY_BACKSLASH = 0x31,

    KEY_SEMICOLON = 0x33,
    KEY_APOSTROPHE = 0x34,
    KEY_GRAVE = 0x35,

    KEY_COMMA = 0x36,
    KEY_DOT = 0x37,
    KEY_SLASH = 0x38,

    // Function keys
    KEY_F1 = 0x3A,
    KEY_F2 = 0x3B,
    KEY_F3 = 0x3C,
    KEY_F4 = 0x3D,
    KEY_F5 = 0x3E,
    KEY_F6 = 0x3F,
    KEY_F7 = 0x40,
    KEY_F8 = 0x41,
    KEY_F9 = 0x42,
    KEY_F10 = 0x43,
    KEY_F11 = 0x44,
    KEY_F12 = 0x45,

    // System
    KEY_PRINT_SCREEN = 0x46,
    KEY_SCROLL_LOCK = 0x47,
    KEY_PAUSE = 0x48,

    // Navigation
    KEY_INSERT = 0x49,
    KEY_HOME = 0x4A,
    KEY_PAGE_UP = 0x4B,
    KEY_DELETE = 0x4C,
    KEY_END = 0x4D,
    KEY_PAGE_DOWN = 0x4E,

    KEY_RIGHT_ARROW = 0x4F,
    KEY_LEFT_ARROW = 0x50,
    KEY_DOWN_ARROW = 0x51,
    KEY_UP_ARROW = 0x52,

    // Numeric keypad
    KEY_NUM_LOCK = 0x53,
    KEY_KP_SLASH = 0x54,
    KEY_KP_ASTERISK = 0x55,
    KEY_KP_MINUS = 0x56,
    KEY_KP_PLUS = 0x57,
    KEY_KP_ENTER = 0x58,

    KEY_KP_1 = 0x59,
    KEY_KP_2 = 0x5A,
    KEY_KP_3 = 0x5B,
    KEY_KP_4 = 0x5C,
    KEY_KP_5 = 0x5D,
    KEY_KP_6 = 0x5E,
    KEY_KP_7 = 0x5F,
    KEY_KP_8 = 0x60,
    KEY_KP_9 = 0x61,
    KEY_KP_0 = 0x62,
    KEY_KP_DOT = 0x63,

    // Application / menu
    KEY_MENU = 0x65,

    // F13-F24
    KEY_F13 = 0x68,
    KEY_F14 = 0x69,
    KEY_F15 = 0x6A,
    KEY_F16 = 0x6B,
    KEY_F17 = 0x6C,
    KEY_F18 = 0x6D,
    KEY_F19 = 0x6E,
    KEY_F20 = 0x6F,
    KEY_F21 = 0x70,
    KEY_F22 = 0x71,
    KEY_F23 = 0x72,
    KEY_F24 = 0x73,

    // Modifier usages.
    //
    // Normally modifiers are represented by bits in byte 0 of the HID
    // report. We still use the normal HID usages in the keymap so that
    // the physical layout remains readable.
    KEY_LEFT_CTRL = 0xE0,
    KEY_LEFT_SHIFT = 0xE1,
    KEY_LEFT_ALT = 0xE2,
    KEY_LEFT_GUI = 0xE3,

    KEY_RIGHT_CTRL = 0xE4,
    KEY_RIGHT_SHIFT = 0xE5,
    KEY_RIGHT_ALT = 0xE6,
    KEY_RIGHT_GUI = 0xE7
};


// HID modifier bit masks.

enum : uint8_t {

    MOD_NONE = 0x00,

    MOD_LCTRL = 0x01,
    MOD_LSHIFT = 0x02,
    MOD_LALT = 0x04,
    MOD_LGUI = 0x08,

    MOD_RCTRL = 0x10,
    MOD_RSHIFT = 0x20,
    MOD_RALT = 0x40,
    MOD_RGUI = 0x80
};


// Logical key action stored in RAM/NVS.
//
// usage:
//   USB HID usage ID.
//
// modifiers:
//   extra modifier mask.
//
// Example:
//   tilde = Shift + Grave
//
//   { KEY_GRAVE, MOD_LSHIFT }

struct KeyAction {

    uint8_t usage;
    uint8_t modifiers;

} __attribute__((packed));

static_assert(sizeof(KeyAction) == 2, "KeyAction must be exactly 2 bytes");


// ============================================================================
// KEYMAP HELPERS
// ============================================================================

#define K(k)              { (uint8_t)(k), MOD_NONE }
#define KM(k, m)           { (uint8_t)(k), (uint8_t)(m) }


// ============================================================================
// DEFAULT KEYMAP
// ============================================================================
//
// This is your original STM32 keymap translated to actual USB HID usages.
//
// Physical coordinates:
//
// row 0: Delete,   0,    LeftAlt,    T,          Right
// row 1: Space,    8,    M,          Up,         Down
// row 2: F4,       6,    R,          F,          Left
// row 3: F3,       4,    E,          D,          B
// row 4: F2,       2,    W,          S,          X
// row 5: F1,       1,    Q,          A,          Z
// row 6: Esc,      ~,    Tab,        LeftShift,  LeftCtrl
//
// '~' requires Shift + Grave in a US HID keyboard layout.
//

const KeyAction DEFAULT_KEYMAP[ROWS][COLS] = {

    {
        K(KEY_DELETE),
        K(KEY_0),
        K(KEY_LEFT_ALT),
        K(KEY_T),
        K(KEY_RIGHT_ARROW)
    },

    {
        K(KEY_SPACE),
        K(KEY_8),
        K(KEY_M),
        K(KEY_UP_ARROW),
        K(KEY_DOWN_ARROW)
    },

    {
        K(KEY_F4),
        K(KEY_6),
        K(KEY_R),
        K(KEY_F),
        K(KEY_LEFT_ARROW)
    },

    {
        K(KEY_F3),
        K(KEY_4),
        K(KEY_E),
        K(KEY_D),
        K(KEY_B)
    },

    {
        K(KEY_F2),
        K(KEY_2),
        K(KEY_W),
        K(KEY_S),
        K(KEY_X)
    },

    {
        K(KEY_F1),
        K(KEY_1),
        K(KEY_Q),
        K(KEY_A),
        K(KEY_Z)
    },

    {
        K(KEY_ESC),
        KM(KEY_GRAVE, MOD_LSHIFT),   // '~'
        K(KEY_TAB),
        K(KEY_LEFT_SHIFT),
        K(KEY_LEFT_CTRL)
    }
};


// Working keymap. Loaded from NVS during setup.

KeyAction keymap[ROWS][COLS];


// ============================================================================
// MATRIX STATE
// ============================================================================

bool rawPressed[ROWS][COLS];
bool stablePressed[ROWS][COLS];

uint32_t rawChangedAt[ROWS][COLS];


// ============================================================================
// BLE GLOBALS
// ============================================================================

BLEServer *bleServer = nullptr;
BLEHIDDevice *hid = nullptr;

BLECharacteristic *keyboardInputReport = nullptr;

BLEService *keymapService = nullptr;
BLECharacteristic *keymapSetCharacteristic = nullptr;
BLECharacteristic *keymapDumpCharacteristic = nullptr;

volatile bool bleConnected = false;
volatile bool forceKeyboardReport = true;


// ============================================================================
// SERIAL / NVS STATE
// ============================================================================

Preferences preferences;


// ============================================================================
// HID NKRO REPORT
// ============================================================================

#if USE_NKRO

/*
 * NKRO report:
 *
 * byte 0   modifiers
 * byte 1   reserved
 * byte 2   usage bitmap bits 0..7
 * ...
 * byte 30  usage bitmap bits 224..231
 *
 * Usages 0x00..0xE7 = 232 usages
 * 232 / 8 = 29 bytes
 */

static constexpr size_t NKRO_BITMAP_BYTES = 29;
static constexpr size_t HID_REPORT_SIZE = 2 + NKRO_BITMAP_BYTES;

uint8_t lastHidReport[HID_REPORT_SIZE];

#else

/*
 * Traditional 6KRO report:
 *
 * byte 0 = modifiers
 * byte 1 = reserved
 * bytes 2..7 = six simultaneous non-modifier usages
 */

static constexpr size_t HID_REPORT_SIZE = 8;

uint8_t lastHidReport[HID_REPORT_SIZE];

#endif


// ============================================================================
// BLE HID REPORT DESCRIPTOR
// ============================================================================

#if USE_NKRO

/*
 * True NKRO keyboard report descriptor.
 *
 * Report ID 1
 *
 * Eight modifier bits
 * One reserved byte
 * 232 keyboard usage bits
 */
const uint8_t HID_REPORT_DESCRIPTOR[] = {

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)

    0x85, 0x01,       // Report ID (1)

    // Modifier byte
    0x05, 0x07,       // Usage Page (Keyboard/Keypad)
    0x19, 0xE0,       // Usage Minimum (Left Control)
    0x29, 0xE7,       // Usage Maximum (Right GUI)

    0x15, 0x00,       // Logical Minimum (0)
    0x25, 0x01,       // Logical Maximum (1)

    0x75, 0x01,       // Report Size (1)
    0x95, 0x08,       // Report Count (8)

    0x81, 0x02,       // Input (Data, Variable, Absolute)

    // Reserved byte
    0x75, 0x08,       // Report Size (8)
    0x95, 0x01,       // Report Count (1)
    0x81, 0x01,       // Input (Constant)

    // NKRO bitmap
    0x15, 0x00,       // Logical Minimum (0)
    0x26, 0xE7, 0x00, // Logical Maximum (231)

    0x19, 0x00,       // Usage Minimum (0)
    0x29, 0xE7,       // Usage Maximum (231)

    0x75, 0x01,       // Report Size (1)
    0x95, 0xE8,       // Report Count (232)

    0x81, 0x02,       // Input (Data, Variable, Absolute)

    0xC0              // End Collection
};

#else

/*
 * Standard 6KRO keyboard descriptor.
 */
const uint8_t HID_REPORT_DESCRIPTOR[] = {

    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)

    0x85, 0x01,       // Report ID 1

    // Modifier
    0x05, 0x07,
    0x19, 0xE0,
    0x29, 0xE7,

    0x15, 0x00,
    0x25, 0x01,

    0x75, 0x01,
    0x95, 0x08,

    0x81, 0x02,

    // Reserved
    0x75, 0x08,
    0x95, 0x01,

    0x81, 0x01,

    // Six simultaneous keys
    0x05, 0x07,
    0x19, 0x00,
    0x29, 0xE7,

    0x15, 0x00,
    0x25, 0xE7,

    0x75, 0x08,
    0x95, 0x06,

    0x81, 0x00,

    0xC0
};

#endif


// ============================================================================
// BLE SERVER CALLBACKS
// ============================================================================

class KeyboardServerCallbacks : public BLEServerCallbacks {

public:

    void onConnect(BLEServer *server) override {

        bleConnected = true;

        // If keys are already physically held when the host connects,
        // force one complete report immediately.
        forceKeyboardReport = true;

        Serial.println("[BLE] Keyboard connected");
    }

    void onDisconnect(BLEServer *server) override {

        bleConnected = false;

        Serial.println("[BLE] Keyboard disconnected");

        // Allow the host to reconnect.
        BLEDevice::startAdvertising();
    }
};


// ============================================================================
// CUSTOM KEYMAP GATT CALLBACK
// ============================================================================
void saveKeymap(void);
void updateKeymapDumpCharacteristic(void);

class KeymapWriteCallbacks : public BLECharacteristicCallbacks {

public:

    void onWrite(BLECharacteristic *characteristic) override {

        String value = characteristic->getValue();

        if (value.length() != 3) {

            Serial.println(
                "[BLE] Invalid keymap packet. "
                "Expected exactly 3 bytes."
            );

            return;
        }

        uint8_t index = (uint8_t)value[0];
        uint8_t usage = (uint8_t)value[1];
        uint8_t mods  = (uint8_t)value[2];

        // Special command:
        // FF 00 00 = restore compiled default keymap.
        if (index == 0xFF) {

            memcpy(
                keymap,
                DEFAULT_KEYMAP,
                sizeof(keymap)
            );

            saveKeymap();

            updateKeymapDumpCharacteristic();

            forceKeyboardReport = true;

            Serial.println(
                "[BLE] Keymap reset to default"
            );

            return;
        }

        if (index >= NUM_KEYS) {

            Serial.printf(
                "[BLE] Invalid key index: %u\n",
                index
            );

            return;
        }

        uint8_t row = index / COLS;
        uint8_t col = index % COLS;

        keymap[row][col].usage = usage;
        keymap[row][col].modifiers = mods;

        saveKeymap();
        updateKeymapDumpCharacteristic();

        forceKeyboardReport = true;

        Serial.printf(
            "[BLE] SET key[%u] = usage 0x%02X mods 0x%02X\n",
            index,
            usage,
            mods
        );
    }
};


// ============================================================================
// NVS
// ============================================================================

void loadDefaultKeymap() {

    memcpy(
        keymap,
        DEFAULT_KEYMAP,
        sizeof(keymap)
    );
}


void saveKeymap() {

    preferences.putBytes(
        NVS_KEYMAP_KEY,
        keymap,
        sizeof(keymap)
    );

    preferences.putUInt(
        NVS_MAGIC_KEY,
        KEYMAP_MAGIC
    );

    Serial.println("[NVS] Keymap saved");
}


bool loadKeymap() {

    uint32_t magic = preferences.getUInt(
        NVS_MAGIC_KEY,
        0
    );

    size_t length = preferences.getBytesLength(
        NVS_KEYMAP_KEY
    );

    if (
        magic != KEYMAP_MAGIC ||
        length != sizeof(keymap)
    ) {

        Serial.println(
            "[NVS] No valid keymap found; "
            "using compiled default"
        );

        loadDefaultKeymap();

        saveKeymap();

        return false;
    }

    size_t read = preferences.getBytes(
        NVS_KEYMAP_KEY,
        keymap,
        sizeof(keymap)
    );

    if (read != sizeof(keymap)) {

        Serial.println(
            "[NVS] Keymap read failed; "
            "using compiled default"
        );

        loadDefaultKeymap();
        saveKeymap();

        return false;
    }

    Serial.println("[NVS] Keymap loaded");

    return true;
}


// ============================================================================
// BLE KEYMAP DUMP
// ============================================================================

void updateKeymapDumpCharacteristic() {

    if (keymapDumpCharacteristic == nullptr) {
        return;
    }

    keymapDumpCharacteristic->setValue(
        (uint8_t *)keymap,
        sizeof(keymap)
    );
}


// ============================================================================
// HID REPORT BUILDING
// ============================================================================

void clearReport(uint8_t *report) {

    memset(
        report,
        0,
        HID_REPORT_SIZE
    );
}


#if USE_NKRO

void addUsageToNkroReport(
    uint8_t *report,
    uint8_t usage
) {

    if (usage == KEY_NONE) {
        return;
    }

    // Modifier usage.
    if (usage >= 0xE0 && usage <= 0xE7) {

        report[0] |=
            (uint8_t)(1 << (usage - 0xE0));

        return;
    }

    // Our descriptor supports usages 0x00..0xE7.
    if (usage > 0xE7) {
        return;
    }

    // HID usage 0x00 is reserved/no key.
    if (usage == 0x00) {
        return;
    }

    uint16_t bit = usage;

    uint16_t byteIndex =
        2 + (bit / 8);

    uint8_t bitMask =
        1 << (bit % 8);

    if (byteIndex < HID_REPORT_SIZE) {

        report[byteIndex] |= bitMask;
    }
}


void buildKeyboardReport(
    uint8_t *report
) {

    clearReport(report);

    for (uint8_t row = 0; row < ROWS; ++row) {

        for (uint8_t col = 0; col < COLS; ++col) {

            if (!stablePressed[row][col]) {
                continue;
            }

            const KeyAction &key =
                keymap[row][col];

            // Explicit additional modifier mask.
            report[0] |= key.modifiers;

            addUsageToNkroReport(
                report,
                key.usage
            );
        }
    }
}

#else

void buildKeyboardReport(
    uint8_t *report
) {

    clearReport(report);

    uint8_t count = 0;

    for (uint8_t row = 0; row < ROWS; ++row) {

        for (uint8_t col = 0; col < COLS; ++col) {

            if (!stablePressed[row][col]) {
                continue;
            }

            const KeyAction &key =
                keymap[row][col];

            // Explicit modifiers.
            report[0] |= key.modifiers;

            // HID modifier usage.
            if (
                key.usage >= 0xE0 &&
                key.usage <= 0xE7
            ) {

                report[0] |=
                    (uint8_t)(
                        1 << (key.usage - 0xE0)
                    );

                continue;
            }

            // 6KRO capacity.
            if (count < 6) {

                report[2 + count] = key.usage;

                ++count;
            }
        }
    }
}

#endif


void sendKeyboardReport(
    bool force
) {

    if (!bleConnected) {
        return;
    }

    uint8_t report[HID_REPORT_SIZE];

    buildKeyboardReport(report);

    if (
        !force &&
        memcmp(
            report,
            lastHidReport,
            HID_REPORT_SIZE
        ) == 0
    ) {

        return;
    }

    memcpy(
        lastHidReport,
        report,
        HID_REPORT_SIZE
    );

    keyboardInputReport->setValue(
        report,
        HID_REPORT_SIZE
    );

    keyboardInputReport->notify();
}


// ============================================================================
// LED INDEX TRANSFORMATION
// ============================================================================
//
// This deliberately preserves your original STM32 mapping:
//
//   int k = row * COLS + col;
//   int c = k % COLS;
//   int r = k / COLS;
//
//   int cc = ROWS - 1 - r;
//   int rr = c;
//
//   int ledIdx = cc + rr * ROWS;
//
// This accounts for your swapped row/column/LED physical orientation.
// ============================================================================

uint8_t matrixToLedIndex(
    uint8_t row,
    uint8_t col
) {

    uint8_t cc = ROWS - 1 - row;
    uint8_t rr = col;

    return cc + rr * ROWS;
}


void updateKeyLed(
    uint8_t row,
    uint8_t col,
    bool pressed
) {

    uint8_t ledIndex =
        matrixToLedIndex(row, col);

    if (pressed) {

        leds[ledIndex].setRGB(
            random(10, 255),
            random(10, 255),
            random(10, 255)
        );

    } else {

        leds[ledIndex].setRGB(
            15,
            random(10, 30),
            15
        );
    }
}


// ============================================================================
// MATRIX SCANNER
// ============================================================================

void initializeMatrix() {

    // Columns are normally HIGH through their pull-ups.
    for (uint8_t col = 0; col < COLS; ++col) {

        pinMode(
            colPins[col],
            INPUT_PULLUP
        );
    }

    // All rows high-Z when idle.
    for (uint8_t row = 0; row < ROWS; ++row) {

        pinMode(
            rowPins[row],
            INPUT
        );
    }

    memset(
        rawPressed,
        0,
        sizeof(rawPressed)
    );

    memset(
        stablePressed,
        0,
        sizeof(stablePressed)
    );

    memset(
        rawChangedAt,
        0,
        sizeof(rawChangedAt)
    );
}


void scanMatrix() {

    const uint32_t now =
        millis();

    bool stateChanged = false;

    for (uint8_t row = 0; row < ROWS; ++row) {

        // All rows inactive / high impedance.
        for (uint8_t r = 0; r < ROWS; ++r) {

            pinMode(
                rowPins[r],
                INPUT
            );
        }

        // Activate exactly one row.
        pinMode(
            rowPins[row],
            OUTPUT
        );

        digitalWrite(
            rowPins[row],
            MATRIX_ROW_ACTIVE_LEVEL
        );

        delayMicroseconds(
            ROW_SETTLE_US
        );

        // Read all columns.
        for (uint8_t col = 0; col < COLS; ++col) {

            bool pressed =
                digitalRead(colPins[col])
                == MATRIX_PRESSED_LEVEL;

            if (
                pressed !=
                rawPressed[row][col]
            ) {

                rawPressed[row][col] =
                    pressed;

                rawChangedAt[row][col] =
                    now;
            }

            // Commit after the raw state has remained
            // unchanged for the entire debounce period.
            if (
                (stablePressed[row][col] !=
                 rawPressed[row][col]) &&

                (uint32_t)(
                    now -
                    rawChangedAt[row][col]
                ) >= DEBOUNCE_MS
            ) {

                stablePressed[row][col] =
                    rawPressed[row][col];

                updateKeyLed(
                    row,
                    col,
                    stablePressed[row][col]
                );

                stateChanged = true;
            }
        }

        // Return row to high-Z.
        pinMode(
            rowPins[row],
            INPUT
        );
    }

    if (stateChanged) {

        sendKeyboardReport(false);
    }
}


// ============================================================================
// STARTUP RGB ANIMATION
// ============================================================================

void sweepRainbow(
    bool clear
) {

    // Same basic animation idea as your original code.
    //
    // The LEDs are physically arranged in the transformed order used above.

    for (
        int step = 0;
        step < (COLS * ROWS) - 1;
        ++step
    ) {

        for (int row = 0; row < COLS; ++row) {

            int col =
                step - row;

            if (
                col >= 0 &&
                col < ROWS
            ) {

                int ledIndex =
                    row * ROWS + col;

                uint8_t hue =
                    (uint8_t)(
                        map(
                            step,
                            0,
                            (COLS * ROWS) - 2,
                            0,
                            250
                        )
                    );

                if (clear) {

                    leds[ledIndex] =
                        CHSV(
                            hue,
                            0,
                            0
                        );

                } else {

                    leds[ledIndex] =
                        CHSV(
                            hue,
                            255,
                            200
                        );
                }
            }
        }

        FastLED.show();

        delay(HELLO_SWEEP_SPEED);
    }
}


// ============================================================================
// SERIAL KEYMAP TOOLS
// ============================================================================
//
// Commands:
//
//   HELP
//
//   DUMP
//
//   RESET
//
//   SAVE
//
//   SET <row> <col> <usage> <modifier>
//
// Example:
//
//   SET 0 0 0x4C 0
//
// sets row 0 / column 0 to Delete.
//
// Another example:
//
//   SET 6 1 0x35 0x02
//
// sets row 6 / column 1 to Shift + Grave, i.e. '~'.
//
// For convenience, values may be decimal or 0x-prefixed hexadecimal.
//

uint32_t parseNumber(
    const char *s,
    bool &ok
) {

    if (
        s == nullptr ||
        *s == '\0'
    ) {

        ok = false;

        return 0;
    }

    char *endPtr = nullptr;

    unsigned long value =
        strtoul(
            s,
            &endPtr,
            0
        );

    if (
        endPtr == s ||
        *endPtr != '\0'
    ) {

        ok = false;

        return 0;
    }

    ok = true;

    return value;
}


void printKeymap() {

    Serial.println();
    Serial.println("Current keymap:");
    Serial.println(
        "row col  index  usage  mods"
    );

    for (uint8_t row = 0; row < ROWS; ++row) {

        for (uint8_t col = 0; col < COLS; ++col) {

            uint8_t index =
                row * COLS + col;

            Serial.printf(
                "%3u %3u %5u   0x%02X   0x%02X\n",
                row,
                col,
                index,
                keymap[row][col].usage,
                keymap[row][col].modifiers
            );
        }
    }

    Serial.println();
}


void printHelp() {

    Serial.println();
    Serial.println("MultiKey configuration commands:");
    Serial.println();
    Serial.println("  HELP");
    Serial.println("      Show this help");
    Serial.println();
    Serial.println("  DUMP");
    Serial.println("      Print current keymap");
    Serial.println();
    Serial.println("  RESET");
    Serial.println("      Restore compiled default keymap");
    Serial.println();
    Serial.println("  SAVE");
    Serial.println("      Save current RAM keymap to NVS");
    Serial.println();
    Serial.println(
        "  SET <row> <col> <usage> <mods>"
    );
    Serial.println(
        "      Example: SET 0 0 0x4C 0"
    );
    Serial.println();
    Serial.println(
        "  SET 6 1 0x35 0x02"
    );
    Serial.println(
        "      Shift + Grave (~)"
    );
    Serial.println();
}


void processSerialCommand(
    String line
) {

    line.trim();

    if (line.length() == 0) {
        return;
    }

    char buffer[128];

    line.toCharArray(
        buffer,
        sizeof(buffer)
    );

    char *command =
        strtok(
            buffer,
            " \t"
        );

    if (command == nullptr) {
        return;
    }

    if (
        strcasecmp(
            command,
            "HELP"
        ) == 0
    ) {

        printHelp();

        return;
    }

    if (
        strcasecmp(
            command,
            "DUMP"
        ) == 0
    ) {

        printKeymap();

        return;
    }

    if (
        strcasecmp(
            command,
            "SAVE"
        ) == 0
    ) {

        saveKeymap();

        return;
    }

    if (
        strcasecmp(
            command,
            "RESET"
        ) == 0
    ) {

        loadDefaultKeymap();
        saveKeymap();

        updateKeymapDumpCharacteristic();

        forceKeyboardReport = true;

        Serial.println(
            "Default keymap restored."
        );

        return;
    }

    if (
        strcasecmp(
            command,
            "SET"
        ) == 0
    ) {

        char *rowText =
            strtok(nullptr, " \t");

        char *colText =
            strtok(nullptr, " \t");

        char *usageText =
            strtok(nullptr, " \t");

        char *modifierText =
            strtok(nullptr, " \t");

        if (
            rowText == nullptr ||
            colText == nullptr ||
            usageText == nullptr ||
            modifierText == nullptr
        ) {

            Serial.println(
                "Syntax: SET <row> <col> <usage> <mods>"
            );

            return;
        }

        bool okRow = false;
        bool okCol = false;
        bool okUsage = false;
        bool okMods = false;

        uint32_t row =
            parseNumber(
                rowText,
                okRow
            );

        uint32_t col =
            parseNumber(
                colText,
                okCol
            );

        uint32_t usage =
            parseNumber(
                usageText,
                okUsage
            );

        uint32_t mods =
            parseNumber(
                modifierText,
                okMods
            );

        if (
            !okRow ||
            !okCol ||
            !okUsage ||
            !okMods
        ) {

            Serial.println(
                "Invalid numeric value."
            );

            return;
        }

        if (
            row >= ROWS ||
            col >= COLS
        ) {

            Serial.println(
                "Row/column out of range."
            );

            return;
        }

        if (usage > 0xFF || mods > 0xFF) {

            Serial.println(
                "Usage and modifier must be 0..255."
            );

            return;
        }

        keymap[row][col].usage =
            (uint8_t)usage;

        keymap[row][col].modifiers =
            (uint8_t)mods;

        updateKeymapDumpCharacteristic();

        forceKeyboardReport = true;

        Serial.printf(
            "SET row=%lu col=%lu usage=0x%02lX mods=0x%02lX\n",
            row,
            col,
            usage,
            mods
        );

        return;
    }

    Serial.println(
        "Unknown command. Type HELP."
    );
}


void serviceSerial() {

    static String line;

    while (Serial.available()) {

        char ch =
            (char)Serial.read();

        if (
            ch == '\n' ||
            ch == '\r'
        ) {

            if (line.length() > 0) {

                processSerialCommand(line);

                line = "";
            }

        } else {

            if (line.length() < 120) {

                line += ch;
            }
        }
    }
}


// ============================================================================
// BLE INITIALIZATION
// ============================================================================

void initializeBluetooth() {

    Serial.println("[BLE] Initializing...");

    BLEDevice::init(
        BLE_DEVICE_NAME
    );

    /*
     * NKRO requires more than the default 23-byte ATT MTU.
     *
     * 64 is comfortably large enough for:
     *
     *   31-byte NKRO report
     *   3-byte ATT/GATT overhead
     */
    BLEDevice::setMTU(64);

    // Just Works + bonding + secure connections.
    //
    // There is no display or keyboard for a pairing PIN.
    BLESecurity *security =
        new BLESecurity();

    security->setCapability(
        ESP_IO_CAP_NONE
    );

    security->setAuthenticationMode(
        true,   // bonding
        false,  // MITM
        true    // secure connections
    );

    bleServer =
        BLEDevice::createServer();

    bleServer->setCallbacks(
        new KeyboardServerCallbacks()
    );

    // ------------------------------------------------------------------------
    // HID
    // ------------------------------------------------------------------------

    hid =
        new BLEHIDDevice(
            bleServer
        );

    hid->manufacturer()->setValue(
        BLE_MANUFACTURER
    );

    hid->pnp(
        0x02,
        BLE_VENDOR_ID,
        BLE_PRODUCT_ID,
        BLE_VERSION
    );

    hid->hidInfo(
        0x00,   // country code
        0x01
    );

    hid->reportMap(
        (uint8_t *)HID_REPORT_DESCRIPTOR,
        sizeof(HID_REPORT_DESCRIPTOR)
    );

    keyboardInputReport =
        hid->inputReport(1);

    if (keyboardInputReport == nullptr) {

        Serial.println(
            "[BLE] ERROR: Could not create HID input report"
        );

        return;
    }

    hid->setBatteryLevel(100);


    // ------------------------------------------------------------------------
    // Custom keymap GATT service
    // ------------------------------------------------------------------------

    keymapService =
        bleServer->createService(
            KEYMAP_SERVICE_UUID
        );

    // Write exactly 3 bytes:
    //
    // [index][usage][modifiers]
    //
    keymapSetCharacteristic =
        keymapService->createCharacteristic(
            KEYMAP_SET_UUID,
            BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR
        );

    keymapSetCharacteristic->setCallbacks(
        new KeymapWriteCallbacks()
    );


    // Read current complete keymap.
    //
    // 35 entries x 2 bytes = 70 bytes.
    keymapDumpCharacteristic =
        keymapService->createCharacteristic(
            KEYMAP_DUMP_UUID,
            BLECharacteristic::PROPERTY_READ
        );

    updateKeymapDumpCharacteristic();


    // ------------------------------------------------------------------------
    // Start services
    // ------------------------------------------------------------------------
    //
    // BLEHIDDevice::startServices() starts the HID services and the BLE server.
    //
    hid->startServices();


    // ------------------------------------------------------------------------
    // Advertising
    // ------------------------------------------------------------------------

    BLEAdvertising *advertising =
        BLEDevice::getAdvertising();

    advertising->setAppearance(
        0x03C1        // HID Keyboard
    );

    advertising->addServiceUUID(
        hid->hidService()->getUUID()
    );

    advertising->addServiceUUID(
        BLEUUID(
            KEYMAP_SERVICE_UUID
        )
    );

    advertising->setScanResponse(true);

    advertising->setMinPreferred(0x06);
    advertising->setMaxPreferred(0x12);

    BLEDevice::startAdvertising();

    Serial.println(
        "[BLE] Advertising as:"
    );

    Serial.println(
        BLE_DEVICE_NAME
    );
}


// ============================================================================
// SETUP
// ============================================================================

void setup() {

    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println(
        "===================================="
    );

    Serial.println(
        "   MultiKey ESP32-C3 Keyboard"
    );

    Serial.println(
        "===================================="
    );


    // ------------------------------------------------------------------------
    // NVS
    // ------------------------------------------------------------------------

    preferences.begin(
        NVS_NAMESPACE,
        false
    );

    loadKeymap();


    // ------------------------------------------------------------------------
    // Matrix
    // ------------------------------------------------------------------------

    initializeMatrix();


    // ------------------------------------------------------------------------
    // FastLED
    // ------------------------------------------------------------------------

    FastLED.addLeds<
        CHIPSET,
        LED_PIN,
        COLOR_ORDER
    >(
        leds,
        NUM_KEYS
    ).setCorrection(
        TypicalLEDStrip
    );

    FastLED.clear();

    FastLED.setBrightness(
        BRIGHTNESS
    );

    // ESP32 hardware RNG.
    randomSeed(
        esp_random()
    );


    // ------------------------------------------------------------------------
    // Startup animation
    // ------------------------------------------------------------------------

    sweepRainbow(false);
    sweepRainbow(true);

    helloFlag = false;


    // ------------------------------------------------------------------------
    // BLE HID
    // ------------------------------------------------------------------------

    initializeBluetooth();


    // Clear cached report so the first connected scan is reported.
    memset(
        lastHidReport,
        0,
        sizeof(lastHidReport)
    );

    forceKeyboardReport = true;

    Serial.println();
    Serial.println(
        "Ready."
    );

    printHelp();

      fill_rainbow(leds, 35, 128, 7);
      FastLED.show();
      delay(500);
      FastLED.clear();
      fill_rainbow(leds, 35, 58, 7);
      FastLED.show();
      delay(500);
      FastLED.clear();
}


// ============================================================================
// LOOP
// ============================================================================

void loop() {

    // Matrix scanning.
    scanMatrix();


    // BLE configuration / USB-CDC configuration.
    serviceSerial();


    // If a new host connected, or the keymap changed, transmit
    // one complete state report.
    if (forceKeyboardReport) {

        forceKeyboardReport = false;

        sendKeyboardReport(true);
    }


    // Preserve approximately the same LED update rate as your STM32 code.
    //
    // The RGB data itself is hardware-timed by FastLED/RMT.
    static uint32_t lastLedUpdate = 0;

    if (
        (uint32_t)(
            millis() - lastLedUpdate
        ) >= LED_UPD_INTERVAL
    ) {

        lastLedUpdate =
            millis();

        FastLED.show();
    }

    // Yield to BLE/FreeRTOS.
    delay(1);
}