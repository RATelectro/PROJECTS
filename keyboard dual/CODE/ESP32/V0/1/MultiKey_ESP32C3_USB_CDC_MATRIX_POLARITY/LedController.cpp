#include "LedController.h"
#include <esp_system.h>

void LedController::begin() {
    FastLED.addLeds<WS2812B, Config::LED_PIN, GRB>(_leds, Config::NUM_KEYS)
        .setCorrection(TypicalLEDStrip);
    FastLED.clear(true);
    FastLED.setBrightness(_brightness);
    randomSeed(esp_random());
    _lastActivityMs = millis();
    clearCustom();
}

uint8_t LedController::matrixToLedIndex(uint8_t row, uint8_t col) {
    return (Config::ROWS - 1 - row) + col * Config::ROWS;
}

void LedController::playHello() {
    for (int step = 0; step < (Config::COLS * Config::ROWS) - 1; ++step) {
        for (int row = 0; row < Config::COLS; ++row) {
            const int col = step - row;
            if (col >= 0 && col < Config::ROWS) {
                const int ledIndex = row * Config::ROWS + col;
                const uint8_t hue = map(step, 0, (Config::COLS * Config::ROWS) - 2, 0, 250);
                _leds[ledIndex] = CHSV(hue, 255, 200);
            }
        }
        FastLED.show();
        delay(Config::HELLO_SWEEP_SPEED_MS);
    }
    for (int step = 0; step < (Config::COLS * Config::ROWS) - 1; ++step) {
        for (int row = 0; row < Config::COLS; ++row) {
            const int col = step - row;
            if (col >= 0 && col < Config::ROWS) {
                _leds[row * Config::ROWS + col] = CRGB::Black;
            }
        }
        FastLED.show();
        delay(Config::HELLO_SWEEP_SPEED_MS);
    }
    FastLED.clear(true);
    _lastFrameMs = millis();
    _lastActivityMs = _lastFrameMs;
}

void LedController::setEnabled(bool enabled) {
    _enabled = enabled;
    if (!_enabled) FastLED.clear(true);
}

void LedController::setBrightness(uint8_t brightness) {
    _brightness = brightness;
    FastLED.setBrightness(_brightness);
}

void LedController::setEffect(LedEffect effect) {
    _effect = effect;
    notifyActivity();
}

void LedController::setKeypressAnimation(KeypressAnimation animation) {
    _keypressAnimation = animation;
    memset(_pulseActive, 0, sizeof(_pulseActive));
    _rippleActive = false;
}

void LedController::setSleepAnimation(SleepAnimation animation) {
    _sleepAnimation = animation;
}

void LedController::setSleepTimeout(uint32_t timeoutMs) {
    _sleepTimeoutMs = timeoutMs;
    notifyActivity();
}

void LedController::notifyActivity() {
    _lastActivityMs = millis();
    _sleeping = false;
}

void LedController::setBleConnected(bool connected) {
    if (_bleConnected == connected) return;
    _bleConnected = connected;
    _bleChangedAtMs = millis();
}

void LedController::onKeyPressed(uint8_t keyIndex) {
    if (keyIndex >= Config::NUM_KEYS) return;
    notifyActivity();
    if (_keypressAnimation == KEYPRESS_NONE) return;

    _pulseActive[keyIndex] = true;
    _pulseStartedAt[keyIndex] = millis();
    _pulseColor[keyIndex] = _keypressAnimation == KEYPRESS_SPARKLE
        ? CHSV(random(0, 256), 255, 255)
        : _color;
    if (_keypressAnimation == KEYPRESS_SPARKLE || _color == CRGB::Black) {
        _pulseColor[keyIndex] = CHSV(random(0, 256), 220, 255);
    }
    if (_keypressAnimation == KEYPRESS_RIPPLE) {
        _rippleSource = keyIndex;
        _rippleStartedAt = millis();
        _rippleActive = true;
    }
}

void LedController::setKeyColor(uint8_t keyIndex, CRGB color) {
    if (keyIndex >= Config::NUM_KEYS) return;
    setKeyColor(keyIndex / Config::COLS, keyIndex % Config::COLS, color);
}

void LedController::setKeyColor(uint8_t row, uint8_t col, CRGB color) {
    if (row >= Config::ROWS || col >= Config::COLS) return;
    setRawLedColor(matrixToLedIndex(row, col), color);
}

void LedController::setRawLedColor(uint8_t ledIndex, CRGB color) {
    if (ledIndex >= Config::NUM_KEYS) return;
    _effect = LED_CUSTOM;
    _custom[ledIndex] = color;
    _leds[ledIndex] = color;
}

void LedController::clearCustom() {
    fill_solid(_custom, Config::NUM_KEYS, CRGB::Black);
}

void LedController::clear() {
    fill_solid(_leds, Config::NUM_KEYS, CRGB::Black);
}

void LedController::show() {
    FastLED.show();
}

