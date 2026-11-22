#include "LedController.h"
#include <esp_system.h>

namespace {

constexpr uint32_t FRAME_INTERVAL_MS = 1000 / Config::LED_FPS;

// Scale elapsed time by an animation speed setting (128 == 1x).
uint32_t scaledTime(uint32_t elapsed, uint8_t speed) {
    return static_cast<uint32_t>((static_cast<uint64_t>(elapsed) * speed) / 128);
}

void fillSolid(CRGB *dst, CRGB color) {
    fill_solid(dst, Config::NUM_KEYS, color);
}

} // namespace

void LedController::begin() {
    FastLED.addLeds<WS2812B, Config::LED_PIN, GRB>(_leds, Config::NUM_KEYS)
        .setCorrection(TypicalLEDStrip);
    FastLED.clear(true);
    FastLED.setBrightness(_s.brightness);
    randomSeed(esp_random());
    _lastActivityMs = millis();
    _lastFrameMs = millis();
}

uint8_t LedController::matrixToLedIndex(uint8_t row, uint8_t col) {
    return (Config::ROWS - 1 - row) + col * Config::ROWS;
}

void LedController::playHello() {
    // Diagonal rainbow sweep in and out; total ~1 s.
    constexpr uint32_t STEP_MS = 45;
    const uint8_t maxStep = Config::ROWS + Config::COLS - 2;
    for (int pass = 0; pass < 2; ++pass) {
        for (uint8_t step = 0; step <= maxStep; ++step) {
            fillSolid(_frame, CRGB::Black);
            for (uint8_t row = 0; row < Config::ROWS; ++row) {
                for (uint8_t col = 0; col < Config::COLS; ++col) {
                    if (row + col != step) continue;
                    const uint8_t hue = map(step, 0, maxStep, 0, 250);
                    _frame[row * Config::COLS + col] = CHSV(hue, 255, 200);
                }
            }
            for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
                _leds[matrixToLedIndex(index / Config::COLS, index % Config::COLS)] = _frame[index];
            }
            FastLED.show();
            delay(STEP_MS);
        }
    }
    FastLED.clear(true);
    _lastFrameMs = millis();
    _lastActivityMs = _lastFrameMs;
}

void LedController::noteSettingsChanged() {
    _settingsDirty = true;
    _settingsChangedAtMs = millis();
}

void LedController::setEnabled(bool enabled) {
    _s.enabled = enabled ? 1 : 0;
    if (!_s.enabled) {
        FastLED.clear(true);
    }
    noteSettingsChanged();
}

void LedController::setBrightness(uint8_t brightness) {
    _s.brightness = brightness;
    FastLED.setBrightness(_s.brightness);
    noteSettingsChanged();
}

void LedController::setEffect(LedEffect effect) {
    _s.effect = effect;
    notifyActivity();
    noteSettingsChanged();
}

void LedController::setColor(CRGB color) {
    _s.colorR = color.r;
    _s.colorG = color.g;
    _s.colorB = color.b;
    noteSettingsChanged();
}

void LedController::setEffectSpeed(uint8_t speed) {
    _s.effectSpeed = speed ? speed : 1;
    noteSettingsChanged();
}

void LedController::setKeypressAnimation(KeypressAnimation animation) {
    _s.keypressAnimation = animation;
    memset(_pulseActive, 0, sizeof(_pulseActive));
    _rippleActive = false;
    noteSettingsChanged();
}

void LedController::setKeypressSpeed(uint8_t speed) {
    _s.keypressSpeed = speed ? speed : 1;
    noteSettingsChanged();
}

void LedController::setSleepAnimation(SleepAnimation animation) {
    _s.sleepAnimation = animation;
    noteSettingsChanged();
}

void LedController::setSleepSpeed(uint8_t speed) {
    _s.sleepSpeed = speed ? speed : 1;
    noteSettingsChanged();
}

void LedController::setSleepTimeout(uint32_t timeoutMs) {
    _s.sleepTimeoutMs = timeoutMs;
    notifyActivity();
    noteSettingsChanged();
}

void LedController::setBleIndicators(bool enabled) {
    _s.bleIndicators = enabled ? 1 : 0;
    noteSettingsChanged();
}

void LedController::setMixMode(LedMixMode mode) {
    _s.mixMode = mode;
    noteSettingsChanged();
}

void LedController::setBacklight(uint8_t brightness, CRGB color) {
    _s.backlightBrightness = brightness;
    _s.backlightR = color.r;
    _s.backlightG = color.g;
    _s.backlightB = color.b;
    noteSettingsChanged();
}

