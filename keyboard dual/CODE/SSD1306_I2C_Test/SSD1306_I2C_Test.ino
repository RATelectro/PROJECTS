// ============================================================================
// SSD1306 bit-bang I2C test — standalone single-file sketch.
//
// This runs the EXACT same bit-banged I2C code as the keyboard firmware
// (src/display/Ssd1306.cpp) to isolate why the display stays black there.
// Same pins (SDA=GPIO8, SCL=GPIO7), same open-drain approach, same timings.
//
// It draws the fixed test image (outer perimeter hugging the panel edges,
// inner perimeter at 16 px, one diagonal) and keeps refreshing it.
//
// Serial (115200) reports progress; if the border does not appear, the
// bit-bang itself is at fault. If the border appears here but not in the
// keyboard firmware, the matrix scanner is interfering with the shared pins.
// ============================================================================

#include <Arduino.h>

// --- wiring (identical to the keyboard) --------------------------------------
constexpr uint8_t OLED_SDA = 8;   // GPIO8
constexpr uint8_t OLED_SCL = 7;   // GPIO7
constexpr uint8_t OLED_ADDR = 0x3C;
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;

// --- bit-bang I2C — COPIED VERBATIM from src/display/Ssd1306.cpp --------------
namespace {

inline void i2cDelay() { delayMicroseconds(1); }

inline void sdaHigh() { digitalWrite(OLED_SDA, HIGH); } // release
inline void sdaLow() { digitalWrite(OLED_SDA, LOW); }
inline void sclHigh() { digitalWrite(OLED_SCL, HIGH); } // release
inline void sclLow() { digitalWrite(OLED_SCL, LOW); }

void i2cStart() {
    sdaHigh();
    sclHigh();
    i2cDelay();
    sdaLow();
    i2cDelay();
    sclLow();
    i2cDelay();
}

void i2cStop() {
    sdaLow();
    i2cDelay();
    sclHigh();
    i2cDelay();
    sdaHigh();
    i2cDelay();
}

void i2cWriteByte(uint8_t value) {
    for (int8_t bit = 7; bit >= 0; --bit) {
        if ((value >> bit) & 1) sdaHigh();
        else sdaLow();
        i2cDelay();
        sclHigh();
        i2cDelay();
        sclLow();
        i2cDelay();
    }
    sdaHigh();  // release for the ACK bit (ignored)
    i2cDelay();
    sclHigh();
    i2cDelay();
    sclLow();
    i2cDelay();
}

inline void i2cWriteAddress() {
    i2cWriteByte(static_cast<uint8_t>(OLED_ADDR << 1)); // write mode
}

void oledSendCommand(uint8_t command) {
    i2cStart();
    i2cWriteAddress();
    i2cWriteByte(0x80); // control byte: Co=1, one command follows
    i2cWriteByte(command);
    i2cStop();
}

void oledBegin() {
    // Take the pins as open-drain outputs, bus idle (released).
    digitalWrite(OLED_SDA, HIGH);
    digitalWrite(OLED_SCL, HIGH);
    pinMode(OLED_SDA, OUTPUT_OPEN_DRAIN);
    pinMode(OLED_SCL, OUTPUT_OPEN_DRAIN);
    delay(50); // let the panel power up

    static const uint8_t init[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock divide
        0xA8, 0x3F, // multiplex 64
        0xD3, 0x00, // display offset
        0x40,       // start line 0
        0x8D, 0x14, // charge pump on
        0x20, 0x02, // page addressing mode
        0xA1,       // segment remap
        0xC8,       // COM scan dec
        0xDA, 0x12, // COM pins
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // pre-charge
        0xDB, 0x40, // VCOMH level
        0xA4,       // resume RAM
        0xA6,       // non-inverted
        0xAF        // display on
    };
    i2cStart();
    i2cWriteAddress();
    i2cWriteByte(0x00); // command stream (Co=0)
    for (uint8_t i = 0; i < sizeof(init); ++i) i2cWriteByte(init[i]);
    i2cStop();
}

// Push one page (128 bytes) from RAM.
void oledPushPage(uint8_t page, const uint8_t *data) {
    i2cStart();
    i2cWriteAddress();
    i2cWriteByte(0x80); // command: page select
    i2cWriteByte(static_cast<uint8_t>(0xB0 | page));
    i2cWriteByte(0x80); // command: column low nibble 0
    i2cWriteByte(0x00);
    i2cWriteByte(0x80); // command: column high nibble 0
    i2cWriteByte(0x10);
    i2cWriteByte(0x40); // data stream
    for (uint16_t i = 0; i < OLED_WIDTH; ++i) i2cWriteByte(data[i]);
    i2cStop();
}

} // namespace