void LedController::renderBle(uint32_t nowMs) {
    if (!_bleConnected) {
        const uint8_t level = 20 + (sin8(nowMs / 5) * 235UL) / 255UL;
        fill_solid(_leds, Config::NUM_KEYS, CRGB::Blue);
        fadeToBlackBy(_leds, Config::NUM_KEYS, 255 - level);
        return;
    }

    const uint32_t elapsed = nowMs - _bleChangedAtMs;
    const uint32_t litCount = min(static_cast<uint32_t>(Config::NUM_KEYS),
                                  static_cast<uint32_t>(elapsed * Config::NUM_KEYS / 1200UL + 1));
    const uint8_t lit = static_cast<uint8_t>(litCount);
    fill_solid(_leds, Config::NUM_KEYS, CRGB::Black);
    for (uint8_t i = 0; i < lit; ++i) _leds[i] = CHSV(96, 255, 220);
}

void LedController::renderBase(uint32_t nowMs) {
    if (_sleeping) {
        switch (_sleepAnimation) {
            case SLEEP_BREATHE: {
                const uint8_t level = 4 + sin8(nowMs / 12) / 8;
                fill_solid(_leds, Config::NUM_KEYS, CHSV(170, 255, level));
                break;
            }
            case SLEEP_RAINBOW:
                fill_rainbow(_leds, Config::NUM_KEYS, nowMs / 80, 7);
                fadeToBlackBy(_leds, Config::NUM_KEYS, 220);
                break;
            case SLEEP_OFF:
            default:
                fill_solid(_leds, Config::NUM_KEYS, CRGB::Black);
                break;
        }
        return;
    }

    switch (_effect) {
        case LED_STATIC:
            fill_solid(_leds, Config::NUM_KEYS, _color);
            break;
        case LED_RAINBOW:
            fill_rainbow(_leds, Config::NUM_KEYS, nowMs / 20, 7);
            break;
        case LED_BREATHE: {
            CRGB frameColor = _color;
            frameColor.nscale8_video(10 + (sin8(nowMs / 8) * 245UL) / 255UL);
            fill_solid(_leds, Config::NUM_KEYS, frameColor);
            break;
        }
        case LED_CUSTOM:
            memcpy(_leds, _custom, sizeof(_leds));
            break;
        case LED_OFF:
        default:
            fill_solid(_leds, Config::NUM_KEYS, CRGB::Black);
            break;
    }
}

void LedController::renderKeypresses(uint32_t nowMs) {
    if (_keypressAnimation == KEYPRESS_NONE) return;

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        if (!_pulseActive[index]) continue;
        const uint32_t elapsed = nowMs - _pulseStartedAt[index];
        if (elapsed >= 450) {
            _pulseActive[index] = false;
            continue;
        }
        if (_keypressAnimation != KEYPRESS_RIPPLE) {
            const uint8_t amount = 255 - static_cast<uint8_t>(elapsed * 255UL / 450UL);
            const uint8_t row = index / Config::COLS;
            const uint8_t col = index % Config::COLS;
            const uint8_t led = matrixToLedIndex(row, col);
            _leds[led] = blend(_leds[led], _pulseColor[index], amount);
        }
    }

    if (_keypressAnimation == KEYPRESS_RIPPLE && _rippleActive) {
        const uint32_t elapsed = nowMs - _rippleStartedAt;
        if (elapsed > 1000) {
            _rippleActive = false;
            return;
        }
        const uint8_t sourceRow = _rippleSource / Config::COLS;
        const uint8_t sourceCol = _rippleSource % Config::COLS;
        for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
            const uint8_t row = index / Config::COLS;
            const uint8_t col = index % Config::COLS;
            const uint8_t distance = abs(static_cast<int>(row) - sourceRow) +
                                     abs(static_cast<int>(col) - sourceCol);
            const uint32_t delayMs = distance * 65UL;
            if (elapsed < delayMs || elapsed - delayMs > 350) continue;
            const uint8_t amount = 255 - static_cast<uint8_t>((elapsed - delayMs) * 255UL / 350UL);
            const uint8_t led = matrixToLedIndex(row, col);
            _leds[led] = blend(_leds[led], _pulseColor[_rippleSource], amount);
        }
    }
}

void LedController::update(uint32_t nowMs) {
    if (static_cast<uint32_t>(nowMs - _lastFrameMs) < Config::LED_UPDATE_INTERVAL_MS) return;
    _lastFrameMs = nowMs;

    if (!_enabled) return;
    FastLED.setBrightness(_brightness);
    _sleeping = _sleepTimeoutMs != 0 &&
                static_cast<uint32_t>(nowMs - _lastActivityMs) >= _sleepTimeoutMs;

    const bool connectionSuccess = _bleConnected &&
        static_cast<uint32_t>(nowMs - _bleChangedAtMs) < 1200;
    if (_bleIndicators && (!_bleConnected || connectionSuccess)) {
        renderBle(nowMs);
    } else {
        renderBase(nowMs);
        renderKeypresses(nowMs);
    }
    FastLED.show();
}
