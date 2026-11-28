#include "Keyboard.h"
#include "Keymaps.h"
#include "src/display/Ssd1306.h"
#include "src/led/Palettes.h"
#include <string.h>

MultiKey::MultiKey() : _cli(*this) {
    clearMomentaryLayers();
}

void MultiKey::begin() {
    Serial0.end();
    Serial.begin(115200);
    delay(100);

    _storeReady = _store.begin();

    // Device-wide settings first: the BLE device name depends on them.
    if (_storeReady) {
        GlobalSettings stored;
        if (_store.loadGlobal(stored)) _globals = stored;
    }

    bool loaded = _storeReady && _store.load(_data);
    if (!loaded) {
        memset(&_data, 0, sizeof(_data));
        _data.layerCount = Config::DEFAULT_LAYER_COUNT;
        _data.baseLayer = 0;
        _data.holdMs = HOLD_DUR_MS;
        memcpy(_data.layerNames, DEFAULT_LAYER_NAMES, sizeof(_data.layerNames));
        loadDefaultKeymaps(_data.layers);
        for (uint8_t layer = 0; layer < Config::MAX_LAYERS; ++layer) {
            _data.layerLed[layer] = defaultLayerLedSettings();
        }
        initDefaultPalettes(_data.palettes);
        if (_storeReady) _store.save(_data);
    }

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        _latched[index] = hidAction();
    }
    _scanner.begin();

    Display.begin();
    _hid.begin(*this);
    _led.begin();
    if (_storeReady) {
        LedGlobalSettings ledSettings;
        if (_store.loadLed(ledSettings)) _led.applySettings(ledSettings);
    }
    _led.applyLayerSettings(_data.layerLed[_data.baseLayer]);
    applyGlobalSettings();

    _menu.begin(*this);
    _link.begin(*this);
    _cli.begin();

    _led.playHello(); // 1 s rainbow sweep
    _menu.open();     // boot chooser (or straight to the menu if disabled)

    _lastBleConnected = _hid.isConnected();
    _led.setBleConnected(_lastBleConnected);
}

void MultiKey::update() {
    const uint32_t nowMs = millis();
    _scanner.update(nowMs);

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        if (!_scanner.changed(index)) continue;
        _physicalPressed[index] = _scanner.isPressed(index);
        if (_physicalPressed[index]) handlePress(index, nowMs);
        else handleRelease(index, nowMs);
    }

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        if (_physicalPressed[index] && !_holdFired[index] &&
            static_cast<uint32_t>(nowMs - _pressedAt[index]) >= _data.holdMs) {
            _holdFired[index] = true;
            emitKeyEvent(KEY_EVENT_HOLD, index, nowMs);
        }
    }

    // The menu is only offered at boot (red-key combo) or via the CLI; there
    // is no hold-a-key shortcut to reopen it after EXIT.

    const bool connected = _hid.isConnected();
    if (connected != _lastBleConnected) {
        _lastBleConnected = connected;
        _led.setBleConnected(connected);
        _cli.emitBleEvent(connected);
        if (_bleCallback) _bleCallback(connected, _bleContext);
    }

    // Menus/games own the keys and the display while active.
    _menu.update(nowMs);
    Display.update(); // one chunked I2C slice per loop

    if (!_menu.active()) {
        _hid.update(_physicalPressed, _latched);
    }
    _led.update(nowMs);
    _link.update();
    _cli.update();

    // OLED sleep on inactivity (menus and key presses wake it).
    if (_globals.oledSleepTimeoutMs != 0 && !_menu.active() &&
        static_cast<uint32_t>(nowMs - _lastKeyActivityMs) >= _globals.oledSleepTimeoutMs) {
        Display.sleep();
    }

    persistQuietSettings(nowMs);
    delay(1);
}

