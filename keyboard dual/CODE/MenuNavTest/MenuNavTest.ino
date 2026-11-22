/*
 * MenuNavTest — bisect harness for the firmware's black OLED.
 *
 * The standalone SSD1306_I2C_Test works on this hardware; the firmware does
 * not. This sketch starts as an exact clone of the working baseline and adds
 * ONE firmware ingredient per 8-second phase, announced on Serial (115200),
 * so a single flash shows which ingredient kills the display:
 *
 *   Phase 0  BASELINE    whole-frame burst push, bus left RELEASED between
 *                        transactions, no scanner, no FastLED.
 *                        (= SSD1306_I2C_Test with menu text)
 *   Phase 1  CHUNKED     one 128-byte page per loop iteration (firmware's
 *                        Ssd1306::update pacing), bus still released.
 *   Phase 2  BUS-LOW     + drive SDA/SCL LOW between transactions (firmware's
 *                        "hand pins back to the scanner" writes).
 *   Phase 3  SCANNER     + real MatrixScanner running every loop (firmware
 *                        order: chunk push, then scan). Key presses print to
 *                        Serial in picture numbering from here on.
 *   Phase 4  FASTLED     + LED strip rendering (green base, blue pulse on
 *                        key 1, RED nav-key overlay). Full firmware replica.
 *   Phase 5  NO-SHARED   scanning continues but rows 3/4 (SDA/SCL) are never
 *                        driven, so the scanner produces zero bus edges. Runs
 *                        forever. If the display froze in P3/P4 but survives
 *                        here, the scanner's edges on the shared lines are
 *                        the killer (panel I2C desync).
 *
 * The panel is re-initialised at every phase change, so each phase starts
 * from a known-good display; in P3/P4 expect the SCANS counter on screen to
 * freeze a few seconds in if the desync theory holds. Watch the serial
 * heartbeat: frames(+N) tells whether the ESP32 still streams, and the
 * SDA/SCL l/r readbacks show the bus state at the freeze moment.
 * In P5 the nav keys on the shared rows (up/down/right/enter) obviously
 * don't scan; left and all other keys still print.
 *
 * If the menu is visible in phase N but gone in N+1, the ingredient added in
 * N+1 is the culprit. If it never appears even in phase 0, power-cycle the
 * keyboard (unplug USB) — the panel latches corrupted states until power-off.
 *
 * Board: ESP32C3 Dev Module, "USB CDC On Boot: Enabled".
 */

#include <Arduino.h>
#include <FastLED.h>
#include "KeyboardConfig.h"
#include "MatrixScanner.h"
#include "src/display/Ssd1306.h"

// ---------------------------------------------------------------------------
// Nav keys (firmware matrix indices): picture 11/17/19/25/18.
// ---------------------------------------------------------------------------
constexpr uint8_t NAV_UP = 16;
constexpr uint8_t NAV_LEFT = 12;
constexpr uint8_t NAV_RIGHT = 22;
constexpr uint8_t NAV_DOWN = 18;
constexpr uint8_t NAV_ENTER = 17;
constexpr uint8_t MENU_NAV_BRIGHTNESS = 128; // direct 0..255, like the firmware

constexpr uint32_t PHASE_MS = 8000;
constexpr uint8_t PHASE_COUNT = 6;

MatrixScanner scanner;
CRGB leds[Config::NUM_KEYS];

const char *ITEMS[] = { "KEY CFG", "LED CFG", "HID & CONNECT", "GAMES", "PRESETS", "EXIT" };
constexpr uint8_t ITEM_COUNT = 6;
uint8_t selection = 0;

uint8_t phase = 0;
uint32_t phaseStartedMs = 0;
uint32_t scanCounter = 0;
int16_t lastPressedPic = -1;
uint32_t lastEnterMs = 0;
uint32_t lastBurstMs = 0;
uint32_t lastLedMs = 0;
uint32_t lastHeartbeatMs = 0;
uint16_t lastFrames = 0;

uint8_t matrixToLedIndex(uint8_t row, uint8_t col) {
    return (Config::ROWS - 1 - row) + col * Config::ROWS;
}

