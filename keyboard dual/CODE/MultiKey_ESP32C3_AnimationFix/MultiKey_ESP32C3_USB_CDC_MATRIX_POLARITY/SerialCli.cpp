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
  Serial.println("LED SPEED <EFFECT|KEYPRESS|SLEEP> <1..255> | LED MIX <0..4>");
  Serial.println("LED BACKLIGHT <0..255> [r g b] | LED LAYERFLASH <ON|OFF> [r g b] [r g b] [fadeMs]");
  Serial.println("LED LAYER <layer> [BRIGHT|EFFECT|COLOR] ... | GET LEDLAYER <layer>");
  Serial.println("GET GLOBAL | SET GLOBAL <field> <value>");
  Serial.println("GET PALETTE <0..15> | SET PALETTE <idx> <stop> <r> <g> <b>");
  Serial.println("PRESET SAVE <1..6> | PRESET LOAD <n> | PRESET LIST");
  Serial.println("MULTIKEY STATUS|PUSH | MULTIKEY SLOT SAVE <n>|LOAD <n>");
  Serial.println("MENU OPEN | MENU CLOSE | GAME LIST | GAME START <n> | GAME STOP");
  Serial.println("BLE STATUS | EVENTS ON|OFF | GET EVENTS");
  Serial.println("END HELP");
}

}  // namespace

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

  auto next = [&save]() -> char * {
    return strtok_r(nullptr, " \t", &save);
  };

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
      if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) || !rangedNumber(next(), 0, Config::NUM_KEYS - 1, index)) {
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
    if (strcasecmp(what, "LEDLAYER") == 0) {
      uint32_t layer;
      if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer)) {
        Serial.println("ERR GET LEDLAYER invalid_layer");
        return;
      }
      const LayerLedSettings s = _keyboard.layerLed(static_cast<uint8_t>(layer));
      Serial.printf("OK LEDLAYER layer=%lu brightness=%u effect=%u color=%02X%02X%02X\n",
                    static_cast<unsigned long>(layer), s.brightness, s.effect,
                    s.colorR, s.colorG, s.colorB);
      return;
    }
    if (strcasecmp(what, "GLOBAL") == 0) {
      const GlobalSettings &g = _keyboard.globals();
      Serial.printf("OK GLOBAL rollover=%u usb=%u bt=%u bt_power=%u mk=%u mk_id=%u mk_layout=%u save_slave=%u show_all=%u lang=%u oled_sleep=%lu nav=%u,%u,%u,%u,%u boot=%u boot_ms=%u nav_bright=%u\n",
                    g.rollover, g.usbMode, g.btMode, g.btTxPower, g.multikeyEnabled,
                    g.multikeyId, g.multikeyLayoutMode, g.saveSlaveChanges,
                    g.showAllKeyboards, g.hidLang,
                    static_cast<unsigned long>(g.oledSleepTimeoutMs),
                    g.oledNavKeys[0], g.oledNavKeys[1], g.oledNavKeys[2],
                    g.oledNavKeys[3], g.oledNavKeys[4],
                    g.bootMenuEnabled, g.bootMenuTimeMs, g.menuNavBrightness);
      return;
    }
    if (strcasecmp(what, "PALETTE") == 0) {
      uint32_t index;
      if (!rangedNumber(next(), 0, NUM_PALETTES - 1, index)) {
        Serial.println("ERR GET PALETTE invalid_index");
        return;
      }
      const Palette &p = _keyboard.palettes()[index];
      Serial.printf("OK PALETTE index=%lu stops=%u\n", static_cast<unsigned long>(index), p.count);
      for (uint8_t i = 0; i < p.count; ++i) {
        Serial.printf("STOP %u %u %u %u\n", i, p.color[i][0], p.color[i][1], p.color[i][2]);
      }
      Serial.println("END PALETTE");
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
      if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer) || !rangedNumber(next(), 0, Config::NUM_KEYS - 1, index)) {
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
        if (strcasecmp(type, "HID") == 0 && (!rangedNumber(next(), 0, 0xE7, usage) || !rangedNumber(next(), 0, 0xFF, mods))) {
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
      if (!rangedNumber(next(), 1, 65535, duration) || !_keyboard.setHoldDurationThreshold(duration)) {
        Serial.println("ERR SET HOLD invalid_duration");
        return;
      }
      Serial.printf("OK SET HOLD ms=%lu\n", static_cast<unsigned long>(duration));
      return;
    }
    if (strcasecmp(what, "GLOBAL") == 0) {
      char *field = next();
      char *value = next();
      if (field == nullptr || value == nullptr) {
        Serial.println("ERR SET GLOBAL missing_argument");
        return;
      }
      GlobalSettings &g = _keyboard.globals();
      uint32_t num;
      bool ok = true;
      if (strcasecmp(field, "ROLLOVER") == 0) {
        if (strcasecmp(value, "NKRO") == 0) g.rollover = ROLLOVER_NKRO;
        else if (strcasecmp(value, "6KRO") == 0) g.rollover = ROLLOVER_6KRO;
        else if (rangedNumber(value, 0, 1, num)) g.rollover = static_cast<uint8_t>(num);
        else ok = false;
      } else if (strcasecmp(field, "USB") == 0) {
        if (strcasecmp(value, "HID") == 0) g.usbMode = USB_MODE_HID;
        else if (strcasecmp(value, "CFG") == 0) g.usbMode = USB_MODE_CFG;
        else if (rangedNumber(value, 0, 1, num)) g.usbMode = static_cast<uint8_t>(num);
        else ok = false;
      } else if (strcasecmp(field, "BT") == 0) {
        if (strcasecmp(value, "HID") == 0) g.btMode = BT_MODE_HID;
        else if (strcasecmp(value, "COM") == 0) g.btMode = BT_MODE_COM;
        else if (rangedNumber(value, 0, 1, num)) g.btMode = static_cast<uint8_t>(num);
        else ok = false;
      } else if (strcasecmp(field, "BTPOWER") == 0) {
        ok = rangedNumber(value, 0, 7, num);
        if (ok) g.btTxPower = static_cast<uint8_t>(num);
      } else if (strcasecmp(field, "MK") == 0) {
        bool on;
        ok = isOn(value, on);
        if (ok) g.multikeyEnabled = on ? 1 : 0;
      } else if (strcasecmp(field, "MKID") == 0) {
        ok = rangedNumber(value, 0, 5, num);
        if (ok) g.multikeyId = static_cast<uint8_t>(num);
      } else if (strcasecmp(field, "MKLAYOUT") == 0) {
        ok = rangedNumber(value, 0, 255, num);
        if (ok) g.multikeyLayoutMode = static_cast<uint8_t>(num);
      } else if (strcasecmp(field, "SAVECHANGES") == 0) {
        bool on;
        ok = isOn(value, on);
        if (ok) g.saveSlaveChanges = on ? 1 : 0;
      } else if (strcasecmp(field, "SHOWALL") == 0) {
        bool on;
        ok = isOn(value, on);
        if (ok) g.showAllKeyboards = on ? 1 : 0;
      } else if (strcasecmp(field, "LANG") == 0) {
        if (strcasecmp(value, "RU") == 0) g.hidLang = HID_LABEL_RU;
        else if (strcasecmp(value, "EN") == 0) g.hidLang = HID_LABEL_EN;
        else if (rangedNumber(value, 0, 1, num)) g.hidLang = static_cast<uint8_t>(num);
        else ok = false;
      } else if (strcasecmp(field, "OLEDSLEEP") == 0) {
        ok = number(value, num);
        if (ok) g.oledSleepTimeoutMs = num * 1000UL;
      } else if (strcasecmp(field, "BOOTMENU") == 0) {
        bool on;
        ok = isOn(value, on);
        if (ok) g.bootMenuEnabled = on ? 1 : 0;
      } else if (strcasecmp(field, "BOOTTIME") == 0) {
        ok = rangedNumber(value, 1000, 65535, num);
        if (ok) g.bootMenuTimeMs = static_cast<uint16_t>(num);
      } else if (strcasecmp(field, "NAVBRIGHT") == 0) {
        ok = rangedNumber(value, 0, 255, num);
        if (ok) g.menuNavBrightness = static_cast<uint16_t>(num);
      } else if (strcasecmp(field, "NAVKEYS") == 0) {
        uint32_t first;
        if (!rangedNumber(value, 0, Config::NUM_KEYS - 1, first)) { ok = false; }
        else {
          g.oledNavKeys[0] = static_cast<uint8_t>(first);
          for (uint8_t i = 1; i < 5; ++i) {
            if (!rangedNumber(next(), 0, Config::NUM_KEYS - 1, num)) { ok = false; break; }
            g.oledNavKeys[i] = static_cast<uint8_t>(num);
          }
        }
      } else {
        ok = false;
      }
      if (!ok) {
        Serial.println("ERR SET GLOBAL invalid_field");
        return;
      }
      _keyboard.noteGlobalsChanged();
      _keyboard.applyGlobalSettings();
      Serial.println("OK SET GLOBAL");
      return;
    }
    if (strcasecmp(what, "PALETTE") == 0) {
      uint32_t index, stop, r, gch, b;
      if (!rangedNumber(next(), 0, NUM_PALETTES - 1, index) ||
          !rangedNumber(next(), 0, PALETTE_MAX_STOPS - 1, stop) ||
          !rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, gch) ||
          !rangedNumber(next(), 0, 255, b)) {
        Serial.println("ERR SET PALETTE invalid_value");
        return;
      }
      Palette *palettes = _keyboard.palettes();
      Palette &p = palettes[index];
      if (p.count == 0) {
        p.count = 2;
      }
      if (stop >= p.count) {
        Serial.println("ERR SET PALETTE invalid_stop");
        return;
      }
      p.color[stop][0] = static_cast<uint8_t>(r);
      p.color[stop][1] = static_cast<uint8_t>(gch);
      p.color[stop][2] = static_cast<uint8_t>(b);
      _keyboard.savePalettes();
      Serial.println("OK SET PALETTE");
      return;
    }

    // Original single-layer syntax: SET <row> <col> <usage> <mods>.
    uint32_t row, col, usage, mods;
    if (rangedNumber(what, 0, Config::ROWS - 1, row) && rangedNumber(next(), 0, Config::COLS - 1, col) && rangedNumber(next(), 0, 0xE7, usage) && rangedNumber(next(), 0, 0xFF, mods) && _keyboard.setKeyAction(0, row * Config::COLS + col, hidAction(usage, mods))) {
      Serial.printf("OK SET KEY layer=0 index=%lu\n",
                    static_cast<unsigned long>(row * Config::COLS + col));
      return;
    }
    Serial.println("ERR SET invalid_syntax");
    return;
  }

  if (strcasecmp(command, "SAVE") == 0 || strcasecmp(command, "RECALL") == 0 || strcasecmp(command, "LOAD") == 0 || strcasecmp(command, "RESET") == 0) {
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
      const LedGlobalSettings s = led.captureSettings();
      Serial.printf("OK LED enabled=%u brightness=%u effect=%u fx_speed=%u color=0x%02X%02X%02X keypress=%u kp_speed=%u sleep=%u sleep_speed=%u sleep_timeout=%lu sleeping=%u ble_indicators=%u mix=%u backlight=%u#%02X%02X%02X flash=%u next=%02X%02X%02X prev=%02X%02X%02X fade=%u\n",
                    s.enabled, s.brightness, s.effect, s.effectSpeed,
                    s.colorR, s.colorG, s.colorB, s.keypressAnimation, s.keypressSpeed,
                    s.sleepAnimation, s.sleepSpeed,
                    static_cast<unsigned long>(s.sleepTimeoutMs), led.sleeping(),
                    s.bleIndicators, s.mixMode, s.backlightBrightness,
                    s.backlightR, s.backlightG, s.backlightB, s.layerFlashEnabled,
                    s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB,
                    s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB,
                    s.layerFlashFadeMs);
      return;
    }
    if (strcasecmp(what, "SPEED") == 0) {
      char *which = next();
      uint32_t speed;
      if (which == nullptr || !rangedNumber(next(), 1, 255, speed)) {
        Serial.println("ERR LED SPEED invalid_value");
        return;
      }
      if (strcasecmp(which, "EFFECT") == 0) led.setEffectSpeed(speed);
      else if (strcasecmp(which, "KEYPRESS") == 0) led.setKeypressSpeed(speed);
      else if (strcasecmp(which, "SLEEP") == 0) led.setSleepSpeed(speed);
      else {
        Serial.println("ERR LED SPEED invalid_target");
        return;
      }
      Serial.printf("OK LED SPEED %s %lu\n", which, static_cast<unsigned long>(speed));
      return;
    }
    if (strcasecmp(what, "MIX") == 0) {
      uint32_t mode;
      if (!rangedNumber(next(), 0, 4, mode)) {
        Serial.println("ERR LED MIX invalid_value");
        return;
      }
      led.setMixMode(static_cast<LedMixMode>(mode));
      Serial.printf("OK LED MIX %lu\n", static_cast<unsigned long>(mode));
      return;
    }
    if (strcasecmp(what, "BACKLIGHT") == 0) {
      uint32_t brightness, r = 0, g = 0, b = 0;
      if (!rangedNumber(next(), 0, 255, brightness)) {
        Serial.println("ERR LED BACKLIGHT invalid_value");
        return;
      }
      char *rText = next();
      if (rText != nullptr) {
        if (!rangedNumber(rText, 0, 255, r) || !rangedNumber(next(), 0, 255, g) || !rangedNumber(next(), 0, 255, b)) {
          Serial.println("ERR LED BACKLIGHT invalid_color");
          return;
        }
      } else {
        const LedGlobalSettings s = led.captureSettings();
        r = s.backlightR; g = s.backlightG; b = s.backlightB;
      }
      led.setBacklight(static_cast<uint8_t>(brightness), CRGB(r, g, b));
      Serial.println("OK LED BACKLIGHT");
      return;
    }
    if (strcasecmp(what, "LAYERFLASH") == 0) {
      bool enabled;
      if (!isOn(next(), enabled)) {
        Serial.println("ERR LED LAYERFLASH expected_ON_or_OFF");
        return;
      }
      const LedGlobalSettings s = led.captureSettings();
      CRGB nextC(s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB);
      CRGB prevC(s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB);
      uint16_t fade = s.layerFlashFadeMs;
      uint32_t r, g, b;
      char *rText = next();
      if (rText != nullptr && rangedNumber(rText, 0, 255, r) && rangedNumber(next(), 0, 255, g) && rangedNumber(next(), 0, 255, b)) {
        nextC = CRGB(r, g, b);
        char *r2 = next();
        if (r2 != nullptr && rangedNumber(r2, 0, 255, r) && rangedNumber(next(), 0, 255, g) && rangedNumber(next(), 0, 255, b)) {
          prevC = CRGB(r, g, b);
          uint32_t fadeIn;
          if (number(next(), fadeIn)) fade = static_cast<uint16_t>(fadeIn);
        }
      }
      led.setLayerFlash(enabled, nextC, prevC, fade);
      Serial.println("OK LED LAYERFLASH");
      return;
    }
    if (strcasecmp(what, "LAYER") == 0) {
      uint32_t layer;
      if (!rangedNumber(next(), 0, _keyboard.layerCount() - 1, layer)) {
        Serial.println("ERR LED LAYER invalid_layer");
        return;
      }
      char *field = next();
      if (field == nullptr) {
        Serial.println("ERR LED LAYER missing_argument");
        return;
      }
      LayerLedSettings s = _keyboard.layerLed(static_cast<uint8_t>(layer));
      if (strcasecmp(field, "BRIGHT") == 0) {
        uint32_t value;
        if (!rangedNumber(next(), 0, 255, value)) {
          Serial.println("ERR LED LAYER BRIGHT invalid_value");
          return;
        }
        s.brightness = static_cast<uint8_t>(value);
      } else if (strcasecmp(field, "EFFECT") == 0) {
        uint32_t value;
        if (!rangedNumber(next(), 0, 255, value) || (value > LED_CUSTOM && value != LAYER_LED_FOLLOW_GLOBAL)) {
          Serial.println("ERR LED LAYER EFFECT invalid_value");
          return;
        }
        s.effect = static_cast<uint8_t>(value);
      } else if (strcasecmp(field, "COLOR") == 0) {
        uint32_t r, g, b;
        if (!rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, g) || !rangedNumber(next(), 0, 255, b)) {
          Serial.println("ERR LED LAYER COLOR invalid_value");
          return;
        }
        s.colorR = static_cast<uint8_t>(r);
        s.colorG = static_cast<uint8_t>(g);
        s.colorB = static_cast<uint8_t>(b);
      } else {
        Serial.println("ERR LED LAYER unknown_field");
        return;
      }
      _keyboard.setLayerLed(static_cast<uint8_t>(layer), s);
      Serial.println("OK LED LAYER");
      return;
    }
    if (strcasecmp(what, "ENABLE") == 0) {
      bool enabled;
      if (!isOn(next(), enabled)) {
        Serial.println("ERR LED ENABLE expected_ON_or_OFF");
        return;
      }
      led.setEnabled(enabled);
    } else if (strcasecmp(what, "BRIGHTNESS") == 0) {
      uint32_t value;
      if (!rangedNumber(next(), 0, 255, value)) {
        Serial.println("ERR LED BRIGHTNESS invalid_value");
        return;
      }
      led.setBrightness(value);
    } else if (strcasecmp(what, "COLOR") == 0) {
      uint32_t r, g, b;
      if (!rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, g) || !rangedNumber(next(), 0, 255, b)) {
        Serial.println("ERR LED COLOR invalid_value");
        return;
      }
      led.setColor(CRGB(r, g, b));
    } else if (strcasecmp(what, "EFFECT") == 0) {
      LedEffect effect;
      if (!parseEffect(next(), effect)) {
        Serial.println("ERR LED EFFECT invalid_effect");
        return;
      }
      led.setEffect(effect);
    } else if (strcasecmp(what, "KEYPRESS") == 0) {
      KeypressAnimation animation;
      if (!parseKeypress(next(), animation)) {
        Serial.println("ERR LED KEYPRESS invalid_animation");
        return;
      }
      led.setKeypressAnimation(animation);
    } else if (strcasecmp(what, "SLEEP") == 0) {
      SleepAnimation animation;
      if (!parseSleep(next(), animation)) {
        Serial.println("ERR LED SLEEP invalid_animation");
        return;
      }
      led.setSleepAnimation(animation);
    } else if (strcasecmp(what, "SLEEP_TIMEOUT") == 0) {
      uint32_t timeout;
      if (!number(next(), timeout)) {
        Serial.println("ERR LED SLEEP_TIMEOUT invalid_value");
        return;
      }
      led.setSleepTimeout(timeout);
    } else if (strcasecmp(what, "BLE") == 0) {
      bool enabled;
      if (!isOn(next(), enabled)) {
        Serial.println("ERR LED BLE expected_ON_or_OFF");
        return;
      }
      led.setBleIndicators(enabled);
    } else if (strcasecmp(what, "SAVE") == 0) {
      if (!_keyboard.saveLedSettings()) {
        Serial.println("ERR LED SAVE failed");
        return;
      }
    } else if (strcasecmp(what, "SET") == 0) {
      uint32_t index, r, g, b;
      if (!rangedNumber(next(), 0, Config::NUM_KEYS - 1, index) || !rangedNumber(next(), 0, 255, r) || !rangedNumber(next(), 0, 255, g) || !rangedNumber(next(), 0, 255, b)) {
        Serial.println("ERR LED SET invalid_value");
        return;
      }
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

  if (strcasecmp(command, "PRESET") == 0) {
    char *action = next();
    uint32_t slot;
    if (action == nullptr || !rangedNumber(next(), 1, MULTIKEY_SLOT_COUNT, slot)) {
      Serial.println("ERR PRESET invalid_arguments");
      return;
    }
    if (strcasecmp(action, "SAVE") == 0) {
      if (!_keyboard.savePresetSlot(static_cast<uint8_t>(slot - 1))) {
        Serial.println("ERR PRESET SAVE failed");
        return;
      }
    } else if (strcasecmp(action, "LOAD") == 0) {
      if (!_keyboard.loadPresetSlot(static_cast<uint8_t>(slot - 1))) {
        Serial.println("ERR PRESET LOAD failed");
        return;
      }
    } else if (strcasecmp(action, "LIST") == 0) {
      Serial.println("OK PRESET");
      for (uint8_t i = 0; i < MULTIKEY_SLOT_COUNT; ++i) {
        Serial.printf("SLOT %u %s\n", i + 1, _keyboard.presetExists(i) ? "SET" : "EMPTY");
      }
      Serial.println("END PRESET");
      return;
    } else {
      Serial.println("ERR PRESET unknown_action");
      return;
    }
    Serial.printf("OK PRESET %s slot=%lu\n", action, static_cast<unsigned long>(slot));
    return;
  }

  if (strcasecmp(command, "MULTIKEY") == 0) {
    char *action = next();
    if (action == nullptr) {
      Serial.println("ERR MULTIKEY missing_argument");
      return;
    }
    if (strcasecmp(action, "STATUS") == 0) {
      const GlobalSettings &g = _keyboard.globals();
      Serial.printf("OK MULTIKEY enabled=%u id=%u role=%s slaves=%u save_slave=%u\n",
                    g.multikeyEnabled, g.multikeyId,
                    _keyboard.link().isMaster() ? "MASTER" : "SLAVE",
                    _keyboard.link().connectedSlaves(), g.saveSlaveChanges);
      return;
    }
    if (strcasecmp(action, "PUSH") == 0) {
      _keyboard.link().pushConfig();
      Serial.println("OK MULTIKEY PUSH");
      return;
    }
    if (strcasecmp(action, "SLOT") == 0) {
      char *op = next();
      uint32_t slot;
      if (op == nullptr || !rangedNumber(next(), 1, MULTIKEY_SLOT_COUNT, slot)) {
        Serial.println("ERR MULTIKEY SLOT invalid_arguments");
        return;
      }
      if (strcasecmp(op, "SAVE") == 0) {
        if (!_keyboard.saveMultikeyConfigSlot(static_cast<uint8_t>(slot - 1), "MK CFG")) {
          Serial.println("ERR MULTIKEY SLOT SAVE failed");
          return;
        }
      } else if (strcasecmp(op, "LOAD") == 0) {
        if (!_keyboard.loadMultikeyConfigSlot(static_cast<uint8_t>(slot - 1))) {
          Serial.println("ERR MULTIKEY SLOT LOAD failed");
          return;
        }
      } else {
        Serial.println("ERR MULTIKEY SLOT unknown_action");
        return;
      }
      Serial.printf("OK MULTIKEY SLOT %s slot=%lu\n", op, static_cast<unsigned long>(slot));
      return;
    }
    Serial.println("ERR MULTIKEY unknown_action");
    return;
  }

  if (strcasecmp(command, "MENU") == 0) {
    char *action = next();
    if (action != nullptr && strcasecmp(action, "OPEN") == 0) {
      _keyboard.menu().open(true); // force: bypasses the post-EXIT reboot gate
      Serial.println("OK MENU OPEN");
    } else if (action != nullptr && strcasecmp(action, "CLOSE") == 0) {
      _keyboard.menu().close();
      Serial.println("OK MENU CLOSE");
    } else {
      Serial.println("ERR MENU expected_OPEN_or_CLOSE");
    }
    return;
  }

  if (strcasecmp(command, "GAME") == 0) {
    char *action = next();
    if (action == nullptr) {
      Serial.println("ERR GAME missing_argument");
      return;
    }
    if (strcasecmp(action, "LIST") == 0) {
      Serial.println("OK GAME");
      for (uint8_t i = 0; i < _keyboard.games().count(); ++i) {
        Serial.printf("GAME %u %s\n", i, _keyboard.games().name(i));
      }
      Serial.println("END GAME");
      return;
    }
    if (strcasecmp(action, "START") == 0) {
      uint32_t index;
      if (!rangedNumber(next(), 0, _keyboard.games().count() - 1, index)) {
        Serial.println("ERR GAME START invalid_index");
        return;
      }
      _keyboard.menu().close();
      _keyboard.menu().startGame(static_cast<uint8_t>(index));
      Serial.println("OK GAME START");
      return;
    }
    if (strcasecmp(action, "STOP") == 0) {
      _keyboard.games().stop();
      Serial.println("OK GAME STOP");
      return;
    }
    Serial.println("ERR GAME unknown_action");
    return;
  }

  Serial.printf("ERR %s unknown_command\n", command);
}