void LedController::setLayerFlash(bool enabled, CRGB nextColor, CRGB prevColor, uint16_t fadeMs) {
    _s.layerFlashEnabled = enabled ? 1 : 0;
    _s.nextLayerColorR = nextColor.r;
    _s.nextLayerColorG = nextColor.g;
    _s.nextLayerColorB = nextColor.b;
    _s.prevLayerColorR = prevColor.r;
    _s.prevLayerColorG = prevColor.g;
    _s.prevLayerColorB = prevColor.b;
    _s.layerFlashFadeMs = fadeMs;
    noteSettingsChanged();
}

void LedController::applyLayerSettings(const LayerLedSettings &layer) {
    _layer = layer;
}

void LedController::applySettings(const LedGlobalSettings &s) {
    _s = s;
    if (_s.effectSpeed == 0) _s.effectSpeed = 128;
    if (_s.keypressSpeed == 0) _s.keypressSpeed = 128;
    if (_s.sleepSpeed == 0) _s.sleepSpeed = 128;
    memset(_pulseActive, 0, sizeof(_pulseActive));
    _rippleActive = false;
    _flashActive = false;
    FastLED.setBrightness(_s.brightness);
}

LedGlobalSettings LedController::captureSettings() const {
    return _s;
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

void LedController::notifyLayerChanged(int8_t newLayer, int8_t prevLayer) {
    if (!_s.layerFlashEnabled || newLayer == prevLayer || prevLayer < 0) return;
    _flashNext = newLayer > prevLayer;
    _flashStartedAt = millis();
    _flashActive = true;
}

void LedController::onKeyPressed(uint8_t keyIndex) {
    if (keyIndex >= Config::NUM_KEYS) return;
    notifyActivity();
    if (_s.keypressAnimation == KEYPRESS_NONE) return;

    _pulseActive[keyIndex] = true;
    _pulseStartedAt[keyIndex] = millis();
    _pulseColor[keyIndex] = _s.keypressAnimation == KEYPRESS_SPARKLE
        ? CHSV(random(0, 256), 255, 255)
        : CRGB(_s.colorR, _s.colorG, _s.colorB);
    if (_s.keypressAnimation == KEYPRESS_SPARKLE ||
        (_s.colorR == 0 && _s.colorG == 0 && _s.colorB == 0)) {
        _pulseColor[keyIndex] = CHSV(random(0, 256), 220, 255);
    }
    if (_s.keypressAnimation == KEYPRESS_RIPPLE) {
        _rippleSource = keyIndex;
        _rippleStartedAt = millis();
        _rippleActive = true;
    }
}

void LedController::setOverlayColor(uint8_t keyIndex, CRGB color) {
    if (keyIndex >= Config::NUM_KEYS) return;
    _overlay[keyIndex] = color;
    _overlaySet[keyIndex] = true;
}

void LedController::clearOverlay() {
    fillSolid(_overlay, CRGB::Black);
    memset(_overlaySet, 0, sizeof(_overlaySet));
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
    _s.effect = LED_CUSTOM;
    _s.custom[ledIndex][0] = color.r;
    _s.custom[ledIndex][1] = color.g;
    _s.custom[ledIndex][2] = color.b;
    noteSettingsChanged();
}

void LedController::clearCustom() {
    memset(_s.custom, 0, sizeof(_s.custom));
}

void LedController::clear() {
    fillSolid(_leds, CRGB::Black);
}

void LedController::show() {
    FastLED.show();
}

CRGB LedController::mixPair(CRGB base, CRGB overlay) {
    switch (_s.mixMode) {
        case MIX_ADD: {
            CRGB out = base;
            out.r = qadd8(out.r, overlay.r);
            out.g = qadd8(out.g, overlay.g);
            out.b = qadd8(out.b, overlay.b);
            return out;
        }
        case MIX_ADD_NORM: {
            const uint16_t r = static_cast<uint16_t>(base.r) + overlay.r;
            const uint16_t g = static_cast<uint16_t>(base.g) + overlay.g;
            const uint16_t b = static_cast<uint16_t>(base.b) + overlay.b;
            const uint16_t peak = max(r, max(g, b));
            if (peak <= 255) return CRGB(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b));
            const uint16_t scale = (255UL * 256) / peak;
            return CRGB(static_cast<uint8_t>((r * scale) / 256),
                        static_cast<uint8_t>((g * scale) / 256),
                        static_cast<uint8_t>((b * scale) / 256));
        }
        case MIX_MAX:
            return CRGB(max(base.r, overlay.r), max(base.g, overlay.g), max(base.b, overlay.b));
        case MIX_SCREEN:
            return CRGB(255 - ((255 - base.r) * (255 - overlay.r) + 127) / 255,
                        255 - ((255 - base.g) * (255 - overlay.g) + 127) / 255,
                        255 - ((255 - base.b) * (255 - overlay.b) + 127) / 255);
        case MIX_HUE: {
            const uint8_t lumaA = base.getLuma();
            const uint8_t lumaB = overlay.getLuma();
            CHSV pick = lumaB >= lumaA ? rgb2hsv_approximate(overlay) : rgb2hsv_approximate(base);
            const uint16_t combined = static_cast<uint16_t>(lumaA) + lumaB;
            pick.v = combined > 255 ? 255 : static_cast<uint8_t>(combined);
            return CRGB(pick);
        }
        default:
            return overlay;
    }
}