// Read back both bus lines in both states WITHOUT forming I2C START/STOP
// conditions (SCL is pulled low first and released last, so SDA never moves
// while SCL is high). Catches damaged pad drivers and dead pull-up paths
// (loose header, cracked pull-up on the module) live, at any time.
void busReadback(int &sdaLow, int &sdaRel, int &sclLow, int &sclRel) {
    constexpr uint8_t SDA = Config::OLED_PIN_SDA;
    constexpr uint8_t SCL = Config::OLED_PIN_SCL;
    digitalWrite(SCL, LOW);
    digitalWrite(SDA, LOW);
    delayMicroseconds(50);
    sdaLow = digitalRead(SDA);
    sclLow = digitalRead(SCL);
    digitalWrite(SDA, HIGH); // release (SCL still low -> not a STOP)
    delayMicroseconds(50);
    sdaRel = digitalRead(SDA);
    digitalWrite(SCL, HIGH); // release
    delayMicroseconds(50);
    sclRel = digitalRead(SCL);
}

void gpioSelfTest() {
    pinMode(Config::OLED_PIN_SDA, OUTPUT_OPEN_DRAIN);
    pinMode(Config::OLED_PIN_SCL, OUTPUT_OPEN_DRAIN);
    int sdaLow, sdaRel, sclLow, sclRel;
    busReadback(sdaLow, sdaRel, sclLow, sclRel);
    Serial.println("GPIO self-test (healthy: l=0 r=1 on both lines):");
    Serial.printf("  SDA(GPIO%u) l=%d r=%d   SCL(GPIO%u) l=%d r=%d\n",
                  Config::OLED_PIN_SDA, sdaLow, sdaRel,
                  Config::OLED_PIN_SCL, sclLow, sclRel);
    Serial.println("  l reads 1 -> ESP32 pull-down dead; r reads 0 -> pull-up path dead.");
}

uint8_t pictureKey(uint8_t index) { // firmware index -> picture key 1..35
    return (index % Config::COLS) * Config::ROWS + index / Config::COLS + 1;
}

bool justPressed(uint8_t index) {
    return scanner.changed(index) && scanner.isPressed(index);
}

void applyPhaseFlags() {
    // Phase 0: burst + bus released + no scan + no FastLED.
    Display.driveBusLowIdle = phase >= 2;
    scanner.scanSharedRows = phase < 5; // P5: scan without touching SDA/SCL
    Serial.printf("--- PHASE %u: %s ---\n", phase,
                  phase == 0 ? "BASELINE (burst, bus released, no scan, no FastLED)" :
                  phase == 1 ? "CHUNKED (1 page/loop, bus released)" :
                  phase == 2 ? "BUS-LOW IDLE (SDA/SCL driven low between transactions)" :
                  phase == 3 ? "SCANNER ON (full scan every loop)" :
                  phase == 4 ? "FASTLED ON (full firmware replica)" :
                               "NO-SHARED-SCAN (scan skips SDA/SCL rows; runs forever)");
    // Fresh, known-good panel for every phase (a freeze in one phase must not
    // carry into the next).
    Display.begin();
}

void handleNav(uint32_t nowMs) {
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (!justPressed(i)) continue;
        lastPressedPic = pictureKey(i);
        Serial.printf("key %u pressed (index %u)\n", lastPressedPic, i);
    }
    if (justPressed(NAV_UP))   selection = (selection + ITEM_COUNT - 1) % ITEM_COUNT;
    if (justPressed(NAV_DOWN)) selection = (selection + 1) % ITEM_COUNT;
    if (justPressed(NAV_ENTER)) {
        lastEnterMs = nowMs;
        Serial.printf("ENTER -> %s\n", ITEMS[selection]);
    }
}

void drawMenu(uint32_t nowMs) {
    Display.clear();
    char title[12];
    snprintf(title, sizeof(title), "MAIN MENU P%u", phase);
    Display.drawText(1, 0, title, true);
    Display.hline(0, 8, Config::OLED_WIDTH, true);
    for (uint8_t i = 0; i < ITEM_COUNT; ++i) {
        const uint8_t y = 12 + i * 7;
        if (i == selection) Display.drawText(1, y, ">", true); // arrow cursor
        Display.drawText(8, y, ITEMS[i], true);
    }
    char line[26];
    if (lastEnterMs != 0 && nowMs - lastEnterMs < 1500) {
        snprintf(line, sizeof(line), "ENTER: %s", ITEMS[selection]);
    } else if (lastPressedPic >= 0) {
        snprintf(line, sizeof(line), "KEY %d SCANS %lu", lastPressedPic,
                 (unsigned long)scanCounter);
    } else {
        snprintf(line, sizeof(line), "SCANS %lu", (unsigned long)scanCounter);
    }
    Display.drawText(1, 58, line, true);
}

