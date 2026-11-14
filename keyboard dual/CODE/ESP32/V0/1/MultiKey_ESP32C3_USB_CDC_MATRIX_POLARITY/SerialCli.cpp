#include "SerialCli.h"
#include "Keyboard.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace {

bool number(const char *text, uint32_t &value) {
    if (text == nullptr || *text == '\0' || *text == '-') return false;
    char *end = nullptr;
    errno = 0;
    const unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool rangedNumber(const char *text, uint32_t minimum, uint32_t maximum, uint32_t &value) {
    return number(text, value) && value >= minimum && value <= maximum;
}

bool isOn(const char *text, bool &value) {
    if (text == nullptr) return false;
    if (strcasecmp(text, "ON") == 0 || strcmp(text, "1") == 0) {
        value = true;
        return true;
    }
    if (strcasecmp(text, "OFF") == 0 || strcmp(text, "0") == 0) {
        value = false;
        return true;
    }
    return false;
}

const char *actionTypeName(uint8_t type) {
    switch (type) {
        case ACTION_HID: return "HID";
        case ACTION_LAYER: return "LAYER";
        case ACTION_TRANSPARENT: return "TRANSPARENT";
        default: return "UNKNOWN";
    }
}

const char *modeName(uint8_t mode) {
    switch (mode) {
        case LAYER_MOMENTARY: return "MOMENTARY";
        case LAYER_SWITCH: return "SWITCH";
        case LAYER_MODAL: return "MODAL";
        default: return "UNKNOWN";
    }
}

void printAction(uint8_t layer, uint8_t index, const KeyAction &action, bool ok) {
    Serial.printf("%s KEY layer=%u index=%u row=%u col=%u type=%s usage=0x%02X mods=0x%02X target=%u mode=%s\n",
                  ok ? "OK" : "KEY", layer, index,
                  index / Config::COLS, index % Config::COLS,
                  actionTypeName(action.type), action.usage, action.modifiers,
                  action.layer, modeName(action.layerMode));
}

void printKeymap(MultiKey &keyboard, uint8_t layer) {
    Serial.printf("OK KEYMAP layer=%u entries=%u\n", layer, Config::NUM_KEYS);
    for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
        printAction(layer, index, keyboard.keyAction(layer, index), false);
    }
    Serial.printf("END KEYMAP layer=%u\n", layer);
}

int parseLayerSelection(const char *text, uint8_t layerCount) {
    if (text == nullptr || strcasecmp(text, "ALL") == 0 || strcmp(text, "*") == 0) return -1;
    uint32_t layer;
    if (!rangedNumber(text, 0, layerCount - 1, layer)) return -2;
    return static_cast<int>(layer);
}

bool parseMode(const char *text, uint8_t &mode) {
    uint32_t numeric;
    if (text == nullptr) return false;
    if (strcasecmp(text, "MOMENTARY") == 0 || strcasecmp(text, "WHILE_PRESSED") == 0) {
        mode = LAYER_MOMENTARY;
        return true;
    }
    if (strcasecmp(text, "SWITCH") == 0) {
        mode = LAYER_SWITCH;
        return true;
    }
    if (strcasecmp(text, "MODAL") == 0) {
        mode = LAYER_MODAL;
        return true;
    }
    if (rangedNumber(text, 0, LAYER_MODAL, numeric)) {
        mode = static_cast<uint8_t>(numeric);
        return true;
    }
    return false;
}

bool parseEffect(const char *text, LedEffect &effect) {
    if (text == nullptr) return false;
    if (strcasecmp(text, "OFF") == 0) effect = LED_OFF;
    else if (strcasecmp(text, "STATIC") == 0) effect = LED_STATIC;
    else if (strcasecmp(text, "RAINBOW") == 0) effect = LED_RAINBOW;
    else if (strcasecmp(text, "BREATHE") == 0) effect = LED_BREATHE;
    else if (strcasecmp(text, "CUSTOM") == 0) effect = LED_CUSTOM;
    else return false;
    return true;
}

bool parseKeypress(const char *text, KeypressAnimation &animation) {
    if (text == nullptr) return false;
    if (strcasecmp(text, "OFF") == 0 || strcasecmp(text, "NONE") == 0) animation = KEYPRESS_NONE;
    else if (strcasecmp(text, "PULSE") == 0) animation = KEYPRESS_PULSE;
    else if (strcasecmp(text, "SPARKLE") == 0) animation = KEYPRESS_SPARKLE;
    else if (strcasecmp(text, "RIPPLE") == 0) animation = KEYPRESS_RIPPLE;
    else return false;
    return true;
}