void LedController::mixInto(CRGB *dst, const CRGB *src) {
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (src[i] == CRGB::Black) continue;
        dst[i] = mixPair(dst[i], src[i]);
        _affected[i] = true;
    }
}

void LedController::computeBase(CRGB *dst, uint32_t nowMs) {
    uint8_t effect = _s.effect;
    CRGB baseColor = CRGB(_s.colorR, _s.colorG, _s.colorB);
    if (_layer.effect != LAYER_LED_FOLLOW_GLOBAL) {
        effect = _layer.effect;
    }
    if (_layer.colorR != 0 || _layer.colorG != 0 || _layer.colorB != 0) {
        baseColor = CRGB(_layer.colorR, _layer.colorG, _layer.colorB);
    }

    const uint32_t t = scaledTime(nowMs, _s.effectSpeed);
    switch (effect) {
        case LED_STATIC:
            fillSolid(dst, baseColor);
            break;
        case LED_RAINBOW:
            fill_rainbow(dst, Config::NUM_KEYS, t / 20, 7);
            break;
        case LED_BREATHE: {
            CRGB c = baseColor;
            c.nscale8_video(10 + (sin8(t / 8) * 245UL) / 255UL);
            fillSolid(dst, c);
            break;
        }
        case LED_CUSTOM:
            for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
                dst[i] = CRGB(_s.custom[i][0], _s.custom[i][1], _s.custom[i][2]);
            }
            break;
        case LED_OFF:
        default:
            fillSolid(dst, CRGB::Black);
            break;
    }
    if (_layer.brightness != 255) {
        for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) dst[i].nscale8_video(_layer.brightness);
    }
}

void LedController::computeSleep(CRGB *dst, uint32_t nowMs) {
    // Energy saving: while asleep with no BLE link, pulse only key 0 blue
    // instead of animating the whole matrix.
    if (_s.bleIndicators && !_bleConnected) {
        fillSolid(dst, CRGB::Black);
        const uint8_t level = 20 + (sin8(scaledTime(nowMs, 128) / 5) * 235UL) / 255UL;
        CRGB c = CRGB::Blue;
        c.nscale8_video(level);
        dst[0] = c;
        return;
    }
    const uint32_t t = scaledTime(nowMs, _s.sleepSpeed);
    switch (_s.sleepAnimation) {
        case SLEEP_BREATHE: {
            const uint8_t level = 4 + sin8(t / 12) / 8;
            fillSolid(dst, CHSV(170, 255, level));
            break;
        }
        case SLEEP_RAINBOW:
            fill_rainbow(dst, Config::NUM_KEYS, t / 80, 7);
            fadeToBlackBy(dst, Config::NUM_KEYS, 220);
            break;
        case SLEEP_OFF:
        default:
            fillSolid(dst, CRGB::Black);
            break;
    }
}

void LedController::computeFlash(CRGB *dst, uint32_t nowMs) {
    fillSolid(dst, CRGB::Black);
    if (!_flashActive) return;
    const uint32_t elapsed = nowMs - _flashStartedAt;
    if (elapsed >= _s.layerFlashFadeMs) {
        _flashActive = false;
        return;
    }
    const uint8_t amount = 255 - static_cast<uint8_t>(elapsed * 255UL / _s.layerFlashFadeMs);
    CRGB c = _flashNext ? CRGB(_s.nextLayerColorR, _s.nextLayerColorG, _s.nextLayerColorB)
                        : CRGB(_s.prevLayerColorR, _s.prevLayerColorG, _s.prevLayerColorB);
    c.nscale8_video(amount);
    fillSolid(dst, c);
}

