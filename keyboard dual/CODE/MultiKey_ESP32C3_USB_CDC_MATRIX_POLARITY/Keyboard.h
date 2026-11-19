#pragma once

#include "KeyboardConfig.h"
#include "KeyAction.h"
#include "KeymapStore.h"
#include "MatrixScanner.h"
#include "LedController.h"
#include "HidKeyboard.h"
#include "SerialCli.h"

enum KeyEventType : uint8_t {
    KEY_EVENT_PRESS = 0,
    KEY_EVENT_RELEASE = 1,
    KEY_EVENT_HOLD = 2
};

struct KeyEvent {
    KeyEventType type;
    uint8_t index;
    uint8_t row;
    uint8_t col;
    uint8_t layer;
    uint32_t pressedAtMs;
    uint32_t heldMs;
    KeyAction action;
};

typedef void (*KeyEventCallback)(const KeyEvent &event, void *context);
typedef void (*LayerEventCallback)(uint8_t activeLayer, uint8_t baseLayer, void *context);
typedef void (*BleEventCallback)(bool connected, void *context);

class MultiKey {
public:
    MultiKey();

    void begin();
    void update();

    // Debounced key state and edge detection for the current update() pass.
    bool isPressed(uint8_t index) const;
    bool rawIsPressed(uint8_t index) const;
    bool justPressed(uint8_t index) const;
    bool justReleased(uint8_t index) const;
    uint32_t holdDuration(uint8_t index) const;
    bool heldFor(uint8_t index, uint32_t durationMs) const;
    bool isHeld(uint8_t index) const; // Uses the configured HOLD_DUR_MS value.
    bool holdThresholdReached(uint8_t index) const;
    uint64_t pressedMask() const { return _scanner.pressedMask(); }

    void onKeyEvent(KeyEventCallback callback, void *context = nullptr);
    void onLayerEvent(LayerEventCallback callback, void *context = nullptr);
    void onBleEvent(BleEventCallback callback, void *context = nullptr);

    uint16_t holdDurationThreshold() const { return _data.holdMs; }
    bool setHoldDurationThreshold(uint16_t durationMs);

    // Layer state. MOMENTARY layers override the base; SWITCH and MODAL set it.
    uint8_t activeLayer() const;
    uint8_t baseLayer() const { return _data.baseLayer; }
    bool setBaseLayer(uint8_t layer);
    uint8_t layerCount() const { return _data.layerCount; }
    bool setLayerCount(uint8_t count);
    const char *layerName(uint8_t layer) const;
    bool setLayerName(uint8_t layer, const char *name);

    // RAM keymap editing, explicit NVS save/recall, and compiled defaults.
    KeyAction keyAction(uint8_t layer, uint8_t index) const;
    bool setKeyAction(uint8_t layer, uint8_t index, const KeyAction &action);
    bool saveKeymaps(int layer = -1);   // -1 = all layers and metadata.
    bool recallKeymaps(int layer = -1); // -1 = all layers and metadata.
    void resetKeymaps(int layer = -1);  // RAM only; save explicitly if desired.
    uint8_t dirtyLayers() const { return _dirtyLayers; }
    bool metadataDirty() const { return _metadataDirty; }
    bool storageAvailable() const { return _storeReady; }

    // LED settings persistence (NVS). Settings are written automatically after
    // a quiet delay; this forces an immediate save (used by the LED SAVE cmd).
    bool saveLedSettings();

    LedController &led() { return _led; }
    HidKeyboard &hid() { return _hid; }
    SerialCli &cli() { return _cli; }
    void processSerialCommand(const char *line) { _cli.processLine(line); }

private:
    KeyAction resolveAction(uint8_t index) const;
    void handlePress(uint8_t index, uint32_t nowMs);
    void handleRelease(uint8_t index, uint32_t nowMs);
    void applyLayerPress(uint8_t index, const KeyAction &action);
    void applyLayerRelease(uint8_t index, const KeyAction &action);
    void emitKeyEvent(KeyEventType type, uint8_t index, uint32_t nowMs);
    void layerChanged();
    void keymapChanged();
    void markAllDirty();
    void clearMomentaryLayers();

    MatrixScanner _scanner;
    LedController _led;
    HidKeyboard _hid;
    KeymapStore _store;
    SerialCli _cli;
    KeymapData _data = {};
    bool _storeReady = false;
    uint8_t _dirtyLayers = 0;
    bool _metadataDirty = false;
    uint32_t _lastLedSaveAttemptMs = 0;

    bool _physicalPressed[Config::NUM_KEYS] = {};
    KeyAction _latched[Config::NUM_KEYS] = {};
    uint32_t _pressedAt[Config::NUM_KEYS] = {};
    bool _holdFired[Config::NUM_KEYS] = {};

    int8_t _momentaryKeys[Config::NUM_KEYS];
    uint8_t _momentaryTargets[Config::NUM_KEYS] = {};
    uint8_t _momentaryCount = 0;
    uint8_t _modalReturnLayer[Config::NUM_KEYS] = {};
    bool _lastBleConnected = false;

    KeyEventCallback _keyCallback = nullptr;
    LayerEventCallback _layerCallback = nullptr;
    BleEventCallback _bleCallback = nullptr;
    void *_keyContext = nullptr;
    void *_layerContext = nullptr;
    void *_bleContext = nullptr;
};