// --- test image ----------------------------------------------------------------

uint8_t fb[OLED_WIDTH * OLED_HEIGHT / 8];

void drawTestImage() {
    memset(fb, 0, sizeof(fb));
    for (uint16_t y = 0; y < OLED_HEIGHT; ++y) {
        for (uint16_t x = 0; x < OLED_WIDTH; ++x) {
            const bool outer = (x == 0 || x == OLED_WIDTH - 1 || y == 0 || y == OLED_HEIGHT - 1);
            const bool inner = (x == 16 || x == OLED_WIDTH - 17 || y == 16 || y == OLED_HEIGHT - 17);
            const bool diag  = (x == (y * 2));
            if (outer || inner || diag) {
                fb[(y >> 3) * OLED_WIDTH + x] |= static_cast<uint8_t>(1 << (y & 7));
            }
        }
    }
}

// Set to 1 to simulate the keyboard matrix scanner hammering the shared pins
// (GPIO7/8 are matrix rows 3/4 there). Use this ONLY if the plain test shows
// the border correctly — it reproduces the firmware's pin contention.
#ifndef SIMULATE_MATRIX
#define SIMULATE_MATRIX 0
#endif

// --- setup / loop --------------------------------------------------------------

#if SIMULATE_MATRIX
// Mimics MatrixScanner::update(): every loop, drive all "rows" (the two real
// shared pins plus stand-ins) inactive-LOW, then pulse each row active-HIGH
// with a settle delay, exactly like the firmware scan.
constexpr uint8_t SIM_ROWS[7] = { 20, 10, 9, 8, 7, 6, 5 };
void simulatedScan() {
    for (uint8_t row = 0; row < 7; ++row) {
        for (uint8_t other = 0; other < 7; ++other) {
            digitalWrite(SIM_ROWS[other], LOW);
        }
        digitalWrite(SIM_ROWS[row], HIGH);
        delayMicroseconds(30);
        digitalWrite(SIM_ROWS[row], LOW);
    }
}
#endif

void setup() {
    Serial.begin(115200);
    while (!Serial && millis() < 2000) delay(10);

    Serial.println();
    Serial.println(F("=== SSD1306 bit-bang I2C test (firmware-identical code) ==="));
#if SIMULATE_MATRIX
    Serial.println(F("MATRIX SIMULATION ON"));
    for (uint8_t i = 0; i < 7; ++i) { pinMode(SIM_ROWS[i], OUTPUT); digitalWrite(SIM_ROWS[i], LOW); }
#endif

    oledBegin();
    Serial.println(F("init done"));
    drawTestImage();
    Serial.println(F("pattern drawn, pushing pages..."));
}

void loop() {
#if SIMULATE_MATRIX
    simulatedScan();
#endif
    const uint32_t t0 = millis();
    for (uint8_t page = 0; page < OLED_HEIGHT / 8; ++page) {
        oledPushPage(page, fb + page * OLED_WIDTH);
    }
    const uint32_t dt = millis() - t0;
    Serial.printf("full frame in %lu ms\n", (unsigned long)dt);
    delay(500);
}