void LedController::computeKeypress(CRGB *dst, uint32_t nowMs) {
    fillSolid(dst, CRGB::Black);
    if (_s.keypressAnimation == KEYPRESS_NONE) return;

    const uint32_t speed = _s.keypressSpeed ? _s.keypressSpeed : 1;
    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        if (!_pulseActive[index]) continue;
        const uint32_t elapsed = scaledTime(nowMs - _pulseStartedAt[index], static_cast<uint8_t>(speed));
        if (elapsed >= 450) {
            _pulseActive[index] = false;
            continue;
        }
        const uint8_t amount = 255 - static_cast<uint8_t>(elapsed * 255UL / 450UL);
        dst[index] = _pulseColor[index];
        dst[index].nscale8_video(amount);
    }

    if (_s.keypressAnimation == KEYPRESS_RIPPLE && _rippleActive) {
        const uint32_t elapsed = scaledTime(nowMs - _rippleStartedAt, static_cast<uint8_t>(speed));
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
            CRGB c = _pulseColor[_rippleSource];
            c.nscale8_video(amount);
            // Ripple stacks with the pressed-key pulse through the mixer.
            dst[index] = mixPair(dst[index], c);
        }
    }
}

void LedController::computeBle(CRGB *dst, uint32_t nowMs) {
    if (!_bleConnected) {
        // Disconnected: only the first key pulses blue (energy saving); the
        // rest of the matrix keeps the normal animation.
        fillSolid(dst, CRGB::Black);
        const uint8_t level = 20 + (sin8(scaledTime(nowMs, 128) / 5) * 235UL) / 255UL;
        CRGB c = CRGB::Blue;
        c.nscale8_video(level);
        dst[0] = c;
        return;
    }

    const uint32_t elapsed = nowMs - _bleChangedAtMs;
    const uint32_t litCount = min(static_cast<uint32_t>(Config::NUM_KEYS),
                                  static_cast<uint32_t>(elapsed * Config::NUM_KEYS / 1200UL + 1));
    const uint8_t lit = static_cast<uint8_t>(litCount);
    fillSolid(dst, CRGB::Black);
    for (uint8_t i = 0; i < lit; ++i) dst[i] = CHSV(96, 255, 220);
}

void LedController::applyBacklightFloor() {
    if (_sleeping || _s.backlightBrightness == 0) return;
    const CRGB glow = CRGB(_s.backlightR, _s.backlightG, _s.backlightB);
    const uint8_t minV = gammaBrightness(_s.backlightBrightness);
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (!_affected[i]) {
            // Idle key: backlight indicates the key with its colour.
            if (_frame[i].getLuma() < minV / 4) {
                _frame[i] = glow;
                _frame[i].nscale8_video(minV);
            }
        } else if (_frame[i].getLuma() < minV) {
            // Animated key: backlight only raises the minimum brightness.
            CHSV hsv = rgb2hsv_approximate(_frame[i]);
            if (hsv.v < minV) {
                hsv.v = minV;
                _frame[i] = CRGB(hsv);
            }
        }
    }
}

void LedController::renderFrame(uint32_t nowMs) {
    if (_sleeping) {
        computeSleep(_frame, nowMs);
    } else {
        computeBase(_frame, nowMs);
        computeFlash(_plane, nowMs);
        mixInto(_frame, _plane);
        computeKeypress(_plane, nowMs);
        mixInto(_frame, _plane);
        const bool connectionSuccess = _bleConnected &&
            static_cast<uint32_t>(nowMs - _bleChangedAtMs) < 1200;
        if (_s.bleIndicators && (!_bleConnected || connectionSuccess)) {
            computeBle(_plane, nowMs);
            mixInto(_frame, _plane);
        }
        applyBacklightFloor();
    }

    // Overlay plane (menus/games) is always on top.
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (!_overlaySet[i]) continue;
        _frame[i] = _overlay[i];
        _affected[i] = true;
    }

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        _leds[matrixToLedIndex(index / Config::COLS, index % Config::COLS)] = _frame[index];
    }
    FastLED.show();

    clearOverlay();
    memset(_affected, 0, sizeof(_affected));
}

void LedController::update(uint32_t nowMs) {
    if (!_s.enabled) return;
    if (static_cast<uint32_t>(nowMs - _lastFrameMs) < FRAME_INTERVAL_MS) return;
    _lastFrameMs = nowMs;

    FastLED.setBrightness(_s.brightness);
    _sleeping = _s.sleepTimeoutMs != 0 &&
                static_cast<uint32_t>(nowMs - _lastActivityMs) >= _s.sleepTimeoutMs;
    renderFrame(nowMs);
}