bool parseSleep(const char *text, SleepAnimation &animation) {
    if (text == nullptr) return false;
    if (strcasecmp(text, "OFF") == 0) animation = SLEEP_OFF;
    else if (strcasecmp(text, "BREATHE") == 0) animation = SLEEP_BREATHE;
    else if (strcasecmp(text, "RAINBOW") == 0) animation = SLEEP_RAINBOW;
    else return false;
    return true;
}

void printHelp() {
    Serial.println("OK HELP");
    Serial.println("PING | INFO | HELP");
    Serial.println("GET KEYMAP [layer|ALL] | GET KEY <layer> <index>");
    Serial.println("SET KEY <layer> <index> HID <usage> <mods>");
    Serial.println("SET KEY <layer> <index> LAYER <target> <MOMENTARY|SWITCH|MODAL>");
    Serial.println("SET KEY <layer> <index> TRANSPARENT");
    Serial.println("SAVE [layer|ALL] | RECALL [layer|ALL] | RESET [layer|ALL]");
    Serial.println("GET LAYERS | GET LAYER | SET LAYER <layer>");
    Serial.println("SET LAYER_COUNT <1..8> | SET LAYER_NAME <layer> <name>");
    Serial.println("GET HOLD | SET HOLD <ms> | GET DIRTY | GET STATE");
    Serial.println("LED STATUS|ENABLE|BRIGHTNESS|COLOR|EFFECT|KEYPRESS|SLEEP|SLEEP_TIMEOUT|BLE|SET|CLEAR|HELLO");
    Serial.println("BLE STATUS | EVENTS ON|OFF | GET EVENTS");
    Serial.println("END HELP");
}

} // namespace

void SerialCli::begin() {
    Serial.printf("OK READY version=%s protocol=%u\n",
                  Config::FIRMWARE_VERSION, Config::SERIAL_PROTOCOL_VERSION);
    Serial.println("------------------------------");
    Serial.println("Type HELP to view command list");
}

void SerialCli::update() {
    while (Serial.available()) {
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r' || ch == '\n') {
            if (_overflow) {
                Serial.println("ERR INPUT line_too_long");
            } else if (_length > 0) {
                _line[_length] = '\0';
                processLine(_line);
            }
            _length = 0;
            _overflow = false;
            _line[0] = '\0';
            continue;
        }
        if (_overflow) continue;
        if (_length + 1 >= LINE_CAPACITY) {
            _overflow = true;
            _length = 0;
            continue;
        }
        _line[_length++] = ch;
    }
}

