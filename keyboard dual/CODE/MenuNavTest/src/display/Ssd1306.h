#pragma once

#include <Arduino.h>
#include "../../KeyboardConfig.h"

// Minimal SSD1306 128x64 driver (page-addressing, bit-banged I2C).
//
// The display shares SCL/SDA with matrix rows 3/4 (GPIO7/8). The hardware I2C
// peripheral cannot be used there (the scanner's GPIO writes would reroute
// the pins), so the driver bit-bangs the bus with digitalWrite() on
// open-drain pins — open-drain HIGH doubles as the matrix "active row" level
// via the module's pull-ups, so scanning and I2C interleave without any pin
// reconfiguration. Render into the framebuffer at any time, then call
// update() once per main-loop iteration: each call pushes at most
// OLED_CHUNK_BYTES of GDDRAM and returns immediately, so a full 1 KB frame
// spreads across several matrix scans and is paced to 60 fps.
//
// DISPLAY_ROTATE_180 (compile-time) flips both axes.
class Ssd1306 {
public:
    void begin();          // claims the shared pins as open-drain outputs
    void update();         // push one slice; call every main-loop iteration
    void wake();
    void sleep();

    void clear();
    void pixel(int16_t x, int16_t y, bool on = true);
    bool getPixel(int16_t x, int16_t y) const;
    void hline(int16_t x, int16_t y, int16_t w, bool on = true);
    void rect(int16_t x, int16_t y, int16_t w, int16_t h, bool on = true);
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, bool on = true);
    void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool on = true);
    void circle(int16_t cx, int16_t cy, int16_t r, bool on = true);
    void blit(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *rows); // 1bpp row-major
    void drawChar(int16_t x, int16_t y, char c, bool on = true);
    void drawText(int16_t x, int16_t y, const char *text, bool on = true);
    uint8_t textWidth(const char *text) const;

    bool asleep() const { return _asleep; }
    uint16_t frameCounter() const { return _frameCounter; }

    // --- test-only knobs (MenuNavTest bisect; NOT in the firmware) ----------
    // driveBusLowIdle = true reproduces the firmware behaviour of driving
    // SDA/SCL to MATRIX_ROW_INACTIVE_LEVEL after every transaction; false
    // leaves the bus released like the standalone SSD1306_I2C_Test sketch.
    bool driveBusLowIdle = true;
    void pushFrameNow();           // burst: whole frame back-to-back, no pacing

    static constexpr uint8_t CHAR_W = 5; // 4 px glyph + 1 px spacing
    static constexpr uint8_t CHAR_H = 7; // 6 px glyph + 1 px spacing

private:
    void sendCommand(uint8_t command);
    void pushChunk();

    uint8_t _buffer[Config::OLED_WIDTH * Config::OLED_HEIGHT / 8] = {};
    uint16_t _cursor = 0;            // GDDRAM byte cursor for the current frame
    bool _sending = false;
    bool _asleep = false;
    uint32_t _frameDeadlineMs = 0;
    uint16_t _frameCounter = 0;
};

extern Ssd1306 Display;