void MultiKey::persistQuietSettings(uint32_t nowMs) {
    if (!_storeReady) return;
    if (_led.settingsDirty() &&
        static_cast<uint32_t>(nowMs - _led.settingsChangedAtMs()) >= Config::LED_SETTINGS_SAVE_DELAY_MS &&
        static_cast<uint32_t>(nowMs - _lastLedSaveAttemptMs) >= Config::LED_SETTINGS_SAVE_RETRY_MS) {
        _lastLedSaveAttemptMs = nowMs;
        if (_store.saveLed(_led.captureSettings())) _led.markSettingsSaved();
    }
    if (_globalsDirty &&
        static_cast<uint32_t>(nowMs - _globalsChangedAtMs) >= Config::LED_SETTINGS_SAVE_DELAY_MS &&
        static_cast<uint32_t>(nowMs - _lastGlobalsSaveAttemptMs) >= Config::LED_SETTINGS_SAVE_RETRY_MS) {
        _lastGlobalsSaveAttemptMs = nowMs;
        if (saveGlobals()) _globalsDirty = false;
    }
}

bool MultiKey::isPressed(uint8_t index) const {
    return _scanner.isPressed(index);
}

bool MultiKey::rawIsPressed(uint8_t index) const {
    return _scanner.rawIsPressed(index);
}

bool MultiKey::justPressed(uint8_t index) const {
    return _scanner.changed(index) && _scanner.isPressed(index);
}

bool MultiKey::justReleased(uint8_t index) const {
    return _scanner.changed(index) && !_scanner.isPressed(index);
}

uint32_t MultiKey::holdDuration(uint8_t index) const {
    if (!isPressed(index)) return 0;
    return static_cast<uint32_t>(millis() - _pressedAt[index]);
}

bool MultiKey::heldFor(uint8_t index, uint32_t durationMs) const {
    return isPressed(index) && holdDuration(index) >= durationMs;
}

bool MultiKey::isHeld(uint8_t index) const {
    return heldFor(index, _data.holdMs);
}

bool MultiKey::holdThresholdReached(uint8_t index) const {
    return index < Config::NUM_KEYS && isPressed(index) && _holdFired[index];
}

void MultiKey::onKeyEvent(KeyEventCallback callback, void *context) {
    _keyCallback = callback;
    _keyContext = context;
}

void MultiKey::onLayerEvent(LayerEventCallback callback, void *context) {
    _layerCallback = callback;
    _layerContext = context;
}

void MultiKey::onBleEvent(BleEventCallback callback, void *context) {
    _bleCallback = callback;
    _bleContext = context;
}

bool MultiKey::saveLedSettings() {
    if (!_storeReady) return false;
    const bool ok = _store.saveLed(_led.captureSettings());
    if (ok) _led.markSettingsSaved();
    return ok;
}

bool MultiKey::saveGlobals() {
    if (!_storeReady) return false;
    const bool ok = _store.saveGlobal(_globals);
    if (ok) _globalsDirty = false;
    return ok;
}

void MultiKey::applyGlobalSettings() {
    _hid.setRollover(static_cast<RolloverMode>(_globals.rollover));
    _link.applySettings();
}

void MultiKey::applyReceivedConfig(const GlobalSettings &globals,
                                   const LedGlobalSettings &led) {
    _globals = globals;
    _led.applySettings(led);
    _led.applyLayerSettings(_data.layerLed[activeLayer()]);
    applyGlobalSettings();
    saveGlobals();
    saveLedSettings();
    _hid.requestReport();
}

bool MultiKey::savePresetSlot(uint8_t slot) {
    if (!_storeReady || slot >= MULTIKEY_SLOT_COUNT) return false;
    PresetBlob blob;
    blob.led = _led.captureSettings();
    blob.global = _globals;
    return _store.savePreset(slot, blob);
}

bool MultiKey::loadPresetSlot(uint8_t slot) {
    PresetBlob blob;
    if (!_storeReady || !_store.loadPreset(slot, blob)) return false;
    applyReceivedConfig(blob.global, blob.led);
    return true;
}

bool MultiKey::presetExists(uint8_t slot) const {
    return _storeReady && _store.presetExists(slot);
}

