#include "Ssd1306.h"
#include "../../KeyboardConfig.h"
#include "Font4x6.h"

Ssd1306 Display;

// Bit-banged I2C on GPIO7/GPIO8. These pins are matrix rows 3/4, so the
// hardware I2C peripheral can never stay attached (pinMode/digitalWrite from
// the scanner would reroute it). Both pins are configured OUTPUT_OPEN_DRAIN
// once in begin(); from then on everything is plain digitalWrite():
//   HIGH = released (display module pull-ups take the line high),
//   LOW  = pulled low.
// The matrix scanner's HIGH/LOW writes on these rows do exactly the right
// thing for both roles, so scanning and I2C chunks interleave freely in the
// main loop without any pin reconfiguration.
//
// Ordering rule (both sides must follow it): whenever SDA and SCL are pulled
// LOW outside a transaction, SCL (GPIO7, row 4) must fall BEFORE SDA (GPIO8,
// row 3). SDA falling while SCL is high is a START condition; spurious ones
// (hand-back, scanner preamble) desynchronize wedge-prone panels.
//
// Timing: digitalWrite() on the C3 is only tens of ns, far faster than the
// SSD1306 input spec, so every edge is padded with i2cDelay() to keep the
// bus at ~400 kHz (t_low/t_high >= 1 us). Without the padding the bus runs
// at ~3 MHz and the panel latches corrupted data — garbled glyphs, and one
// corrupted command byte can leave the screen black until power cycle.
namespace {

inline uint16_t bufferIndex(uint8_t x, uint8_t y) {
#if DISPLAY_ROTATE_180
    x = Config::OLED_WIDTH - 1 - x;
    y = Config::OLED_HEIGHT - 1 - y;
#endif
    return static_cast<uint16_t>(x) + static_cast<uint16_t>(y / 8) * Config::OLED_WIDTH;
}

inline uint8_t bufferBit(uint8_t y) {
#if DISPLAY_ROTATE_180
    y = Config::OLED_HEIGHT - 1 - y;
#endif
    return static_cast<uint8_t>(y & 7);
}

constexpr uint8_t OLED_SDA = Config::OLED_PIN_SDA;
constexpr uint8_t OLED_SCL = Config::OLED_PIN_SCL;

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

// Returns true when the slave ACKed (pulled SDA low on the 9th clock).
uint8_t i2cWriteByte(uint8_t value) {
    for (int8_t bit = 7; bit >= 0; --bit) {
        if ((value >> bit) & 1) sdaHigh();
        else sdaLow();
        i2cDelay();
        sclHigh();
        i2cDelay();
        sclLow();
        i2cDelay();
    }
    sdaHigh();  // release SDA so the slave can ACK
    i2cDelay();
    sclHigh();
    i2cDelay();
    const uint8_t ack = digitalRead(OLED_SDA) == LOW ? 1 : 0;
    sclLow();
    i2cDelay();
    return ack;
}

inline uint8_t i2cWriteAddress() {
    return i2cWriteByte(static_cast<uint8_t>(Config::OLED_I2C_ADDR << 1)); // write mode
}

// Unstick a slave left mid-byte (e.g. by an unplug or a reset mid-transfer):
// 9 SCL clocks flush its bit counter, then a STOP releases the bus.
void busRecover() {
    sdaHigh();
    for (uint8_t i = 0; i < 9; ++i) {
        sclHigh();
        i2cDelay();
        sclLow();
        i2cDelay();
    }
    i2cStop();
}

} // namespace

void Ssd1306::noteNack() {
    if (++_nacks >= 3 && !_offline) {
        // Several transactions went unanswered: the panel is unplugged,
        // power-cycled, or wedged. update() will retry the full init.
        _offline = true;
        _sending = false;
        _nacks = 0;
        _initRetryMs = millis() + 500;
    }
}

void Ssd1306::sendCommand(uint8_t command) {
    i2cStart();
    if (!i2cWriteAddress()) {
        i2cStop();
        digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
        digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);
        noteNack();
        return;
    }
    _nacks = 0;
    i2cWriteByte(0x80); // control byte: Co=1, one command follows
    i2cWriteByte(command);
    i2cStop();
    // Rest the shared lines LOW for the matrix scanner. SCL must fall BEFORE
    // SDA: after i2cStop() both lines are released, and SDA dropping while
    // SCL is still high would be a spurious I2C START for the panel.
    digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
    digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);
}

