#pragma once

#include <FastLED.h>
#include "KeyboardConfig.h"

enum LedEffect : uint8_t {
    LED_OFF = 0,
    LED_STATIC = 1,
    LED_RAINBOW = 2,
    LED_BREATHE = 3,
    LED_CUSTOM = 4
};

enum KeypressAnimation : uint8_t {
    KEYPRESS_NONE = 0,
    KEYPRESS_PULSE = 1,
    KEYPRESS_SPARKLE = 2,
    KEYPRESS_RIPPLE = 3
};

enum SleepAnimation : uint8_t {
    SLEEP_OFF = 0,
    SLEEP_BREATHE = 1,
    SLEEP_RAINBOW = 2
};

// Persistent snapshot of the LED configuration: global animation settings
// plus the custom per-key colors. Written to NVS as a CRC-checked blob and
// loaded at boot, so LED settings survive power loss.
struct __attribute__((packed)) LedSettings {
    uint8_t enabled;
    uint8_t brightness;
    uint8_t effect;
    uint8_t keypressAnimation;
    uint8_t sleepAnimation;
    uint8_t bleIndicators;
    uint8_t colorR;
    uint8_t colorG;
    uint8_t colorB;
    uint32_t sleepTimeoutMs;
    uint8_t custom[Config::NUM_KEYS][3];
};

static_assert(sizeof(LedSettings) == 118, "LedSettings wire/storage size changed");

class LedController {
public:
    void begin();
    void update(uint32_t nowMs);
    void playHello();

    void onKeyPressed(uint8_t keyIndex);
    void notifyActivity();
    void setBleConnected(bool connected);

    void setEnabled(bool enabled);
    bool enabled() const { return _enabled; }
    void setBrightness(uint8_t brightness);
    uint8_t brightness() const { return _brightness; }
    void setEffect(LedEffect effect);
    LedEffect effect() const { return _effect; }
    void setColor(CRGB color) { _color = color; noteSettingsChanged(); }
    CRGB color() const { return _color; }
    void setKeypressAnimation(KeypressAnimation animation);
    KeypressAnimation keypressAnimation() const { return _keypressAnimation; }
    void setSleepAnimation(SleepAnimation animation);
    SleepAnimation sleepAnimation() const { return _sleepAnimation; }
    void setSleepTimeout(uint32_t timeoutMs);
    uint32_t sleepTimeout() const { return _sleepTimeoutMs; }
    bool sleeping() const { return _sleeping; }
    void setBleIndicators(bool enabled) { _bleIndicators = enabled; noteSettingsChanged(); }
    bool bleIndicators() const { return _bleIndicators; }

    // Persistence: every settings mutation marks the controller dirty; the
    // owner (MultiKey) writes captureSettings() to NVS after a quiet delay,
    // or immediately when LED SAVE is issued.
    void applySettings(const LedSettings &settings);  // loaded value: no dirty mark
    LedSettings captureSettings() const;
    bool settingsDirty() const { return _settingsDirty; }
    uint32_t settingsChangedAtMs() const { return _settingsChangedAtMs; }
    void markSettingsSaved() { _settingsDirty = false; }

    // Manual control switches to LED_CUSTOM. Key indices use matrix order.
    void setKeyColor(uint8_t keyIndex, CRGB color);
    void setKeyColor(uint8_t row, uint8_t col, CRGB color);
    void setRawLedColor(uint8_t ledIndex, CRGB color);
    void clearCustom();
    void clear();
    void show();

    static uint8_t matrixToLedIndex(uint8_t row, uint8_t col);

private:
    void renderBase(uint32_t nowMs);
    void renderBle(uint32_t nowMs);
    void renderKeypresses(uint32_t nowMs);
    void noteSettingsChanged();

    CRGB _leds[Config::NUM_KEYS];
    CRGB _custom[Config::NUM_KEYS];
    CRGB _color = CRGB(0, 20, 0);
    uint8_t _brightness = Config::DEFAULT_BRIGHTNESS;
    bool _enabled = true;
    LedEffect _effect = LED_OFF;
    KeypressAnimation _keypressAnimation = KEYPRESS_NONE;
    SleepAnimation _sleepAnimation = SLEEP_BREATHE;
    uint32_t _sleepTimeoutMs = Config::DEFAULT_SLEEP_TIMEOUT_MS;
    uint32_t _lastActivityMs = 0;
    uint32_t _lastFrameMs = 0;
    bool _sleeping = false;

    bool _bleIndicators = true;
    bool _bleConnected = false;
    uint32_t _bleChangedAtMs = 0;

    bool _settingsDirty = false;
    uint32_t _settingsChangedAtMs = 0;

    bool _pulseActive[Config::NUM_KEYS] = {};
    uint32_t _pulseStartedAt[Config::NUM_KEYS] = {};
    CRGB _pulseColor[Config::NUM_KEYS] = {};
    uint32_t _rippleStartedAt = 0;
    uint8_t _rippleSource = 0;
    bool _rippleActive = false;
};
