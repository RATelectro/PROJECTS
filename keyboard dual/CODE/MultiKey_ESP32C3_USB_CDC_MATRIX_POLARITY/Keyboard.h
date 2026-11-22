#pragma once

#include "KeyboardConfig.h"
#include "KeyAction.h"
#include "KeymapStore.h"
#include "MatrixScanner.h"
#include "LedController.h"
#include "HidKeyboard.h"
#include "SerialCli.h"
#include "Settings.h"
#include "src/menu/MenuApp.h"
#include "src/multikey/LinkBle.h"
#include "src/games/Games.h"

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

    // Per-layer LED settings (stored inside the keymap).
    LayerLedSettings layerLed(uint8_t layer) const;
    bool setLayerLed(uint8_t layer, const LayerLedSettings &settings);

    // User palettes (stored inside the keymap).
    const Palette *palettes() const { return _data.palettes; }
    Palette *palettes() { return _data.palettes; }
    bool savePalettes() { return _storeReady && _store.save(_data); }

    // Device-wide global settings (rollover, USB/BT, multikey, OLED...).
    GlobalSettings &globals() { return _globals; }
    bool saveGlobals();
    void noteGlobalsChanged() { _globalsDirty = true; _globalsChangedAtMs = millis(); }

    // Preset slots: full global configuration snapshots.
    bool savePresetSlot(uint8_t slot);
    bool loadPresetSlot(uint8_t slot);
    bool presetExists(uint8_t slot) const;

    // Multikey configuration slots on the master.
    bool saveMultikeyConfigSlot(uint8_t slot, const char *name);
    bool loadMultikeyConfigSlot(uint8_t slot);
    bool storeMultikeyBlob(uint8_t slot, const MultikeySlotBlob &blob);

    // Full configuration apply (used by preset load and multikey pushes).
    void applyReceivedConfig(const GlobalSettings &globals, const LedGlobalSettings &led);
    void applyGlobalSettings(); // rollover / BT power / link role from globals

    // LED settings persistence (NVS). Settings are written automatically after
    // a quiet delay; this forces an immediate save (used by the LED SAVE cmd).
    bool saveLedSettings();

    LedController &led() { return _led; }
    HidKeyboard &hid() { return _hid; }
    SerialCli &cli() { return _cli; }
    MenuApp &menu() { return _menu; }
    LinkBle &link() { return _link; }
    GameManager &games() { return _games; }
    bool uiActive() const { return _menu.active(); }
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
    void persistQuietSettings(uint32_t nowMs);

    MatrixScanner _scanner;
    LedController _led;
    HidKeyboard _hid;
    KeymapStore _store;
    SerialCli _cli;
    MenuApp _menu;
    LinkBle _link;
    GameManager _games;
    KeymapData _data = {};
    GlobalSettings _globals = defaultGlobalSettings();
    bool _storeReady = false;
    uint8_t _dirtyLayers = 0;
    bool _metadataDirty = false;
    uint32_t _lastLedSaveAttemptMs = 0;
    bool _globalsDirty = false;
    uint32_t _globalsChangedAtMs = 0;
    uint32_t _lastGlobalsSaveAttemptMs = 0;
    uint32_t _lastKeyActivityMs = 0;

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