void renderLeds(uint32_t nowMs) {
    // Base stand-in: static green, blue "BLE waiting" pulse on key 0.
    CRGB frame[Config::NUM_KEYS];
    fill_solid(frame, Config::NUM_KEYS, CRGB(0, 20, 0));
    const uint8_t level = 20 + (sin8(nowMs / 5) * 235UL) / 255UL;
    frame[0] = CRGB::Blue;
    frame[0].nscale8_video(level);

    // Overlay plane LAST (MenuApp::setOverlay equivalent): nav keys can never
    // be covered by an animation. Direct 0..255, no gamma.
    const CRGB navColor(MENU_NAV_BRIGHTNESS, 0, 0);
    const CRGB enterColor = CHSV(96, 255, qadd8(MENU_NAV_BRIGHTNESS, 60));
    const uint8_t navKeys[4] = { NAV_UP, NAV_LEFT, NAV_RIGHT, NAV_DOWN };
    for (uint8_t n = 0; n < 4; ++n) {
        CRGB c = navColor;
        if (scanner.isPressed(navKeys[n])) c = CRGB(qadd8(c.r, 120), qadd8(c.g, 120), qadd8(c.b, 120));
        frame[navKeys[n]] = c;
    }
    frame[NAV_ENTER] = scanner.isPressed(NAV_ENTER) ? CRGB(255, 255, 255) : enterColor;

    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        leds[matrixToLedIndex(i / Config::COLS, i % Config::COLS)] = frame[i];
    }
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("MenuNavTest: display bisect, phases of 8 s each.");
    Serial.println("If phase 0 is black too, POWER-CYCLE the keyboard and retry.");

    gpioSelfTest();
    scanner.begin();
    // FastLED is NOT initialised here; it starts on entering phase 4, so
    // phases 0-3 are an exact clone of the working SSD1306_I2C_Test setup.
    // applyPhaseFlags() sets the phase-0 flags and runs Display.begin().

    phaseStartedMs = millis();
    applyPhaseFlags();
}

void loop() {
    const uint32_t nowMs = millis();

    if (phase < PHASE_COUNT - 1 && nowMs - phaseStartedMs >= PHASE_MS) {
        ++phase;
        phaseStartedMs = nowMs;
        applyPhaseFlags();
    }

    const bool burst = phase == 0;
    const bool scan = phase >= 3;
    const bool ledsOn = phase >= 4;

    static bool fastLedStarted = false;
    if (ledsOn && !fastLedStarted) {
        fastLedStarted = true;
        FastLED.addLeds<WS2812B, Config::LED_PIN, GRB>(leds, Config::NUM_KEYS)
            .setCorrection(TypicalLEDStrip);
        FastLED.setBrightness(Config::DEFAULT_BRIGHTNESS);
        FastLED.clear(true);
    }

    if (burst) {
        // Clone of the working SSD1306_I2C_Test loop: whole frame, then pause.
        if (nowMs - lastBurstMs >= 500) {
            lastBurstMs = nowMs;
            drawMenu(nowMs);
            Display.pushFrameNow();
        }
    } else {
        Display.update(); // firmware path: one 128-byte chunk per loop
        if (scan) {
            scanner.update(nowMs);
            ++scanCounter;
            handleNav(nowMs);
        }
        drawMenu(nowMs);
    }

    if (ledsOn && nowMs - lastLedMs >= 16) { // ~60 fps, like the firmware
        lastLedMs = nowMs;
        renderLeds(nowMs);
    }

    // Heartbeat: live bus health + frame progress, every 2 s. If the panel
    // freezes, "frames" stops increasing; if SCL/SDA misbehaves electrically
    // (dead pull-up or pad), the l/r values show it the moment it happens.
    if (nowMs - lastHeartbeatMs >= 2000) {
        lastHeartbeatMs = nowMs;
        int sdaLow, sdaRel, sclLow, sclRel;
        busReadback(sdaLow, sdaRel, sclLow, sclRel);
        const uint16_t frames = Display.frameCounter();
        Serial.printf("t=%lus P%u frames=%u(+%u) scans=%lu SDA[l=%d r=%d] SCL[l=%d r=%d]\n",
                      (unsigned long)(nowMs / 1000), phase, frames,
                      static_cast<uint16_t>(frames - lastFrames),
                      (unsigned long)scanCounter, sdaLow, sdaRel, sclLow, sclRel);
        lastFrames = frames;
    }
    delay(1);
}