// One full init attempt: bus recovery, init stream, GDDRAM wipe. Returns true
// only if the panel ACKed its address. A cold (just powered or reconnected)
// panel frequently ignores the first transaction, so callers retry.
bool Ssd1306::tryInit() {
    static const uint8_t init[] = {
        0xAE,       // display off
        0xD5, 0x80, // clock divide
        0xA8, 0x3F, // multiplex 64
        0xD3, 0x00, // display offset
        0x40,       // start line 0
        0x8D, 0x14, // charge pump on
        0x20, 0x02, // page addressing mode
        0xA1,       // segment remap (flip horizontally when rotated)
        0xC8,       // COM scan dec (flip vertically when rotated)
        0xDA, 0x12, // COM pins
        0x81, 0xCF, // contrast
        0xD9, 0xF1, // pre-charge
        0xDB, 0x40, // VCOMH level
        0xA4,       // resume RAM
        0xA6,       // non-inverted
        0xAF        // display on
    };

    busRecover();
    i2cStart();
    if (!i2cWriteAddress()) {
        i2cStop();
        digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
        digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);
        return false;
    }
    i2cWriteByte(0x00); // command stream (Co=0)
    for (uint8_t i = 0; i < sizeof(init); ++i) i2cWriteByte(init[i]);
    i2cStop();

    // Clean the GDDRAM: push a full zero frame so no previous content from
    // before the reset can linger while the first real frame streams in.
    for (uint8_t page = 0; page < Config::OLED_HEIGHT / 8; ++page) {
        i2cStart();
        i2cWriteAddress();
        i2cWriteByte(0x80); // command: page select
        i2cWriteByte(static_cast<uint8_t>(0xB0 | page));
        i2cWriteByte(0x80); // command: column low nibble 0
        i2cWriteByte(0x00);
        i2cWriteByte(0x80); // command: column high nibble 0
        i2cWriteByte(0x10);
        i2cWriteByte(0x40); // data stream
        for (uint16_t i = 0; i < Config::OLED_WIDTH; ++i) i2cWriteByte(0x00);
        i2cStop();
    }

    // Rows 3/4 must rest at the inactive level for the matrix scanner; SCL
    // falls before SDA so the hand-back cannot form a spurious START (see
    // sendCommand).
    digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
    digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);

    _offline = false;
    _nacks = 0;
    _sending = false;
    _cursor = 0;
    clear();
    _frameDeadlineMs = millis();
    return true;
}

void Ssd1306::begin() {
    // Take the shared pins over as open-drain outputs, bus idle (released).
    // After this the scanner may only digitalWrite() them (which it does).
    digitalWrite(OLED_SDA, HIGH);
    digitalWrite(OLED_SCL, HIGH);
    pinMode(OLED_SDA, OUTPUT_OPEN_DRAIN);
    pinMode(OLED_SCL, OUTPUT_OPEN_DRAIN);
    delay(50); // let the panel power up

    // A cold panel does not always take the first init; try a few times,
    // then let update() keep retrying in the background.
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
        if (tryInit()) return;
        delay(100);
    }
    _initRetryMs = millis() + 500;
}

void Ssd1306::sleep() {
    if (_asleep) return;
    sendCommand(0xAE); // display off
    sendCommand(0x8D);
    sendCommand(0x10); // charge pump off
    _asleep = true;
}

void Ssd1306::wake() {
    if (!_asleep) return;
    sendCommand(0x8D);
    sendCommand(0x14);
    sendCommand(0xAF);
    _asleep = false;
    _frameDeadlineMs = millis();
}

void Ssd1306::clear() {
    memset(_buffer, 0, sizeof(_buffer));
}

void Ssd1306::pixel(int16_t x, int16_t y, bool on) {
    if (x < 0 || y < 0 || x >= Config::OLED_WIDTH || y >= Config::OLED_HEIGHT) return;
    const uint16_t index = bufferIndex(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    const uint8_t mask = static_cast<uint8_t>(1U << bufferBit(static_cast<uint8_t>(y)));
    if (on) _buffer[index] |= mask;
    else _buffer[index] &= static_cast<uint8_t>(~mask);
}

bool Ssd1306::getPixel(int16_t x, int16_t y) const {
    if (x < 0 || y < 0 || x >= Config::OLED_WIDTH || y >= Config::OLED_HEIGHT) return false;
    const uint16_t index = bufferIndex(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
    return (_buffer[index] >> bufferBit(static_cast<uint8_t>(y))) & 1;
}

void Ssd1306::hline(int16_t x, int16_t y, int16_t w, bool on) {
    for (int16_t i = 0; i < w; ++i) pixel(x + i, y, on);
}

void Ssd1306::rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    hline(x, y, w, on);
    hline(x, y + h - 1, w, on);
    for (int16_t i = 1; i + 1 < h; ++i) {
        pixel(x, y + i, on);
        pixel(x + w - 1, y + i, on);
    }
}

void Ssd1306::fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
    for (int16_t j = 0; j < h; ++j) hline(x, y + j, w, on);
}