void SerialCli::processLine(const char *line) {
    if (line == nullptr) return;
    char buffer[LINE_CAPACITY];
    strncpy(buffer, line, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *save = nullptr;
    char *command = strtok_r(buffer, " \t", &save);
    if (command == nullptr) return;

    auto next = [&save]() -> char * { return strtok_r(nullptr, " \t", &save); };

    if (strcasecmp(command, "PING") == 0) {
        Serial.println("OK PING");
        return;
    }
    if (strcasecmp(command, "HELP") == 0) {
        printHelp();
        return;
    }
    if (strcasecmp(command, "INFO") == 0) {
        Serial.printf("OK INFO version=%s protocol=%u rows=%u cols=%u keys=%u max_layers=%u layers=%u nkro=%u storage=%u\n",
                      Config::FIRMWARE_VERSION, Config::SERIAL_PROTOCOL_VERSION,
                      Config::ROWS, Config::COLS, Config::NUM_KEYS,
                      Config::MAX_LAYERS, _keyboard.layerCount(),
                      Config::USE_NKRO, _keyboard.storageAvailable() ? 1 : 0);
        return;
    }

    if (strcasecmp(command, "DUMP") == 0) {
        for (uint8_t layer = 0; layer < _keyboard.layerCount(); ++layer) printKeymap(_keyboard, layer);
        return;
    }

    if (strcasecmp(command, "GET") == 0) {
        char *what = next();
        if (what == nullptr) {
            Serial.println("ERR GET missing_argument");
            return;
        }
        if (strcasecmp(what, "KEYMAP") == 0 || strcasecmp(what, "KEYMAPS") == 0) {
            char *layerText = next();
            if (layerText == nullptr || strcasecmp(layerText, "ALL") == 0 || strcmp(layerText, "*") == 0) {
                for (uint8_t layer = 0; layer < _keyboard.layerCount(); ++layer) printKeymap(_keyboard, layer);
            } else {
                uint32_t layer;
                if (!rangedNumber(layerText, 0, _keyboard.layerCount() - 1, layer)) {
                    Serial.println("ERR GET KEYMAP invalid_layer");
                    return;
                }
                printKeymap(_keyboard, static_cast<uint8_t>(layer));
            }
            return;
        }
        if (strcasecmp(what, "KEY") == 0) {
            uint32_t layer, index;
            if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) ||
                !rangedNumber(next(), 0, Config::NUM_KEYS - 1, index)) {
                Serial.println("ERR GET KEY invalid_coordinates");
                return;
            }
            printAction(layer, index, _keyboard.keyAction(layer, index), true);
            return;
        }
        if (strcasecmp(what, "LAYERS") == 0) {
            Serial.printf("OK LAYERS count=%u active=%u base=%u\n",
                          _keyboard.layerCount(), _keyboard.activeLayer(), _keyboard.baseLayer());
            for (uint8_t layer = 0; layer < _keyboard.layerCount(); ++layer) {
                Serial.printf("LAYER layer=%u name=%s\n", layer, _keyboard.layerName(layer));
            }
            Serial.println("END LAYERS");
            return;
        }
        if (strcasecmp(what, "LAYER") == 0) {
            Serial.printf("OK LAYER active=%u base=%u\n", _keyboard.activeLayer(), _keyboard.baseLayer());
            return;
        }
        if (strcasecmp(what, "HOLD") == 0) {
            Serial.printf("OK HOLD ms=%u\n", _keyboard.holdDurationThreshold());
            return;
        }
        if (strcasecmp(what, "DIRTY") == 0) {
            Serial.printf("OK DIRTY mask=0x%02X metadata=%u\n",
                          _keyboard.dirtyLayers(), _keyboard.metadataDirty() ? 1 : 0);
            return;
        }
        if (strcasecmp(what, "EVENTS") == 0) {
            Serial.printf("OK EVENTS enabled=%u\n", _events ? 1 : 0);
            return;
        }
        if (strcasecmp(what, "STATE") == 0) {
            const uint64_t mask = _keyboard.pressedMask();
            Serial.printf("OK STATE pressed=%08lX%08lX active=%u base=%u ble=%s\n",
                          static_cast<unsigned long>(mask >> 32),
                          static_cast<unsigned long>(mask & 0xFFFFFFFFULL),
                          _keyboard.activeLayer(), _keyboard.baseLayer(),
                          _keyboard.hid().isConnected() ? "CONNECTED" : "ADVERTISING");
            return;
        }
        Serial.println("ERR GET unknown_argument");
        return;
    }

    if (strcasecmp(command, "SET") == 0) {
        char *what = next();
        if (what == nullptr) {
            Serial.println("ERR SET missing_argument");
            return;
        }
        if (strcasecmp(what, "KEY") == 0) {
            uint32_t layer, index;
            if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) ||
                !rangedNumber(next(), 0, Config::NUM_KEYS - 1, index)) {
                Serial.println("ERR SET KEY invalid_coordinates");
                return;
            }
            char *type = next();
            if (type == nullptr) {
                Serial.println("ERR SET KEY missing_type");
                return;
            }
            KeyAction action;
            if (strcasecmp(type, "HID") == 0 || strcasecmp(type, "NONE") == 0) {
                uint32_t usage = 0, mods = 0;
                if (strcasecmp(type, "HID") == 0 &&
                    (!rangedNumber(next(), 0, 0xE7, usage) || !rangedNumber(next(), 0, 0xFF, mods))) {
                    Serial.println("ERR SET KEY invalid_hid_value");
                    return;
                }
                action = hidAction(usage, mods);
            } else if (strcasecmp(type, "LAYER") == 0) {
                uint32_t target;
                uint8_t mode;
                if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, target) || !parseMode(next(), mode)) {
                    Serial.println("ERR SET KEY invalid_layer_action");
                    return;
                }
                action = layerAction(target, static_cast<LayerSwitchMode>(mode));
            } else if (strcasecmp(type, "TRANSPARENT") == 0) {
                action = transparentAction();
            } else {
                Serial.println("ERR SET KEY unknown_type");
                return;
            }
            if (!_keyboard.setKeyAction(layer, index, action)) {
                Serial.println("ERR SET KEY rejected");
                return;
            }
            Serial.printf("OK SET KEY layer=%lu index=%lu\n",
                          static_cast<unsigned long>(layer), static_cast<unsigned long>(index));
            return;
        }
        if (strcasecmp(what, "LAYER") == 0) {
            uint32_t layer;
            if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) || !_keyboard.setBaseLayer(layer)) {
                Serial.println("ERR SET LAYER invalid_layer");
                return;
            }
            Serial.printf("OK SET LAYER base=%lu active=%u\n",
                          static_cast<unsigned long>(layer), _keyboard.activeLayer());
            return;
        }
        if (strcasecmp(what, "LAYER_COUNT") == 0) {
            uint32_t count;
            if (!rangedNumber(next(), 1, Config::MAX_LAYERS, count) || !_keyboard.setLayerCount(count)) {
                Serial.println("ERR SET LAYER_COUNT invalid_count");
                return;
            }
            Serial.printf("OK SET LAYER_COUNT count=%lu\n", static_cast<unsigned long>(count));
            return;
        }
        if (strcasecmp(what, "LAYER_NAME") == 0) {
            uint32_t layer;
            if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer)) {
                Serial.println("ERR SET LAYER_NAME invalid_layer");
                return;
            }
            char *name = save;
            while (name != nullptr && (*name == ' ' || *name == '\t')) ++name;
            if (name == nullptr || *name == '\0' || !_keyboard.setLayerName(layer, name)) {
                Serial.println("ERR SET LAYER_NAME invalid_name");
                return;
            }
            Serial.printf("OK SET LAYER_NAME layer=%lu name=%s\n",
                          static_cast<unsigned long>(layer), _keyboard.layerName(layer));
            return;
        }
        if (strcasecmp(what, "HOLD") == 0) {
            uint32_t duration;
            if (!rangedNumber(next(), 1, 65535, duration) ||
                !_keyboard.setHoldDurationThreshold(duration)) {
                Serial.println("ERR SET HOLD invalid_duration");
                return;
            }
            Serial.printf("OK SET HOLD ms=%lu\n", static_cast<unsigned long>(duration));
            return;
        }

        // Original single-layer syntax: SET <row> <col> <usage> <mods>.
        uint32_t row, col, usage, mods;
        if (rangedNumber(what, 0, Config::ROWS - 1, row) &&
            rangedNumber(next(), 0, Config::COLS - 1, col) &&
            rangedNumber(next(), 0, 0xE7, usage) && rangedNumber(next(), 0, 0xFF, mods) &&
            _keyboard.setKeyAction(0, row * Config::COLS + col, hidAction(usage, mods))) {
            Serial.printf("OK SET KEY layer=0 index=%lu\n",
                          static_cast<unsigned long>(row * Config::COLS + col));
            return;
        }
        Serial.println("ERR SET invalid_syntax");
        return;
    }

    if (strcasecmp(command, "SAVE") == 0 || strcasecmp(command, "RECALL") == 0 ||
        strcasecmp(command, "LOAD") == 0 || strcasecmp(command, "RESET") == 0) {
        const int layer = parseLayerSelection(next(), _keyboard.layerCount());
        if (layer == -2) {
            Serial.printf("ERR %s invalid_layer\n", command);
            return;
        }
        bool ok = true;
        if (strcasecmp(command, "SAVE") == 0) ok = _keyboard.saveKeymaps(layer);
        else if (strcasecmp(command, "RESET") == 0) _keyboard.resetKeymaps(layer);
        else ok = _keyboard.recallKeymaps(layer);
        if (!ok) {
            Serial.printf("ERR %s operation_failed\n", command);
            return;
        }
        if (layer < 0) Serial.printf("OK %s layer=ALL\n", command);
        else Serial.printf("OK %s layer=%d\n", command, layer);
        return;
    }

    if (strcasecmp(command, "LAYER") == 0) {
        uint32_t layer;
        if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) || !_keyboard.setBaseLayer(layer)) {
            Serial.println("ERR LAYER invalid_layer");
            return;
        }
        Serial.printf("OK LAYER active=%u base=%u\n", _keyboard.activeLayer(), _keyboard.baseLayer());
        return;
    }

    if (strcasecmp(command, "EVENTS") == 0) {
        bool enabled;
        if (!isOn(next(), enabled)) {
            Serial.println("ERR EVENTS expected_ON_or_OFF");
            return;
        }
        _events = enabled;
        Serial.printf("OK EVENTS enabled=%u\n", _events ? 1 : 0);
        return;
    }

    if (strcasecmp(command, "BLE") == 0) {
        char *what = next();
        if (what != nullptr && strcasecmp(what, "STATUS") == 0) {
            Serial.printf("OK BLE state=%s advertising=%u\n",
                          _keyboard.hid().isConnected() ? "CONNECTED" : "DISCONNECTED",
                          _keyboard.hid().isAdvertising() ? 1 : 0);
        } else {
            Serial.println("ERR BLE expected_STATUS");
        }
        return;
    }

    if (strcasecmp(command, "LED") == 0 || strcasecmp(command, "HELLO") == 0) {
        char *what = strcasecmp(command, "HELLO") == 0 ? const_cast<char *>("HELLO") : next();
        if (what == nullptr) {
            Serial.println("ERR LED missing_argument");
            return;
        }
        LedController &led = _keyboard.led();
        if (strcasecmp(what, "STATUS") == 0) {
            Serial.printf("OK LED enabled=%u brightness=%u effect=%u color=0x%02X%02X%02X keypress=%u sleep=%u sleep_timeout=%lu sleeping=%u ble_indicators=%u\n",
                          led.enabled(), led.brightness(), led.effect(), led.color().r, led.color().g,
                          led.color().b, led.keypressAnimation(), led.sleepAnimation(),
                          static_cast<unsigned long>(led.sleepTimeout()), led.sleeping(), led.bleIndicators());
            return;
        }
        if (strcasecmp(what, "ENABLE") == 0) {
            bool enabled;
            if (!isOn(next(), enabled)) { Serial.println("ERR LED ENABLE expected_ON_or_OFF"); return; }
            led.setEnabled(enabled);
        } else if (strcasecmp(what, "BRIGHTNESS") == 0) {
            uint32_t value;
            if (!rangedNumber(next(), 0, 255, value)) { Serial.println("ERR LED BRIGHTNESS invalid_value"); return; }
            led.setBrightness(value);
        } else if (strcasecmp(what, "COLOR") == 0) {
            uint32_t r, g, b;
            if (!rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, g) ||
                !rangedNumber(next(), 0, 255, b)) { Serial.println("ERR LED COLOR invalid_value"); return; }
            led.setColor(CRGB(r, g, b));
        } else if (strcasecmp(what, "EFFECT") == 0) {
            LedEffect effect;
            if (!parseEffect(next(), effect)) { Serial.println("ERR LED EFFECT invalid_effect"); return; }
            led.setEffect(effect);
        } else if (strcasecmp(what, "KEYPRESS") == 0) {
            KeypressAnimation animation;
            if (!parseKeypress(next(), animation)) { Serial.println("ERR LED KEYPRESS invalid_animation"); return; }
            led.setKeypressAnimation(animation);
        } else if (strcasecmp(what, "SLEEP") == 0) {
            SleepAnimation animation;
            if (!parseSleep(next(), animation)) { Serial.println("ERR LED SLEEP invalid_animation"); return; }
            led.setSleepAnimation(animation);
        } else if (strcasecmp(what, "SLEEP_TIMEOUT") == 0) {
            uint32_t timeout;
            if (!number(next(), timeout)) { Serial.println("ERR LED SLEEP_TIMEOUT invalid_value"); return; }
            led.setSleepTimeout(timeout);
        } else if (strcasecmp(what, "BLE") == 0) {
            bool enabled;
            if (!isOn(next(), enabled)) { Serial.println("ERR LED BLE expected_ON_or_OFF"); return; }
            led.setBleIndicators(enabled);
        } else if (strcasecmp(what, "SET") == 0) {
            uint32_t index, r, g, b;
            if (!rangedNumber(next(), 0, Config::NUM_KEYS - 1, index) ||
                !rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, g) ||
                !rangedNumber(next(), 0, 255, b)) { Serial.println("ERR LED SET invalid_value"); return; }
            led.setKeyColor(index, CRGB(r, g, b));
        } else if (strcasecmp(what, "CLEAR") == 0) {
            led.clearCustom();
            led.setEffect(LED_CUSTOM);
        } else if (strcasecmp(what, "HELLO") == 0) {
            led.playHello();
        } else {
            Serial.println("ERR LED unknown_argument");
            return;
        }
        Serial.printf("OK LED %s\n", what);
        return;
    }

    Serial.printf("ERR %s unknown_command\n", command);
}

void SerialCli::emitKeyEvent(const KeyEvent &event) {
    if (!_events) return;
    const char *type = event.type == KEY_EVENT_PRESS ? "PRESS" :
                       event.type == KEY_EVENT_RELEASE ? "RELEASE" : "HOLD";
    Serial.printf("EVENT KEY type=%s index=%u row=%u col=%u layer=%u held_ms=%lu\n",
                  type, event.index, event.row, event.col, event.layer,
                  static_cast<unsigned long>(event.heldMs));
}

void SerialCli::emitLayerEvent(uint8_t active, uint8_t base) {
    if (!_events) return;
    Serial.printf("EVENT LAYER active=%u base=%u\n", active, base);
}

void SerialCli::emitBleEvent(bool connected) {
    if (!_events) return;
    Serial.printf("EVENT BLE state=%s\n", connected ? "CONNECTED" : "DISCONNECTED");
}
