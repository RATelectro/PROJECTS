#pragma once

#include <FastLED.h>
#include "KeyboardConfig.h"
#include "Settings.h"

// LedController renders the 7x5 LED matrix as a framebuffer that is refreshed
// at LED_FPS (60). Every animation family (base effect, layer-change flash,
// keypress fx, BLE indicator, menu/game overlay) is drawn into its own colour
// plane and then combined with the configured mix algorithm, so effects stack
// instead of overwriting each other and never collapse into dull black/white.
class LedController {
public:
    void begin();
    void update(uint32_t nowMs);
    void playHello();

    void onKeyPressed(uint8_t keyIndex);
    void notifyActivity();
    void setBleConnected(bool connected);
    void notifyLayerChanged(int8_t newLayer, int8_t prevLayer);

    void setEnabled(bool enabled);
    bool enabled() const { return _s.enabled; }
    void setBrightness(uint8_t brightness);
    uint8_t brightness() const { return _s.brightness; }
    void setEffect(LedEffect effect);
    LedEffect effect() const { return static_cast<LedEffect>(_s.effect); }
    void setColor(CRGB color);
    CRGB color() const { return CRGB(_s.colorR, _s.colorG, _s.colorB); }
    void setEffectSpeed(uint8_t speed);
    uint8_t effectSpeed() const { return _s.effectSpeed; }
    void setKeypressAnimation(KeypressAnimation animation);
    KeypressAnimation keypressAnimation() const { return static_cast<KeypressAnimation>(_s.keypressAnimation); }
    void setKeypressSpeed(uint8_t speed);
    uint8_t keypressSpeed() const { return _s.keypressSpeed; }
    void setSleepAnimation(SleepAnimation animation);
    SleepAnimation sleepAnimation() const { return static_cast<SleepAnimation>(_s.sleepAnimation); }
    void setSleepSpeed(uint8_t speed);
    uint8_t sleepSpeed() const { return _s.sleepSpeed; }
    void setSleepTimeout(uint32_t timeoutMs);
    uint32_t sleepTimeout() const { return _s.sleepTimeoutMs; }
    bool sleeping() const { return _sleeping; }
    void setBleIndicators(bool enabled);
    bool bleIndicators() const { return _s.bleIndicators != 0; }
    void setMixMode(LedMixMode mode);
    LedMixMode mixMode() const { return static_cast<LedMixMode>(_s.mixMode); }
    void setBacklight(uint8_t brightness, CRGB color);
    uint8_t backlightBrightness() const { return _s.backlightBrightness; }
    CRGB backlightColor() const { return CRGB(_s.backlightR, _s.backlightG, _s.backlightB); }
    void setLayerFlash(bool enabled, CRGB nextColor, CRGB prevColor, uint16_t fadeMs);
    bool layerFlashEnabled() const { return _s.layerFlashEnabled != 0; }

    // Per-layer LED settings travel with the keymap; the owner pushes the
    // active layer's settings whenever the layer changes.
    void applyLayerSettings(const LayerLedSettings &layer);
    const LayerLedSettings &layerSettings() const { return _layer; }

    // Persistence: every settings mutation marks the controller dirty; the
    // owner writes captureSettings() to NVS after a quiet delay, or
    // immediately on LED SAVE.
    void applySettings(const LedGlobalSettings &settings); // loaded value: no dirty mark
    LedGlobalSettings captureSettings() const;
    bool settingsDirty() const { return _settingsDirty; }
    uint32_t settingsChangedAtMs() const { return _settingsChangedAtMs; }
    void markSettingsSaved() { _settingsDirty = false; }

    // Overlay plane for menus/games: cleared every rendered frame.
    void setOverlayColor(uint8_t keyIndex, CRGB color);
    void clearOverlay();

    // Manual control switches to LED_CUSTOM. Key indices use matrix order.
    void setKeyColor(uint8_t keyIndex, CRGB color);
    void setKeyColor(uint8_t row, uint8_t col, CRGB color);
    void setRawLedColor(uint8_t ledIndex, CRGB color);
    void clearCustom();
    void clear();
    void show();

    static uint8_t matrixToLedIndex(uint8_t row, uint8_t col);

private:
    void renderFrame(uint32_t nowMs);
    void computeBase(CRGB *dst, uint32_t nowMs);
    void computeSleep(CRGB *dst, uint32_t nowMs);
    void computeFlash(CRGB *dst, uint32_t nowMs);
    void computeKeypress(CRGB *dst, uint32_t nowMs);
    void renderAnimationEvents(CRGB *dst, uint32_t nowMs);
    void expireAnimationEvents(uint32_t nowMs);
    void addAnimationEvent(uint8_t type, uint8_t keyIndex, CRGB color, uint8_t speed, uint16_t durationMs);
    void computeBle(CRGB *dst, uint32_t nowMs);
    void applyBacklightFloor();
    void mixInto(CRGB *dst, const CRGB *src);
    CRGB mixPair(CRGB base, CRGB overlay);
    void noteSettingsChanged();

    CRGB _leds[Config::NUM_KEYS];     // physical output in LED strip order
    CRGB _frame[Config::NUM_KEYS];    // combined framebuffer, matrix order
    CRGB _plane[Config::NUM_KEYS];    // scratch plane for one animation source
    CRGB _overlay[Config::NUM_KEYS];  // menu/game overlay plane
    bool _overlaySet[Config::NUM_KEYS] = {};
    bool _affected[Config::NUM_KEYS] = {}; // key touched by fx/overlay this frame

    LedGlobalSettings _s = defaultLedGlobalSettings();
    LayerLedSettings _layer = defaultLayerLedSettings();

    uint32_t _lastActivityMs = 0;
    uint32_t _lastFrameMs = 0;
    bool _sleeping = false;

    bool _bleConnected = false;
    uint32_t _bleChangedAtMs = 0;

    bool _settingsDirty = false;
    uint32_t _settingsChangedAtMs = 0;

    // Fixed event pool: independent animation instances, no heap allocation.
    // Capacity 32 comfortably exceeds 6KRO held-key concurrency and allows
    // rapid press/layer-change bursts without replacing recent events.
    static constexpr uint8_t ANIMATION_EVENT_CAPACITY = 32;
    enum AnimationEventType : uint8_t { ANIM_PULSE = 0, ANIM_RIPPLE = 1, ANIM_LAYER_FLASH = 2 };
    struct AnimationEvent {
        bool active = false;
        uint8_t type = ANIM_PULSE;
        uint8_t keyIndex = 0;
        uint8_t speed = 128;
        CRGB color = CRGB::Black;
        uint32_t startedAt = 0;
        uint16_t durationMs = 450;
    };
    AnimationEvent _animationEvents[ANIMATION_EVENT_CAPACITY] = {};

};