void Ssd1306::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on) {
    const int16_t dx = abs(x1 - x0);
    const int16_t dy = abs(y1 - y0);
    const int16_t sx = x0 < x1 ? 1 : -1;
    const int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    while (true) {
        pixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) break;
        const int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

void Ssd1306::circle(int16_t cx, int16_t cy, int16_t r, bool on) {
    int16_t x = -r;
    int16_t y = 0;
    int16_t err = 2 - 2 * r;
    while (x < 0) {
        pixel(cx - x, cy + y, on);
        pixel(cx - y, cy - x, on);
        pixel(cx + x, cy - y, on);
        pixel(cx + y, cy + x, on);
        r = err;
        if (r <= y) err += ++y * 2 + 1;
        if (r > x || err > y) err += ++x * 2 + 1;
    }
}

void Ssd1306::blit(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *rows) {
    for (uint8_t j = 0; j < h; ++j) {
        for (uint8_t i = 0; i < w; ++i) {
            const bool on = (rows[j] >> (7 - i)) & 1;
            pixel(x + i, y + j, on);
        }
    }
}

void Ssd1306::drawChar(int16_t x, int16_t y, char c, bool on) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = FONT4X6[static_cast<uint8_t>(c) - 32];
    for (uint8_t row = 0; row < 6; ++row) {
        const uint8_t bits = pgm_read_byte_near(glyph + row);
        for (uint8_t col = 0; col < 4; ++col) {
            if ((bits >> (3 - col)) & 1) pixel(x + col, y + row, on);
        }
    }
}

void Ssd1306::drawText(int16_t x, int16_t y, const char *text, bool on) {
    int16_t cursor = x;
    while (*text != '\0') {
        if (*text == '\n') {
            cursor = x;
            y += CHAR_H;
        } else {
            drawChar(cursor, y, *text, on);
            cursor += CHAR_W;
        }
        ++text;
    }
}

uint8_t Ssd1306::textWidth(const char *text) const {
    uint8_t width = 0;
    while (text[width] != '\0') ++width;
    return width * CHAR_W;
}

void Ssd1306::pushChunk() {
    // Page addressing: one chunk = one 128-byte page. Commands use Co=1
    // control bytes so page/column select and the data stream share a single
    // transaction.
    const uint8_t page = static_cast<uint8_t>(_cursor / Config::OLED_WIDTH);
    i2cStart();
    if (!i2cWriteAddress()) {
        // Panel gone or wedged mid-frame: bail out, hand the lines back, and
        // let noteNack()/update() schedule a re-init.
        i2cStop();
        digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
        digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);
        noteNack();
        return;
    }
    _nacks = 0;
    i2cWriteByte(0x80); // command: page select
    i2cWriteByte(static_cast<uint8_t>(0xB0 | page));
    i2cWriteByte(0x80); // command: column low nibble 0
    i2cWriteByte(0x00);
    i2cWriteByte(0x80); // command: column high nibble 0
    i2cWriteByte(0x10);
    i2cWriteByte(0x40); // data stream follows
    const uint16_t remaining = static_cast<uint16_t>(sizeof(_buffer)) - _cursor;
    const uint16_t count = remaining < Config::OLED_CHUNK_BYTES ? remaining
                                                                : Config::OLED_CHUNK_BYTES;
    for (uint16_t i = 0; i < count; ++i) i2cWriteByte(_buffer[_cursor + i]);
    i2cStop();
    _cursor = static_cast<uint16_t>(_cursor + count);

    // Hand the lines to the matrix scanner (inactive rows LOW); SCL falls
    // before SDA so this cannot form a spurious START (see sendCommand).
    digitalWrite(OLED_SCL, Config::MATRIX_ROW_INACTIVE_LEVEL);
    digitalWrite(OLED_SDA, Config::MATRIX_ROW_INACTIVE_LEVEL);
}

void Ssd1306::update() {
    if (_asleep) return;
    const uint32_t now = millis();

    if (_offline) {
        // Panel absent, cold, or wedged: retry the full init twice a second.
        // tryInit() on an absent panel costs one short failed transaction.
        if (static_cast<int32_t>(now - _initRetryMs) >= 0) {
            _initRetryMs = now + 500;
            tryInit();
        }
        return;
    }

    if (!_sending) {
        if (static_cast<int32_t>(now - _frameDeadlineMs) < 0) return;
        _cursor = 0;
        _sending = true;
    }

    pushChunk();

    if (_cursor >= sizeof(_buffer)) {
        _sending = false;
        ++_frameCounter;
        // Pace to 60 fps.
        _frameDeadlineMs = now + (1000 / 60);
    }
}
