#include "Keyboard.h"
#include "Keymaps.h"
#include <string.h>

MultiKey::MultiKey() : _cli(*this) {
    clearMomentaryLayers();
}

void MultiKey::begin() {
    Serial0.end();
    Serial.begin(115200);
    delay(100);

    _storeReady = _store.begin();
    bool loaded = _storeReady && _store.load(_data);
    if (!loaded) {
        memset(&_data, 0, sizeof(_data));
        _data.layerCount = Config::DEFAULT_LAYER_COUNT;
        _data.baseLayer = 0;
        _data.holdMs = HOLD_DUR_MS;
        memcpy(_data.layerNames, DEFAULT_LAYER_NAMES, sizeof(_data.layerNames));
        loadDefaultKeymaps(_data.layers);
        if (_storeReady) _store.save(_data);
    }

    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        _latched[index] = hidAction();
    }
    _scanner.begin();
    _led.begin();
    if (_storeReady) {
        LedSettings ledSettings;
        if (_store.loadLed(ledSettings)) _led.applySettings(ledSettings);
    }
    _led.playHello();
    _hid.begin(*this);
    _lastBleConnected = _hid.isConnected();
    _led.setBleConnected(_lastBleConnected);
    _cli.begin();
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

    const bool connected = _hid.isConnected();
    if (connected != _lastBleConnected) {
        _lastBleConnected = connected;
        _led.setBleConnected(connected);
        _cli.emitBleEvent(connected);
        if (_bleCallback) _bleCallback(connected, _bleContext);
    }

    _hid.update(_physicalPressed, _latched);
    _led.update(nowMs);
    _cli.update();

    // Persist LED settings after the user stops changing them.
    if (_storeReady && _led.settingsDirty() &&
        static_cast<uint32_t>(nowMs - _led.settingsChangedAtMs()) >= Config::LED_SETTINGS_SAVE_DELAY_MS &&
        static_cast<uint32_t>(nowMs - _lastLedSaveAttemptMs) >= Config::LED_SETTINGS_SAVE_RETRY_MS) {
        _lastLedSaveAttemptMs = nowMs;
        if (_store.saveLed(_led.captureSettings())) _led.markSettingsSaved();
    }
    delay(1);
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
        _data.baseLayer = layer;
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
    if (_latched[index].type == ACTION_LAYER) applyLayerPress(index, _latched[index]);
    emitKeyEvent(KEY_EVENT_PRESS, index, nowMs);
    _hid.requestReport();
}

void MultiKey::handleRelease(uint8_t index, uint32_t nowMs) {
    if (_latched[index].type == ACTION_LAYER) applyLayerRelease(index, _latched[index]);
    emitKeyEvent(KEY_EVENT_RELEASE, index, nowMs);
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