void SerialCli::emitKeyEvent(const KeyEvent &event) {
  if (!_events) return;
  const char *type = event.type == KEY_EVENT_PRESS ? "PRESS" : event.type == KEY_EVENT_RELEASE ? "RELEASE"
                                                                                               : "HOLD";
  char line[96];
  snprintf(line, sizeof(line), "EVENT KEY type=%s index=%u row=%u col=%u layer=%u held_ms=%lu\n",
           type, event.index, event.row, event.col, event.layer,
           static_cast<unsigned long>(event.heldMs));
  Serial.print(line);
  _keyboard.link().comWrite(reinterpret_cast<const uint8_t *>(line), strlen(line));
}

void SerialCli::emitLayerEvent(uint8_t active, uint8_t base) {
  if (!_events) return;
  char line[48];
  snprintf(line, sizeof(line), "EVENT LAYER active=%u base=%u\n", active, base);
  Serial.print(line);
  _keyboard.link().comWrite(reinterpret_cast<const uint8_t *>(line), strlen(line));
}

void SerialCli::emitBleEvent(bool connected) {
  if (!_events) return;
  char line[48];
  snprintf(line, sizeof(line), "EVENT BLE state=%s\n", connected ? "CONNECTED" : "DISCONNECTED");
  Serial.print(line);
  _keyboard.link().comWrite(reinterpret_cast<const uint8_t *>(line), strlen(line));
}