bool MultiKey::saveMultikeyConfigSlot(uint8_t slot, const char *name) {
    if (!_storeReady || slot >= MULTIKEY_SLOT_COUNT) return false;
    MultikeySlotBlob blob = {};
    if (name != nullptr) {
        strncpy(blob.name, name, sizeof(blob.name) - 1);
    }
    blob.config.led = _led.captureSettings();
    blob.config.global = _globals;
    return _store.saveMultikeySlot(slot, blob);
}

bool MultiKey::loadMultikeyConfigSlot(uint8_t slot) {
    MultikeySlotBlob blob;
    if (!_storeReady || !_store.loadMultikeySlot(slot, blob)) return false;
    applyReceivedConfig(blob.config.global, blob.config.led);
    return true;
}

bool MultiKey::storeMultikeyBlob(uint8_t slot, const MultikeySlotBlob &blob) {
    if (!_storeReady || slot >= MULTIKEY_SLOT_COUNT) return false;
    return _store.saveMultikeySlot(slot, blob);
}

LayerLedSettings MultiKey::layerLed(uint8_t layer) const {
    if (layer >= Config::MAX_LAYERS) return defaultLayerLedSettings();
    return _data.layerLed[layer];
}

bool MultiKey::setLayerLed(uint8_t layer, const LayerLedSettings &settings) {
    if (layer >= _data.layerCount) return false;
    _data.layerLed[layer] = settings;
    markAllDirty();
    if (layer == activeLayer()) _led.applyLayerSettings(settings);
    return true;
}

bool MultiKey::setHoldDurationThreshold(uint16_t durationMs) {
    if (durationMs == 0) return false;
    _data.holdMs = durationMs;
    markAllDirty();
    return true;
}

uint8_t MultiKey::activeLayer() const {
    if (_momentaryCount > 0) return _momentaryTargets[_momentaryCount - 1];
    return _data.baseLayer;
}

bool MultiKey::setBaseLayer(uint8_t layer) {
    if (layer >= _data.layerCount) return false;
    if (_data.baseLayer != layer) {
        const uint8_t previous = _data.baseLayer;
        _data.baseLayer = layer;
        _led.notifyLayerChanged(layer, previous);
        layerChanged();
    }
    return true;
}

bool MultiKey::setLayerCount(uint8_t count) {
    if (count == 0 || count > Config::MAX_LAYERS) return false;
    _data.layerCount = count;
    if (_data.baseLayer >= count) _data.baseLayer = 0;
    clearMomentaryLayers();
    layerChanged();
    markAllDirty();
    return true;
}

const char *MultiKey::layerName(uint8_t layer) const {
    if (layer >= Config::MAX_LAYERS) return "";
    return _data.layerNames[layer];
}

bool MultiKey::setLayerName(uint8_t layer, const char *name) {
    if (layer >= _data.layerCount || name == nullptr || *name == '\0') return false;
    uint8_t i = 0;
    for (; i < Config::LAYER_NAME_LENGTH - 1 && name[i] != '\0'; ++i) {
        const uint8_t ch = static_cast<uint8_t>(name[i]);
        _data.layerNames[layer][i] = (ch >= 32 && ch <= 126) ? ch : ' ';
    }
    _data.layerNames[layer][i] = '\0';
    markAllDirty();
    return true;
}

KeyAction MultiKey::keyAction(uint8_t layer, uint8_t index) const {
    if (layer >= Config::MAX_LAYERS || index >= Config::NUM_KEYS) return hidAction();
    return _data.layers[layer][index];
}

bool MultiKey::setKeyAction(uint8_t layer, uint8_t index, const KeyAction &action) {
    if (layer >= _data.layerCount || index >= Config::NUM_KEYS ||
        action.type > ACTION_TRANSPARENT) return false;
    if (action.type == ACTION_HID && action.usage > 0xE7) return false;
    if (action.type == ACTION_LAYER &&
        (action.layer >= _data.layerCount || action.layerMode > LAYER_MODAL)) return false;

    KeyAction normalized = action;
    if (normalized.type == ACTION_TRANSPARENT) normalized = transparentAction();
    if (normalized.type == ACTION_LAYER) {
        normalized.usage = 0;
        normalized.modifiers = 0;
    } else if (normalized.type == ACTION_HID) {
        normalized.layer = 0;
        normalized.layerMode = 0;
    }
    _data.layers[layer][index] = normalized;
    _dirtyLayers |= static_cast<uint8_t>(1U << layer);
    keymapChanged();
    return true;
}

bool MultiKey::saveKeymaps(int layer) {
    if (!_storeReady || layer < -1 || layer >= _data.layerCount) return false;
    const bool ok = layer < 0 ? _store.save(_data) : _store.saveLayer(_data, layer);
    if (ok) {
        if (layer < 0) {
            _dirtyLayers = 0;
            _metadataDirty = false;
        } else {
            _dirtyLayers &= static_cast<uint8_t>(~(1U << layer));
        }
    }
    return ok;
}

bool MultiKey::recallKeymaps(int layer) {
    if (!_storeReady || layer < -1 || layer >= _data.layerCount) return false;
    KeymapData saved;
    if (!_store.load(saved)) return false;

    if (layer < 0) {
        _data = saved;
        clearMomentaryLayers();
        _dirtyLayers = 0;
        _metadataDirty = false;
        layerChanged();
    } else {
        memcpy(_data.layers[layer], saved.layers[layer], sizeof(_data.layers[layer]));
        _data.layerLed[layer] = saved.layerLed[layer];
        _dirtyLayers &= static_cast<uint8_t>(~(1U << layer));
    }
    keymapChanged();
    return true;
}

void MultiKey::resetKeymaps(int layer) {
    if (layer < -1 || layer >= _data.layerCount) return;
    KeymapData defaults = {};
    defaults.layerCount = Config::DEFAULT_LAYER_COUNT;
    defaults.holdMs = HOLD_DUR_MS;
    memcpy(defaults.layerNames, DEFAULT_LAYER_NAMES, sizeof(defaults.layerNames));
    loadDefaultKeymaps(defaults.layers);
    for (uint8_t l = 0; l < Config::MAX_LAYERS; ++l) {
        defaults.layerLed[l] = defaultLayerLedSettings();
    }
    initDefaultPalettes(defaults.palettes);

    if (layer < 0) {
        const uint32_t generation = _data.generation;
        _data = defaults;
        _data.generation = generation;
        _data.baseLayer = 0;
        clearMomentaryLayers();
        markAllDirty();
        layerChanged();
    } else {
        memcpy(_data.layers[layer], defaults.layers[layer], sizeof(_data.layers[layer]));
        _data.layerLed[layer] = defaults.layerLed[layer];
        _dirtyLayers |= static_cast<uint8_t>(1U << layer);
    }
    keymapChanged();
}

KeyAction MultiKey::resolveAction(uint8_t index) const {
    int8_t layer = activeLayer();
    while (layer >= 0) {
        const KeyAction action = _data.layers[layer][index];
        if (action.type != ACTION_TRANSPARENT) {
            if (action.type == ACTION_LAYER && action.layer >= _data.layerCount) return hidAction();
            return action;
        }
        --layer;
    }
    return hidAction();
}

void MultiKey::handlePress(uint8_t index, uint32_t nowMs) {
    _pressedAt[index] = nowMs;
    _holdFired[index] = false;
    _latched[index] = resolveAction(index);
    _led.onKeyPressed(index);
    _lastKeyActivityMs = nowMs;
    if (Display.asleep()) Display.wake();
    if (_menu.active()) {
        emitKeyEvent(KEY_EVENT_PRESS, index, nowMs);
        return; // menus consume keys; no HID, no layer switching
    }
    if (_latched[index].type == ACTION_LAYER) applyLayerPress(index, _latched[index]);
    emitKeyEvent(KEY_EVENT_PRESS, index, nowMs);
    // Slaves forward resolved HID events to the master keyboard.
    if (_globals.multikeyEnabled && _globals.multikeyId > 0 &&
        _latched[index].type == ACTION_HID) {
        _link.onKeyEvent(_latched[index].usage, _latched[index].modifiers, true);
    }
    _hid.requestReport();
}

void MultiKey::handleRelease(uint8_t index, uint32_t nowMs) {
    if (_menu.active()) {
        emitKeyEvent(KEY_EVENT_RELEASE, index, nowMs);
        return;
    }
    if (_latched[index].type == ACTION_LAYER) applyLayerRelease(index, _latched[index]);
    emitKeyEvent(KEY_EVENT_RELEASE, index, nowMs);
    if (_globals.multikeyEnabled && _globals.multikeyId > 0 &&
        _latched[index].type == ACTION_HID) {
        _link.onKeyEvent(_latched[index].usage, _latched[index].modifiers, false);
    }
    _hid.requestReport();
}

void MultiKey::applyLayerPress(uint8_t index, const KeyAction &action) {
    const uint8_t target = action.layer;
    if (action.layerMode == LAYER_MOMENTARY) {
        if (_momentaryCount < Config::NUM_KEYS) {
            _momentaryKeys[_momentaryCount] = index;
            _momentaryTargets[_momentaryCount] = target;
            ++_momentaryCount;
            layerChanged();
        }
    } else if (action.layerMode == LAYER_SWITCH) {
        setBaseLayer(target);
    } else if (action.layerMode == LAYER_MODAL) {
        if (_data.baseLayer == target) {
            uint8_t returnLayer = _modalReturnLayer[index];
            if (returnLayer >= _data.layerCount || returnLayer == target) returnLayer = 0;
            setBaseLayer(returnLayer);
        } else {
            _modalReturnLayer[index] = _data.baseLayer;
            setBaseLayer(target);
        }
    }
}

void MultiKey::applyLayerRelease(uint8_t index, const KeyAction &action) {
    if (action.layerMode != LAYER_MOMENTARY) return;
    int8_t found = -1;
    for (uint8_t i = 0; i < _momentaryCount; ++i) {
        if (_momentaryKeys[i] == index) {
            found = i;
            break;
        }
    }
    if (found < 0) return;
    for (uint8_t i = found; i + 1 < _momentaryCount; ++i) {
        _momentaryKeys[i] = _momentaryKeys[i + 1];
        _momentaryTargets[i] = _momentaryTargets[i + 1];
    }
    --_momentaryCount;
    _momentaryKeys[_momentaryCount] = -1;
    layerChanged();
}

void MultiKey::emitKeyEvent(KeyEventType type, uint8_t index, uint32_t nowMs) {
    KeyEvent event = {
        type,
        index,
        static_cast<uint8_t>(index / Config::COLS),
        static_cast<uint8_t>(index % Config::COLS),
        activeLayer(),
        _pressedAt[index],
        type == KEY_EVENT_PRESS ? static_cast<uint32_t>(0) : static_cast<uint32_t>(nowMs - _pressedAt[index]),
        _latched[index]
    };
    _cli.emitKeyEvent(event);
    if (_keyCallback) _keyCallback(event, _keyContext);
}

void MultiKey::layerChanged() {
    _led.applyLayerSettings(_data.layerLed[activeLayer()]);
    _cli.emitLayerEvent(activeLayer(), _data.baseLayer);
    if (_layerCallback) _layerCallback(activeLayer(), _data.baseLayer, _layerContext);
    _hid.updateDumpCharacteristic();
}

void MultiKey::keymapChanged() {
    _hid.requestReport();
    _hid.updateDumpCharacteristic();
}

void MultiKey::markAllDirty() {
    _dirtyLayers = static_cast<uint8_t>((1U << _data.layerCount) - 1U);
    _metadataDirty = true;
}

void MultiKey::clearMomentaryLayers() {
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) _momentaryKeys[i] = -1;
    _momentaryCount = 0;
}
