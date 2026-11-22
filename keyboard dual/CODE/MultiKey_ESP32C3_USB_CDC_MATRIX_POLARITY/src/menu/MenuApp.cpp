#include "MenuApp.h"
#include "../../Keyboard.h"
#include "../../KeyNames.h"
#include "../../HidUsages.h"
#include "../display/Ssd1306.h"
#include "../display/Icons.h"
#include "../led/Palettes.h"
#include "../games/Games.h"
#include <stdio.h>
#include <string.h>

// Char-grid pages for the key editor: letters, digits/symbols, controls, F-keys.
namespace {

struct CharPage {
    const char *title;
    uint8_t usages[40];
    uint8_t count;
};

const CharPage CHAR_PAGES[4] = {
    { "LETTERS", {
        0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
        0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D }, 26 },
    { "DIGITS", {
        0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38 }, 22 },
    { "CONTROL", {
        0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x39,
        0x4F, 0x50, 0x51, 0x52,
        0x4A, 0x4D, 0x4B, 0x4E, 0x49, 0x4C }, 16 },
    { "F-KEYS", {
        0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47, 0x48, 0x65 }, 16 },
};

} // namespace

namespace {

constexpr uint32_t TRANSITION_MS = 180;
constexpr uint32_t TRANSITION_ICON_MS = 320;
constexpr uint32_t LONG_PRESS_MS = 400;
constexpr uint32_t BRIGHTNESS_PALETTE_HOLD_MS = 500;
constexpr uint16_t GAMMA_STEPS = 16;

// Boot chooser: only the red keys remain; holding all four opens the menu.
// Firmware matrix indices mapped from the picture notation (cols 1..7 =
// ROW_PINS, rows 1..5 = COL_PINS, keys 1..35 left-to-right top-to-bottom).
const uint8_t BOOT_KEYS[4][4] = {
    { 0, 30, 4, 34 },   // RED   (picture 1, 7, 29, 35 - the four corners)
    { 6, 26, 8, 28 },   // GREEN (picture 9, 13, 23, 27)
    { 2, 32, 3, 33 },   // BLUE  (picture 15, 21, 22, 28)
    { 17, 18, 22, 23 }, // YELLOW(picture 18, 25, 19, 26)
};

const uint8_t *iconForScreen(MenuApp::ScreenId id) {
    switch (id) {
        case MenuApp::SCR_KB_MENU: return Icons::KEYBOARD;
        case MenuApp::SCR_LED_MENU: return Icons::LED;
        case MenuApp::SCR_HID_MENU: return Icons::CONNECTION;
        case MenuApp::SCR_GAMES: return Icons::GAMES;
        case MenuApp::SCR_OLED_MENU: return Icons::SCREEN;
        case MenuApp::SCR_PALETTES: return Icons::PALETTE;
        default: return Icons::GEAR;
    }
}

const char *effectName(uint8_t effect) {
    switch (effect) {
        case LED_OFF: return "OFF";
        case LED_STATIC: return "STATIC";
        case LED_RAINBOW: return "RAINBOW";
        case LED_BREATHE: return "BREATHE";
        case LED_CUSTOM: return "CUSTOM";
        default: return "FOLLOW";
    }
}

const char *keypressName(uint8_t animation) {
    switch (animation) {
        case KEYPRESS_NONE: return "OFF";
        case KEYPRESS_PULSE: return "PULSE";
        case KEYPRESS_SPARKLE: return "SPARKLE";
        case KEYPRESS_RIPPLE: return "RIPPLE";
        default: return "?";
    }
}

const char *sleepName(uint8_t animation) {
    switch (animation) {
        case SLEEP_OFF: return "OFF";
        case SLEEP_BREATHE: return "BREATHE";
        case SLEEP_RAINBOW: return "RAINBOW";
        default: return "?";
    }
}

const char *mixName(uint8_t mode) {
    switch (mode) {
        case MIX_ADD_NORM: return "ADD NORM";
        case MIX_ADD: return "ADD CLIP";
        case MIX_MAX: return "MAX";
        case MIX_SCREEN: return "SCREEN";
        case MIX_HUE: return "HUE";
        default: return "?";
    }
}

} // namespace

void MenuApp::begin(MultiKey &keyboard) {
    _kbd = &keyboard;
    _state = STATE_IDLE;
    _depth = 0;
}

void MenuApp::open(bool force) {
    if (_kbd == nullptr) return;
    if (_exited && !force) return; // main menu exited: only a reboot reopens it
    if (_kbd->globals().bootMenuEnabled) {
        _state = STATE_BOOT_CHOOSER;
        _stateEnteredMs = millis();
        _lastActivityMs = _stateEnteredMs;
        _bootFlashStartedMs = 0;
    } else {
        _state = STATE_SCREEN;
        _stack[0] = { SCR_MAIN, 0, 0 };
        _depth = 1;
        _stateEnteredMs = millis();
        _lastActivityMs = _stateEnteredMs;
    }
}

void MenuApp::close() {
    _state = STATE_IDLE;
    _depth = 0;
    if (_kbd != nullptr) _kbd->led().clearOverlay();
}

void MenuApp::startGame(uint8_t index) {
    if (_kbd == nullptr) return;
    _kbd->games().start(index, *_kbd);
    _state = STATE_GAME;
    _lastActivityMs = millis();
}

// ---------------------------------------------------------------------------

MenuInput MenuApp::readInput(uint32_t nowMs) {
    MenuInput in;
    const GlobalSettings &g = _kbd->globals();
    in.up = _kbd->justPressed(g.oledNavKeys[0]);
    in.down = _kbd->justPressed(g.oledNavKeys[3]);
    in.left = _kbd->justPressed(g.oledNavKeys[2]);
    in.right = _kbd->justPressed(g.oledNavKeys[1]);
    in.enter = _kbd->justPressed(g.oledNavKeys[4]);
    in.upHeld = _kbd->heldFor(g.oledNavKeys[0], LONG_PRESS_MS);
    in.downHeld = _kbd->heldFor(g.oledNavKeys[3], LONG_PRESS_MS);
    in.enterHeld = _kbd->heldFor(g.oledNavKeys[4], LONG_PRESS_MS);
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (_kbd->isPressed(i)) in.anyHeld = true;
    }
    // First non-nav key press this tick (games and the key editor use it).
    static const uint8_t navDefaults[5] = { 16, 12, 22, 18, 17 }; // picture keys 11/17/19/25/18
    for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
        if (!_kbd->justPressed(i)) continue;
        bool isNav = false;
        for (uint8_t n = 0; n < 5; ++n) {
            if (i == g.oledNavKeys[n] || i == navDefaults[n]) { isNav = true; break; }
        }
        if (!isNav) {
            in.pressedBody = true;
            in.bodyRow = i / Config::COLS;
            in.bodyCol = i % Config::COLS;
            break;
        }
    }
    if (in.anyHeld) _lastActivityMs = nowMs;
    return in;
}

void MenuApp::setOverlay(uint32_t nowMs) {
    const GlobalSettings &g = _kbd->globals();
    LedController &led = _kbd->led();
    // Direct mapping, NOT gammaBrightness(): FastLED's global brightness
    // already attenuates, and gamma on top made 30/255 invisible (~1/255).
    const uint8_t v = g.menuNavBrightness > 255 ? 255 : static_cast<uint8_t>(g.menuNavBrightness);
    // Nav keys glow red in menu mode; the LED overlay plane renders on top of
    // every animation, so effects can never override them.
    const CRGB navColor(v, 0, 0);
    const CRGB enterColor = CHSV(96, 255, qadd8(v, 60)); // enter: distinct colour
    for (uint8_t n = 0; n < 4; ++n) {
        const uint8_t key = g.oledNavKeys[n];
        CRGB c = navColor;
        if (_kbd->isPressed(key)) c = CRGB(qadd8(c.r, 120), qadd8(c.g, 120), qadd8(c.b, 120));
        led.setOverlayColor(key, c);
    }
    const uint8_t enterKey = g.oledNavKeys[4];
    CRGB ec = enterColor;
    if (_kbd->isPressed(enterKey)) ec = CRGB(255, 255, 255);
    led.setOverlayColor(enterKey, ec);
}

// ---------------------------------------------------------------------------
// Screen stack

void MenuApp::pushScreen(ScreenId id, int16_t param, int16_t param2) {
    if (_depth >= sizeof(_stack) / sizeof(_stack[0])) return;
    _transitionTarget = { id, param, param2 };
    _transitionForward = true;
    _transitionStartedMs = millis();
    _state = STATE_TRANSITION;
}

void MenuApp::replaceScreen(ScreenId id, int16_t param, int16_t param2) {
    _transitionTarget = { id, param, param2 };
    _transitionForward = true;
    _transitionStartedMs = millis();
    _state = STATE_TRANSITION;
}

void MenuApp::popScreen() {
    if (_depth <= 1) {
        // Slide back out of the menu tree entirely.
        _transitionTarget = { SCR_MAIN, 0, 0 };
        _transitionForward = false;
        _transitionStartedMs = millis();
        _state = STATE_TRANSITION;
        _depth = 0; // transition end closes to idle
        return;
    }
    --_depth;
    _transitionTarget = _stack[_depth - 1];
    _transitionForward = false;
    _transitionStartedMs = millis();
    _state = STATE_TRANSITION;
}

// ---------------------------------------------------------------------------

void MenuApp::update(uint32_t nowMs) {
    if (_kbd == nullptr) return;

    if (_state == STATE_IDLE) {
        // Idle status screen.
        Ssd1306 &d = Display;
        d.clear();
        d.drawText(1, 0, "MULTIKEY READY", true);
        d.hline(0, 8, Config::OLED_WIDTH, true);
        char line[24];
        snprintf(line, sizeof(line), "LAYER %u %s", _kbd->activeLayer(),
                 _kbd->layerName(_kbd->activeLayer()));
        d.drawText(1, 14, line, true);
        snprintf(line, sizeof(line), "BLE %s", _kbd->hid().isConnected() ? "LINKED" : "WAITING");
        d.drawText(1, 24, line, true);
        const GlobalSettings &g = _kbd->globals();
        snprintf(line, sizeof(line), "NAV %u-%u ENTER %u", g.oledNavKeys[2], g.oledNavKeys[1],
                 g.oledNavKeys[4]);
        d.drawText(1, 56, "MENU: RED KEYS AT BOOT", true);
        return;
    }

    setOverlay(nowMs);
    const MenuInput in = readInput(nowMs);

    switch (_state) {
    case STATE_BOOT_CHOOSER:
        handleBootChooser(in, nowMs);
        break;
    case STATE_TRANSITION: {
        if (static_cast<uint32_t>(nowMs - _transitionStartedMs) >= TRANSITION_MS + TRANSITION_ICON_MS) {
            if (_depth == 0 && !_transitionForward) {
                close();
            } else {
                if (_transitionForward) {
                    _stack[_depth] = _transitionTarget;
                    ++_depth;
                }
                _state = STATE_SCREEN;
            }
        }
        break;
    }
    case STATE_SCREEN:
        handleScreen(in, nowMs);
        break;
    case STATE_VALUE_EDIT:
        handleValueEdit(in);
        break;
    case STATE_COLOR_PICKER:
        handleColorPicker(in, nowMs);
        break;
    case STATE_GAME: {
        Game *game = _kbd->games().active();
        if (game == nullptr) {
            popScreen();
            break;
        }
        if (in.left && _kbd->heldFor(_kbd->globals().oledNavKeys[2], 600)) game->exit();
        game->update(in, nowMs);
        if (game->finished()) {
            _kbd->games().stop();
            _kbd->led().clearOverlay();
            popScreen();
        }
        break;
    }
    default:
        break;
    }

    // Render one display frame.
    Ssd1306 &d = Display;
    d.clear();
    switch (_state) {
    case STATE_BOOT_CHOOSER:
        drawBootChooser(d, nowMs);
        break;
    case STATE_TRANSITION: {
        const uint32_t elapsed = nowMs - _transitionStartedMs;
        int16_t off = 0;
        if (elapsed < TRANSITION_ICON_MS) {
            // Phase 1: splash icon slides in.
            const uint8_t *icon = iconForScreen(_transitionTarget.id);
            const int16_t x = _transitionForward
                ? static_cast<int16_t>(Config::OLED_WIDTH - (elapsed * Config::OLED_WIDTH / TRANSITION_ICON_MS))
                : static_cast<int16_t>(-static_cast<int16_t>(elapsed * Config::OLED_WIDTH / TRANSITION_ICON_MS));
            d.fillRect(0, 0, Config::OLED_WIDTH, Config::OLED_HEIGHT, true);
            const int16_t ix = 58 + (x / 6);
            // Icon drawn inverted on the filled background.
            for (uint8_t row = 0; row < 12; ++row) {
                for (uint8_t col = 0; col < 12; ++col) {
                    const bool on = (icon[row * 2 + (col / 8)] >> (7 - (col % 8))) & 1;
                    if (on) d.pixel(ix + col, 18 + row, false);
                }
            }
            break;
        }
        // Phase 2: list slides in over the outgoing screen.
        const uint32_t slideElapsed = elapsed - TRANSITION_ICON_MS;
        if (slideElapsed < TRANSITION_MS) {
            off = _transitionForward
                ? static_cast<int16_t>(Config::OLED_WIDTH - slideElapsed * Config::OLED_WIDTH / TRANSITION_MS)
                : -static_cast<int16_t>(Config::OLED_WIDTH - slideElapsed * Config::OLED_WIDTH / TRANSITION_MS);
        }
        if (_transitionForward || _depth > 1) {
            Screen target = _transitionTarget;
            drawScreen(target, d, off, nowMs);
        } else {
            drawFrame(d, off, nowMs);
        }
        break;
    }
    case STATE_VALUE_EDIT:
        drawValueEdit(d, 0);
        break;
    case STATE_COLOR_PICKER:
        drawColorPicker(d, 0, nowMs);
        break;
    case STATE_GAME: {
        Game *game = _kbd->games().active();
        if (game != nullptr) game->render(d);
        break;
    }
    default:
        drawFrame(d, 0, nowMs);
        break;
    }
}

void MenuApp::drawFrame(Ssd1306 &d, int16_t xOff, uint32_t nowMs) {
    if (_depth == 0) return;
    Screen current = _stack[_depth - 1];
    drawScreen(current, d, xOff, nowMs);
}

void MenuApp::drawScreen(Screen &screen, Ssd1306 &d, int16_t xOff, uint32_t nowMs) {
    switch (screen.id) {
    case SCR_KEYGRID:
    case SCR_LED_PERKEY:
        drawKeyGrid(d, xOff, static_cast<uint8_t>(screen.param));
        break;
    case SCR_KEY_CHARS:
        drawGrid(d, xOff, CHAR_PAGES[_editCharPage].title);
        break;
    default: {
        MenuList list;
        buildList(screen, list);
        list.select(screen.selected); // restore the cursor (rebuilt every frame)
        list.render(d, xOff, nowMs);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Boot chooser

void MenuApp::handleBootChooser(const MenuInput &in, uint32_t nowMs) {
    LedController &led = _kbd->led();
    constexpr uint32_t FLASH_MS = 250;

    // Phase 2: the red keys flashed once - slide forward into the main menu.
    if (_bootFlashStartedMs != 0) {
        const uint32_t elapsed = nowMs - _bootFlashStartedMs;
        const CRGB flashColor = (elapsed / 60) & 1 ? CRGB::White : CRGB(255, 0, 0);
        for (uint8_t k = 0; k < 4; ++k) {
            led.setOverlayColor(BOOT_KEYS[0][k], flashColor);
        }
        if (elapsed >= FLASH_MS) {
            led.clearOverlay();
            pushScreen(SCR_MAIN);
        }
        return;
    }

    // Phase 1: red keys lit; pressing all four within the window opens the
    // main menu (via the flash above). Otherwise the window just expires.
    for (uint8_t k = 0; k < 4; ++k) {
        led.setOverlayColor(BOOT_KEYS[0][k], CRGB(255, 0, 0));
    }
    uint8_t redPressed = 0;
    for (uint8_t k = 0; k < 4; ++k) {
        if (_kbd->isPressed(BOOT_KEYS[0][k])) ++redPressed;
    }
    if (redPressed == 4) {
        _bootFlashStartedMs = nowMs;
        return;
    }
    (void)in;
    if (static_cast<uint32_t>(nowMs - _stateEnteredMs) >= _kbd->globals().bootMenuTimeMs) {
        led.clearOverlay();
        close();
    }
}

void MenuApp::drawBootChooser(Ssd1306 &d, uint32_t nowMs) {
    (void)nowMs;
    d.drawText(1, 0, "MAIN MENU", true);
    d.hline(0, 8, Config::OLED_WIDTH, true);
    if (_bootFlashStartedMs != 0) {
        d.drawText(1, 22, "OPENING...", true);
        return;
    }
    d.drawText(1, 16, "HOLD ALL RED", true);
    d.drawText(1, 26, "KEYS TO OPEN", true);
    char line[24];
    const uint32_t left = (_kbd->globals().bootMenuTimeMs >
                           static_cast<uint32_t>(nowMs - _stateEnteredMs))
        ? (_kbd->globals().bootMenuTimeMs - (nowMs - _stateEnteredMs)) / 1000
        : 0;
    snprintf(line, sizeof(line), "TIMEOUT %lu S", (unsigned long)(left + 1));
    d.drawText(1, 52, line, true);
}

// ---------------------------------------------------------------------------
// Generic grid picker

void MenuApp::drawGrid(Ssd1306 &d, int16_t xOff, const char *title) {
    d.drawText(xOff + 1, 0, title, true);
    d.hline(xOff, 8, Config::OLED_WIDTH, true);
    const uint8_t rowsVisible = 6;
    const uint8_t cellsPerPage = _grid.cols * rowsVisible;
    const uint8_t page = _grid.cursor / cellsPerPage;
    const uint8_t start = page * cellsPerPage;
    const uint8_t cellW = Config::OLED_WIDTH / _grid.cols;
    for (uint8_t cell = 0; cell < cellsPerPage && start + cell < _grid.count; ++cell) {
        const uint8_t index = start + cell;
        const int16_t cx = xOff + (cell % _grid.cols) * cellW;
        const int16_t cy = 10 + (cell / _grid.cols) * 9;
        const bool selected = index == _grid.cursor;
        if (selected) d.fillRect(cx, cy - 1, cellW, 9, true);
        d.drawText(cx + 1, cy, _grid.labels[index], !selected);
    }
}

int8_t MenuApp::handleGrid(const MenuInput &in) {
    if (in.up && _grid.cursor >= _grid.cols) _grid.cursor -= _grid.cols;
    if (in.down && _grid.cursor + _grid.cols < _grid.count) _grid.cursor += _grid.cols;
    if (in.left) {
        if (_grid.cursor % _grid.cols == 0) return MENU_BACK;
        --_grid.cursor;
    }
    if (in.right && _grid.cursor % _grid.cols + 1 < _grid.cols &&
        _grid.cursor + 1 < _grid.count) {
        ++_grid.cursor;
    }
    if (in.enter) return static_cast<int8_t>(_grid.cursor);
    return MENU_NONE;
}

// ---------------------------------------------------------------------------
// Key grid (keyboard config editor)

void MenuApp::drawKeyGrid(Ssd1306 &d, int16_t xOff, uint8_t layer) {
    // Grid is drawn in the physical orientation from the schematic: 7 columns
    // (matrix rows / ROW_PINS) wide by 5 rows (matrix cols / COL_PINS) tall —
    // i.e. rotated 90 deg CCW relative to the matrix indexing.
    const uint8_t cellW = 18; // 7 cells * 18 = 126 px
    const uint8_t cellH = 10; // 5 cells * 10 = 50 px
    char title[24];
    snprintf(title, sizeof(title), "L%u %s KEYS", layer, _kbd->layerName(layer));
    d.drawText(xOff + 1, 0, title, true);
    for (uint8_t row = 0; row < Config::ROWS; ++row) {
        for (uint8_t col = 0; col < Config::COLS; ++col) {
            const uint8_t index = row * Config::COLS + col;
            const int16_t x = xOff + row * cellW;     // matrix row -> horizontal
            const int16_t y = 8 + col * cellH;        // matrix col -> vertical
            const KeyAction action = _kbd->keyAction(layer, index);
            const bool selected = index == _editKey;
            if (selected) {
                d.fillRect(x, y, cellW - 1, cellH - 1, true);
                continue;
            }
            d.rect(x, y, cellW - 1, cellH - 1, true);
            if (action.type == ACTION_HID && action.usage != 0) {
                d.fillRect(x + 5, y + 3, cellW - 11, cellH - 7, true);
            } else if (action.type == ACTION_LAYER) {
                d.fillRect(x + 1, y + 1, cellW - 3, cellH - 3, true);
            }
        }
    }
    const KeyAction action = _kbd->keyAction(layer, _editKey);
    const uint8_t lang = _kbd->globals().hidLang;
    char line[26];
    if (action.type == ACTION_HID) {
        if (action.usage == 0 && action.modifiers == 0) {
            snprintf(line, sizeof(line), "K%u NONE", _editKey);
        } else {
            snprintf(line, sizeof(line), "K%u %s%s%s", _editKey,
                     (action.modifiers & MOD_LCTRL) ? "C+" : "",
                     (action.modifiers & MOD_LSHIFT) ? "S+" : "",
                     hidKeyName(action.usage, lang));
        }
    } else if (action.type == ACTION_LAYER) {
        snprintf(line, sizeof(line), "K%u LAYER %u M%u", _editKey, action.layer, action.layerMode);
    } else {
        snprintf(line, sizeof(line), "K%u TRANSPARENT", _editKey);
    }
    d.drawText(xOff + 1, 58, line, true);
}

// ---------------------------------------------------------------------------
// Value editor

void MenuApp::drawValueEdit(Ssd1306 &d, int16_t xOff) {
    d.drawText(xOff + 1, 0, _valueTitle, true);
    d.hline(xOff, 8, Config::OLED_WIDTH, true);
    char line[24];
    snprintf(line, sizeof(line), "%u", _valueWorking);
    d.drawText(xOff + 1, 14, line, true);
    // Progress bar across the range.
    const uint16_t span = _valueMax - _valueMin ? _valueMax - _valueMin : 1;
    const uint8_t width = static_cast<uint8_t>(
        (static_cast<uint32_t>(_valueWorking - _valueMin) * (Config::OLED_WIDTH - 4)) / span);
    d.rect(xOff + 1, 26, Config::OLED_WIDTH - 2, 8, true);
    d.fillRect(xOff + 2, 27, width, 6, true);
    if (_valueBrightnessRange != 0) {
        static const char *ranges[4] = { "", "RANGE 0-50", "RANGE 50-100", "RANGE 100-255" };
        d.drawText(xOff + 1, 40, "GAMMA PALETTE", true);
        d.drawText(xOff + 1, 50, ranges[_valueBrightnessRange], true);
    } else {
        d.drawText(xOff + 1, 40, "UP/DN - CHANGE", true);
        d.drawText(xOff + 1, 50, "ENT - OK  LEFT - BACK", true);
    }
}

void MenuApp::handleValueEdit(const MenuInput &in) {
    if (_valueBrightnessRange != 0) {
        // Nonlinear brightness palette: 16 gamma steps inside the range.
        static const uint16_t rangeLo[4] = { 0, 0, 50, 100 };
        static const uint16_t rangeHi[4] = { 0, 50, 100, 255 };
        const uint16_t lo = rangeLo[_valueBrightnessRange];
        const uint16_t hi = rangeHi[_valueBrightnessRange];
        uint16_t stepIndex = 0;
        for (uint16_t i = 0; i < GAMMA_STEPS; ++i) {
            const uint16_t v = lo + (static_cast<uint32_t>(gammaBrightness(
                static_cast<uint8_t>(i * 255 / (GAMMA_STEPS - 1)))) * (hi - lo)) / 255;
            if (v <= _valueWorking) stepIndex = i;
        }
        if (in.up && stepIndex + 1 < GAMMA_STEPS) ++stepIndex;
        if (in.down && stepIndex > 0) --stepIndex;
        _valueWorking = lo + (static_cast<uint32_t>(gammaBrightness(
            static_cast<uint8_t>(stepIndex * 255 / (GAMMA_STEPS - 1)))) * (hi - lo)) / 255;
        // Press-holding up/down again cycles the range.
        if ((in.upHeld || in.downHeld) &&
            static_cast<uint32_t>(millis() - _brightnessPaletteEnteredMs) > BRIGHTNESS_PALETTE_HOLD_MS) {
            _valueBrightnessRange = _valueBrightnessRange >= 3 ? 1 : _valueBrightnessRange + 1;
            _brightnessPaletteEnteredMs = millis();
        }
        if (in.enter) {
            _valueBrightnessRange = 0; // back to direct edit, keep value
        }
        if (in.left) {
            _valueBrightnessRange = 0;
        }
        return;
    }

    if (in.up) {
        _valueWorking = _valueWorking + _valueStep > _valueMax ? _valueMax
                        : static_cast<uint16_t>(_valueWorking + _valueStep);
    }
    if (in.down) {
        _valueWorking = _valueWorking < _valueMin + _valueStep ? _valueMin
                        : static_cast<uint16_t>(_valueWorking - _valueStep);
    }
    if (_valueBrightness && in.upHeld &&
        static_cast<uint32_t>(millis() - _stateEnteredMs) > LONG_PRESS_MS) {
        _valueBrightnessRange = 1;
        _brightnessPaletteEnteredMs = millis();
        return;
    }
    if (in.enter) {
        applyValue(_valueWorking);
        _state = STATE_SCREEN;
    }
    if (in.left) {
        _state = STATE_SCREEN;
    }
}

// ---------------------------------------------------------------------------
// Colour picker

void MenuApp::applyPickedColor() {
    applyColor(_pickColor, _pickBrightness);
}

void MenuApp::handleColorPicker(const MenuInput &in, uint32_t nowMs) {
    LedController &led = _kbd->led();
    const bool blinkOff = (nowMs % 1200) < 120;

    if (!_pickPaletteMode) {
        // Mode list: PALETTE / HUE / R / G / B / DEPTH.
        if (in.up && _pickMode > 0) --_pickMode;
        if (in.down && _pickMode < 5) ++_pickMode;
        if (in.left) {
            _state = STATE_SCREEN;
            return;
        }
        if (in.enter) {
            _pickPaletteMode = true;
            _pickHoldSinceMs = nowMs;
        }
        return;
    }

    if (in.up || in.left) _pickPosition = _pickPosition < 252 ? _pickPosition + 4 : 255;
    if (in.down || in.right) _pickPosition = _pickPosition > 3 ? _pickPosition - 4 : 0;
    if (_pickMode == 0 && (in.upHeld || in.downHeld) &&
        static_cast<uint32_t>(nowMs - _pickHoldSinceMs) > LONG_PRESS_MS) {
        // HOLD up/down: next palette; the palette is shown across the keys.
        _pickPalette = (_pickPalette + 1) % NUM_PALETTES;
        _pickHoldSinceMs = nowMs;
    }
    if (in.enterHeld) {
        // HOLD the blinking (enter) key: confirm and leave the picker.
        applyPickedColor();
        _pickPaletteMode = false;
        _state = STATE_SCREEN;
        return;
    }

    // Compose the working colour from the mode + position.
    switch (_pickMode) {
    case 0: {
        const Palette &p = _kbd->palettes()[_pickPalette];
        _pickColor = paletteColor(p, _pickPosition);
        break;
    }
    case 1:
        _pickColor = CHSV(_pickPosition, 255, 255);
        break;
    case 2:
        _pickColor.r = _pickPosition;
        break;
    case 3:
        _pickColor.g = _pickPosition;
        break;
    case 4:
        _pickColor.b = _pickPosition;
        break;
    case 5: {
        const CHSV hsv = rgb2hsv_approximate(CRGB(_pickColor.r, _pickColor.g, _pickColor.b));
        _pickColor = CHSV(hsv.h, 255 - _pickPosition, 255);
        break;
    }
    default:
        break;
    }

    // Palette mode: display the whole palette across the keys.
    if (_pickMode == 0) {
        const Palette &p = _kbd->palettes()[_pickPalette];
        for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
            const uint8_t pos = static_cast<uint8_t>(i * 255 / (Config::NUM_KEYS - 1));
            led.setOverlayColor(i, paletteColor(p, pos));
        }
    }

    // Preview on the target key(s) at actual brightness (with a blink).
    CRGB preview = _pickColor;
    preview.nscale8_video(_pickBrightness);
    if (!blinkOff) {
        if (_pickTargetKey == 255) {
            for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) led.setOverlayColor(i, preview);
        } else {
            led.setOverlayColor(_pickTargetKey, preview);
        }
    }
}

void MenuApp::drawColorPicker(Ssd1306 &d, int16_t xOff, uint32_t nowMs) {
    (void)nowMs;
    char line[24];
    if (!_pickPaletteMode) {
        d.drawText(xOff + 1, 0, "COLOUR MODE", true);
        d.hline(xOff, 8, Config::OLED_WIDTH, true);
        static const char *modes[] = { "PALETTE", "HUE", "R", "G", "B", "DEPTH" };
        for (uint8_t i = 0; i < 6; ++i) {
            const bool sel = i == _pickMode;
            const int16_t y = 12 + i * 8;
            if (sel) d.fillRect(xOff, y, Config::OLED_WIDTH, 8, true);
            d.drawText(xOff + 2, y + 1, modes[i], !sel);
        }
        return;
    }
    static const char *modeNames[] = { "PALETTE", "HUE", "RED", "GREEN", "BLUE", "DEPTH" };
    snprintf(line, sizeof(line), "%s %u", modeNames[_pickMode], _pickPosition);
    d.drawText(xOff + 1, 0, line, true);
    d.hline(xOff, 8, Config::OLED_WIDTH, true);
    if (_pickMode == 0) {
        snprintf(line, sizeof(line), "%s", paletteName(_pickPalette));
        d.drawText(xOff + 1, 14, line, true);
        d.drawText(xOff + 1, 26, "HOLD UP/DN - NEXT PAL", true);
    }
    d.rect(xOff + 1, 40, Config::OLED_WIDTH - 2, 8, true);
    d.fillRect(xOff + 2, 41, 1 + _pickPosition * (Config::OLED_WIDTH - 4) / 255, 6, true);
    d.drawText(xOff + 1, 54, "HOLD ENTER - PICK", true);
}

void MenuApp::buildList(Screen &screen, MenuList &list) {
    char line[24];
    switch (screen.id) {
    case SCR_MAIN:
        list.begin("MULTIKEY");
        list.addItem("KEY CFG", SCR_KB_MENU);
        list.addItem("LED CFG", SCR_LED_MENU);
        list.addItem("HID & CONNECT", SCR_HID_MENU);
        list.addItem("GAMES", SCR_GAMES);
        list.addItem("SCREEN", SCR_OLED_MENU);
        break;
    case SCR_KB_MENU:
        list.begin("KEY CFG");
        list.addItem("LAYERS", SCR_LAYERS);
        list.addItem("EDIT KEYS", SCR_KEYGRID);
        list.addItem("HOLD THRESHOLD", 100);
        list.addItem(_kbd->globals().hidLang == HID_LABEL_RU ? "LABELS: RU" : "LABELS: EN", 101);
        list.addItem("PRESETS", SCR_PRESETS);
        list.addItem("EXIT", -1); // leave the menu; reopens only after reboot
        break;
    case SCR_LAYERS: {
        list.begin("LAYERS");
        for (uint8_t layer = 0; layer < _kbd->layerCount(); ++layer) {
            snprintf(line, sizeof(line), "%u %s%s", layer + 1, _kbd->layerName(layer),
                     _kbd->baseLayer() == layer ? " *" : "");
            list.addItem(line, layer);
        }
        list.addItem("+ ADD LAYER", 100);
        list.addItem("- REMOVE LAST", 101);
        break;
    }
    case SCR_LAYER_OPTS:
        list.begin(_kbd->layerName(static_cast<uint8_t>(screen.param)));
        list.addItem("SELECT AS BASE", 0);
        list.addItem("EDIT KEYS", 1);
        list.addItem("RENAME", 2);
        break;
    case SCR_KEY_ACTION:
        list.begin("KEY TYPE");
        list.addItem("HID KEY", 0);
        list.addItem("LAYER KEY", 1);
        list.addItem("TRANSPARENT", 2);
        list.addItem("NONE (DISABLE)", 3);
        break;
    case SCR_KEY_MODS:
        list.begin("MODIFIERS");
        for (uint8_t i = 0; i < 8; ++i) {
            const bool on = (_editCharPage & (1U << i)) != 0;
            static const char *names[8] = { "LCTRL", "LSHIFT", "LALT", "LGUI",
                                            "RCTRL", "RSHIFT", "RALT", "RGUI" };
            snprintf(line, sizeof(line), "[%c] %s", on ? 'x' : ' ', names[i]);
            list.addItem(line, i);
        }
        list.addItem("DONE", 100);
        break;
    case SCR_KEY_LAYER_TARGET:
        list.begin("TARGET LAYER");
        for (uint8_t layer = 0; layer < _kbd->layerCount(); ++layer) {
            snprintf(line, sizeof(line), "%u %s", layer + 1, _kbd->layerName(layer));
            list.addItem(line, layer);
        }
        break;
    case SCR_KEY_LAYER_MODE:
        list.begin("LAYER MODE");
        list.addItem("MOMENTARY", LAYER_MOMENTARY);
        list.addItem("SWITCH", LAYER_SWITCH);
        list.addItem("MODAL", LAYER_MODAL);
        break;
    case SCR_PRESETS:
        list.begin("PRESETS");
        for (uint8_t slot = 0; slot < MULTIKEY_SLOT_COUNT; ++slot) {
            snprintf(line, sizeof(line), "SLOT %u %s", slot + 1,
                     _kbd->presetExists(slot) ? "SET" : "EMPTY");
            list.addItem(line, slot);
        }
        break;
    case SCR_PRESET_OPTS:
        list.begin("SLOT");
        list.addItem("SAVE CONFIG", 0);
        list.addItem("LOAD CONFIG", 1);
        break;
    case SCR_LED_MENU:
        list.begin("LED CFG");
        list.addItem("GENERAL", SCR_LED_GENERAL);
        list.addItem("PER-LAYER", SCR_LED_LAYER_SEL);
        list.addItem("PER-KEY", SCR_LED_PERKEY);
        list.addItem("LAYER CHANGE", SCR_LED_FLASH);
        list.addItem("KEYPRESS FX", SCR_LED_KEYPRESS);
        list.addItem("PALETTES", SCR_PALETTES);
        break;
    case SCR_LED_GENERAL: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        list.begin("LED GENERAL");
        snprintf(line, sizeof(line), "ENABLED %s", s.enabled ? "ON" : "OFF");
        list.addItem(line, 0);
        snprintf(line, sizeof(line), "BRIGHTNESS %u", s.brightness);
        list.addItem(line, 1);
        snprintf(line, sizeof(line), "EFFECT %s", effectName(s.effect));
        list.addItem(line, 2);
        snprintf(line, sizeof(line), "FX SPEED %u", s.effectSpeed);
        list.addItem(line, 3);
        snprintf(line, sizeof(line), "BACKLIGHT %u", s.backlightBrightness);
        list.addItem(line, 4);
        list.addItem("BACKLIGHT COLOUR", 5);
        snprintf(line, sizeof(line), "SLEEP %s", sleepName(s.sleepAnimation));
        list.addItem(line, 6);
        snprintf(line, sizeof(line), "SLEEP TIMEOUT %lus", static_cast<unsigned long>(s.sleepTimeoutMs / 1000));
        list.addItem(line, 7);
        snprintf(line, sizeof(line), "MIX %s", mixName(s.mixMode));
        list.addItem(line, 8);
        break;
    }
    case SCR_LED_LAYER_SEL:
        list.begin("LAYER LED");
        for (uint8_t layer = 0; layer < _kbd->layerCount(); ++layer) {
            snprintf(line, sizeof(line), "%u %s", layer + 1, _kbd->layerName(layer));
            list.addItem(line, layer);
        }
        break;
    case SCR_LED_LAYER: {
        const LayerLedSettings s = _kbd->layerLed(static_cast<uint8_t>(screen.param));
        list.begin(_kbd->layerName(static_cast<uint8_t>(screen.param)));
        snprintf(line, sizeof(line), "BRIGHTNESS %u", s.brightness);
        list.addItem(line, 0);
        snprintf(line, sizeof(line), "EFFECT %s", s.effect == LAYER_LED_FOLLOW_GLOBAL ? "FOLLOW" : effectName(s.effect));
        list.addItem(line, 1);
        list.addItem("COLOUR", 2);
        break;
    }
    case SCR_LED_FLASH: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        list.begin("LAYER CHANGE");
        snprintf(line, sizeof(line), "ENABLED %s", s.layerFlashEnabled ? "ON" : "OFF");
        list.addItem(line, 0);
        list.addItem("NEXT COLOUR", 1);
        list.addItem("PREV COLOUR", 2);
        snprintf(line, sizeof(line), "FADE %ums", s.layerFlashFadeMs);
        list.addItem(line, 3);
        break;
    }
    case SCR_LED_KEYPRESS: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        list.begin("KEYPRESS FX");
        snprintf(line, sizeof(line), "ANIM %s", keypressName(s.keypressAnimation));
        list.addItem(line, 0);
        snprintf(line, sizeof(line), "SPEED %u", s.keypressSpeed);
        list.addItem(line, 1);
        break;
    }
    case SCR_PALETTES:
        list.begin("PALETTES");
        for (uint8_t i = 0; i < NUM_PALETTES; ++i) {
            const Palette &p = _kbd->palettes()[i];
            if (p.count == 0) {
                snprintf(line, sizeof(line), "%u EMPTY", i + 1);
            } else {
                snprintf(line, sizeof(line), "%u %s", i + 1, paletteName(i));
            }
            list.addItem(line, i);
        }
        break;
    case SCR_PALETTE_EDIT: {
        const Palette &p = _kbd->palettes()[static_cast<uint8_t>(screen.param)];
        list.begin(paletteName(static_cast<uint8_t>(screen.param)));
        for (uint8_t i = 0; i < p.count; ++i) {
            snprintf(line, sizeof(line), "STOP %u #%02X%02X%02X", i + 1,
                     p.color[i][0], p.color[i][1], p.color[i][2]);
            list.addItem(line, i);
        }
        if (p.count < PALETTE_MAX_STOPS) list.addItem("+ ADD STOP", 100);
        if (p.count > 2) list.addItem("- DEL LAST STOP", 101);
        break;
    }
    case SCR_HID_MENU: {
        const GlobalSettings &g = _kbd->globals();
        list.begin("HID & CONNECT");
        snprintf(line, sizeof(line), "USB %s", g.usbMode == USB_MODE_HID ? "HID" : "CFG");
        list.addItem(line, 0);
        snprintf(line, sizeof(line), "BT %s", g.btMode == BT_MODE_HID ? "HID" : "COM");
        list.addItem(line, 1);
        snprintf(line, sizeof(line), "BT POWER %u", g.btTxPower);
        list.addItem(line, 2);
        snprintf(line, sizeof(line), "ROLLOVER %s", g.rollover == ROLLOVER_NKRO ? "NKRO" : "6KRO");
        list.addItem(line, 3);
        list.addItem("MULTIKEY", SCR_MULTIKEY);
        break;
    }
    case SCR_MULTIKEY: {
        const GlobalSettings &g = _kbd->globals();
        list.begin("MULTIKEY");
        snprintf(line, sizeof(line), "ENABLED %s", g.multikeyEnabled ? "ON" : "OFF");
        list.addItem(line, 0);
        snprintf(line, sizeof(line), "KEYBOARD ID %u", g.multikeyId);
        list.addItem(line, 1);
        snprintf(line, sizeof(line), "SAVE SLAVE CFG %s", g.saveSlaveChanges ? "ON" : "OFF");
        list.addItem(line, 2);
        list.addItem("PUSH CONFIG NOW", 3);
        list.addItem("PUSH TO SLOT", 4);
        break;
    }
    case SCR_OLED_MENU: {
        const GlobalSettings &g = _kbd->globals();
        list.begin("SCREEN");
        snprintf(line, sizeof(line), "SLEEP %lus", static_cast<unsigned long>(g.oledSleepTimeoutMs / 1000));
        list.addItem(line, 0);
        list.addItem("NAV KEYS", SCR_NAVKEYS);
        snprintf(line, sizeof(line), "BOOT MENU %s", g.bootMenuEnabled ? "ON" : "OFF");
        list.addItem(line, 2);
        snprintf(line, sizeof(line), "BOOT TIME %ums", g.bootMenuTimeMs);
        list.addItem(line, 3);
        snprintf(line, sizeof(line), "NAV LED BRIGHT %u", g.menuNavBrightness);
        list.addItem(line, 4);
        break;
    }
    case SCR_NAVKEYS: {
        list.begin("NAV KEYS");
        static const char *names[5] = { "UP", "RIGHT", "LEFT", "DOWN", "ENTER" };
        for (uint8_t i = 0; i < 5; ++i) {
            snprintf(line, sizeof(line), "%s KEY %u", names[i], _kbd->globals().oledNavKeys[i]);
            list.addItem(line, i);
        }
        break;
    }
    case SCR_GAMES:
        list.begin("GAMES");
        for (uint8_t i = 0; i < _kbd->games().count(); ++i) {
            list.addItem(_kbd->games().name(i), i);
        }
        break;
    case SCR_CONFIRM:
        list.begin("CONFIRM");
        list.addItem("YES", 1);
        list.addItem("NO", 0);
        break;
    default:
        list.begin("?");
        break;
    }
}

void MenuApp::editValue(const char *title, uint16_t current, uint16_t min,
                        uint16_t max, uint16_t step, bool brightness, uint8_t target) {
    _valueTitle = title;
    _valueWorking = current;
    _valueMin = min;
    _valueMax = max;
    _valueStep = step;
    _valueBrightness = brightness;
    _valueBrightnessRange = 0;
    _valueTarget = target;
    _stateEnteredMs = millis();
    _state = STATE_VALUE_EDIT;
}

void MenuApp::applyValue(uint16_t value) {
    GlobalSettings &g = _kbd->globals();
    LedController &led = _kbd->led();
    switch (_valueTarget) {
    case VT_LED_ENABLE: led.setEnabled(value != 0); break;
    case VT_FLASH_ENABLE: {
        const LedGlobalSettings s = led.captureSettings();
        led.setLayerFlash(value != 0,
                          CRGB(s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB),
                          CRGB(s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB),
                          s.layerFlashFadeMs);
        break;
    }
    case VT_BRIGHTNESS: led.setBrightness(static_cast<uint8_t>(value)); break;
    case VT_FX_SPEED: led.setEffectSpeed(static_cast<uint8_t>(value)); break;
    case VT_KEYPRESS_SPEED: led.setKeypressSpeed(static_cast<uint8_t>(value)); break;
    case VT_SLEEP_TIMEOUT: led.setSleepTimeout(static_cast<uint32_t>(value) * 1000); break;
    case VT_HOLD: _kbd->setHoldDurationThreshold(value); break;
    case VT_FLASH_FADE: {
        const LedGlobalSettings s = led.captureSettings();
        led.setLayerFlash(s.layerFlashEnabled != 0,
                          CRGB(s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB),
                          CRGB(s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB),
                          value);
        break;
    }
    case VT_BACKLIGHT: {
        const LedGlobalSettings s = led.captureSettings();
        led.setBacklight(static_cast<uint8_t>(value),
                         CRGB(s.backlightR, s.backlightG, s.backlightB));
        break;
    }
    case VT_LAYER_BRIGHT: {
        LayerLedSettings s = _kbd->layerLed(static_cast<uint8_t>(_stack[_depth - 1].param));
        s.brightness = static_cast<uint8_t>(value);
        _kbd->setLayerLed(static_cast<uint8_t>(_stack[_depth - 1].param), s);
        break;
    }
    case VT_EFFECT: led.setEffect(static_cast<LedEffect>(value > LED_CUSTOM ? LED_CUSTOM : value)); break;
    case VT_LAYER_EFFECT: {
        LayerLedSettings s = _kbd->layerLed(static_cast<uint8_t>(_stack[_depth - 1].param));
        s.effect = static_cast<uint8_t>(value);
        _kbd->setLayerLed(static_cast<uint8_t>(_stack[_depth - 1].param), s);
        break;
    }
    case VT_KEYPRESS_ANIM: led.setKeypressAnimation(static_cast<KeypressAnimation>(value > KEYPRESS_RIPPLE ? KEYPRESS_RIPPLE : value)); break;
    case VT_SLEEP_ANIM: led.setSleepAnimation(static_cast<SleepAnimation>(value > SLEEP_RAINBOW ? SLEEP_RAINBOW : value)); break;
    case VT_MIX: led.setMixMode(static_cast<LedMixMode>(value > MIX_HUE ? MIX_HUE : value)); break;
    case VT_USB_MODE: g.usbMode = value ? USB_MODE_CFG : USB_MODE_HID; _kbd->noteGlobalsChanged(); break;
    case VT_BT_MODE: g.btMode = value ? BT_MODE_COM : BT_MODE_HID; _kbd->noteGlobalsChanged(); _kbd->applyGlobalSettings(); break;
    case VT_TXPOWER: g.btTxPower = value > 7 ? 7 : static_cast<uint8_t>(value); _kbd->noteGlobalsChanged(); _kbd->applyGlobalSettings(); break;
    case VT_ROLLOVER: g.rollover = value ? ROLLOVER_NKRO : ROLLOVER_6KRO; _kbd->noteGlobalsChanged(); _kbd->applyGlobalSettings(); break;
    case VT_OLED_SLEEP: g.oledSleepTimeoutMs = static_cast<uint32_t>(value) * 1000; _kbd->noteGlobalsChanged(); break;
    case VT_BOOT_TIME: g.bootMenuTimeMs = value < 1000 ? 1000 : static_cast<uint16_t>(value); _kbd->noteGlobalsChanged(); break;
    case VT_NAV_BRIGHT: g.menuNavBrightness = value; _kbd->noteGlobalsChanged(); break;
    case VT_MK_ID: g.multikeyId = value > 5 ? 5 : static_cast<uint8_t>(value); _kbd->noteGlobalsChanged(); _kbd->applyGlobalSettings(); break;
    case VT_MK_ENABLE: g.multikeyEnabled = value ? 1 : 0; _kbd->noteGlobalsChanged(); _kbd->applyGlobalSettings(); break;
    case VT_MK_SLAVE_SAVE: g.saveSlaveChanges = value ? 1 : 0; _kbd->noteGlobalsChanged(); break;
    case VT_BOOT_MENU_ENABLE: g.bootMenuEnabled = value ? 1 : 0; _kbd->noteGlobalsChanged(); break;
    default: break;
    }
}

void MenuApp::startColorPicker(uint8_t target, int16_t param, int16_t param2,
                               CRGB initial, uint8_t previewKey) {
    _colorTarget = target;
    _colorParam = param;
    _colorParam2 = param2;
    _pickColor = initial;
    _pickBrightness = 255;
    _pickMode = 0;
    _pickPosition = 128;
    _pickPaletteMode = false;
    _pickTargetKey = previewKey;
    _state = STATE_COLOR_PICKER;
}

void MenuApp::applyColor(CRGB color, uint8_t brightness) {
    LedController &led = _kbd->led();
    switch (_colorTarget) {
    case CT_BACKLIGHT: {
        const LedGlobalSettings s = led.captureSettings();
        led.setBacklight(s.backlightBrightness, color);
        break;
    }
    case CT_FLASH_NEXT: {
        const LedGlobalSettings s = led.captureSettings();
        led.setLayerFlash(s.layerFlashEnabled != 0, color,
                          CRGB(s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB),
                          s.layerFlashFadeMs);
        break;
    }
    case CT_FLASH_PREV: {
        const LedGlobalSettings s = led.captureSettings();
        led.setLayerFlash(s.layerFlashEnabled != 0,
                          CRGB(s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB),
                          color, s.layerFlashFadeMs);
        break;
    }
    case CT_LAYER_COLOR: {
        LayerLedSettings s = _kbd->layerLed(static_cast<uint8_t>(_colorParam));
        s.colorR = color.r; s.colorG = color.g; s.colorB = color.b;
        _kbd->setLayerLed(static_cast<uint8_t>(_colorParam), s);
        break;
    }
    case CT_PERKEY:
        led.setKeyColor(static_cast<uint8_t>(_colorParam), color);
        break;
    case CT_PALETTE_STOP: {
        Palette *p = _kbd->palettes();
        Palette &pal = p[_colorParam];
        const uint8_t stop = static_cast<uint8_t>(_colorParam2);
        if (stop < PALETTE_MAX_STOPS) {
            pal.color[stop][0] = color.r;
            pal.color[stop][1] = color.g;
            pal.color[stop][2] = color.b;
            _kbd->savePalettes();
        }
        break;
    }
    default:
        break;
    }
    (void)brightness;
}

const char *MenuApp::confirmText() const {
    return _confirmMessage != nullptr ? _confirmMessage : "SURE?";
}

// ---------------------------------------------------------------------------

void MenuApp::handleScreen(const MenuInput &in, uint32_t nowMs) {
    (void)nowMs;
    if (_depth == 0) return;
    Screen &screen = _stack[_depth - 1];

    // Screens with custom interaction.
    switch (screen.id) {
    case SCR_KEYGRID:
    case SCR_LED_PERKEY: {
        // Cursor moves in the drawn (physical) orientation: up/down walk the
        // matrix column (vertical on screen), left/right walk the matrix row.
        uint8_t &cursor = _editKey;
        if (in.up && cursor % Config::COLS > 0) --cursor;
        if (in.down && cursor % Config::COLS + 1 < Config::COLS) ++cursor;
        if (in.left && cursor >= Config::COLS) cursor -= Config::COLS;
        if (in.right && cursor + Config::COLS < Config::NUM_KEYS) cursor += Config::COLS;
        _editLayer = static_cast<uint8_t>(screen.param);
        uint8_t chosen = 255;
        if (in.pressedBody) chosen = static_cast<uint8_t>(in.bodyRow * Config::COLS + in.bodyCol);
        else if (in.enter) chosen = cursor;
        if (chosen != 255) {
            if (_pickKeyForColor) {
                _pickKeyForColor = false;
                const LedGlobalSettings s = _kbd->led().captureSettings();
                startColorPicker(CT_PERKEY, chosen, 0,
                                 CRGB(s.custom[chosen][0], s.custom[chosen][1], s.custom[chosen][2]),
                                 chosen);
            } else {
                cursor = chosen;
                pushScreen(SCR_KEY_ACTION, cursor, _editLayer);
            }
            return;
        }
        if (in.left && cursor % Config::COLS == 0) popScreen();
        return;
    }
    default:
        break;
    }

    MenuList list;
    buildList(screen, list);
    list.select(screen.selected); // restore the cursor (rebuilt every tick)
    const int8_t result = handleList(screen, in, list);
    if (result == MENU_BACK) popScreen();
}

int8_t MenuApp::handleList(Screen &screen, const MenuInput &in, MenuList &list) {
    const int8_t result = list.handleInput(in);
    screen.selected = list.selected();
    if (result == MENU_NONE || result == MENU_BACK) return result;

    const int16_t v = list.value(result);
    char line[24];
    switch (screen.id) {
    case SCR_MAIN:
        if (v == -1) {
            // EXIT: leave the menu tree; only a reboot offers it again.
            _exited = true;
            popScreen();
            return MENU_NONE;
        }
        pushScreen(static_cast<ScreenId>(v));
        return MENU_NONE;
    case SCR_KB_MENU:
        if (v == SCR_LAYERS) {
            pushScreen(SCR_LAYERS);
        } else if (v == SCR_KEYGRID) {
            _pickKeyForColor = false;
            pushScreen(SCR_KEYGRID, _kbd->activeLayer(), 0);
        } else if (v == SCR_PRESETS) {
            pushScreen(SCR_PRESETS, 0, 0);
        } else if (v == 100) {
            editValue("HOLD MS", _kbd->holdDurationThreshold(), 50, 2000, 50, false, VT_HOLD);
        } else if (v == 101) {
            GlobalSettings &g = _kbd->globals();
            g.hidLang = g.hidLang == HID_LABEL_RU ? HID_LABEL_EN : HID_LABEL_RU;
            _kbd->noteGlobalsChanged();
            _kbd->saveGlobals();
        } else if (v == -1) {
            popScreen(); // EXIT: back to the main menu
        }
        return MENU_NONE;
    case SCR_LAYERS:
        if (v == 100) {
            _kbd->setLayerCount(_kbd->layerCount() + 1);
            _kbd->saveKeymaps();
        } else if (v == 101) {
            _kbd->setLayerCount(_kbd->layerCount() - 1);
            _kbd->saveKeymaps();
        } else {
            pushScreen(SCR_LAYER_OPTS, v, 0);
        }
        return MENU_NONE;
    case SCR_LAYER_OPTS:
        if (result == 0) {
            _kbd->setBaseLayer(static_cast<uint8_t>(screen.param));
            _kbd->saveKeymaps();
            popScreen();
        } else if (result == 1) {
            pushScreen(SCR_KEYGRID, screen.param, 0);
        } else if (result == 2) {
            // Simple cyclic rename from common tokens (full text entry is
            // impractical on 7 keys); repeated presses cycle further.
            static const char *names[] = { "BASE", "NUMBERS", "SYMBOLS", "FN", "MEDIA",
                                           "GAMING", "NAV", "MACRO" };
            const char *current = _kbd->layerName(static_cast<uint8_t>(screen.param));
            uint8_t next = 0;
            for (uint8_t i = 0; i < 8; ++i) {
                if (strcmp(current, names[i]) == 0) { next = static_cast<uint8_t>((i + 1) % 8); break; }
                if (i == 7) next = 0;
            }
            _kbd->setLayerName(static_cast<uint8_t>(screen.param), names[next]);
            _kbd->saveKeymaps();
        }
        return MENU_NONE;
    case SCR_KEY_ACTION:
        _editLayer = static_cast<uint8_t>(screen.param2);
        _editKey = static_cast<uint8_t>(screen.param);
        if (v == 0) {
            _editCharPage = 0;
            replaceScreen(SCR_KEY_CHARS, _editKey, _editLayer);
        } else if (v == 1) {
            pushScreen(SCR_KEY_LAYER_TARGET, _editKey, _editLayer);
        } else if (v == 2) {
            _kbd->setKeyAction(_editLayer, _editKey, transparentAction());
            _kbd->saveKeymaps(_editLayer);
            popScreen();
            popScreen();
        } else {
            _kbd->setKeyAction(_editLayer, _editKey, hidAction(0, 0));
            _kbd->saveKeymaps(_editLayer);
            popScreen();
            popScreen();
        }
        return MENU_NONE;
    case SCR_KEY_CHARS: {
        // Grid of usages across pages.
        const uint8_t page = _editCharPage;
        const CharPage &cp = CHAR_PAGES[page];
        _grid.count = 0;
        _grid.cols = 6;
        _grid.cursor = 0;
        for (uint8_t i = 0; i < cp.count; ++i) {
            _grid.labels[_grid.count++] = hidKeyName(cp.usages[i], _kbd->globals().hidLang);
        }
        _grid.labels[_grid.count++] = ">PAGE";
        const int8_t gridResult = handleGrid(in);
        if (gridResult == MENU_BACK) { popScreen(); return MENU_NONE; }
        if (gridResult >= 0) {
            if (gridResult == _grid.count - 1) {
                _editCharPage = static_cast<uint8_t>((page + 1) % 4);
            } else {
                _editKey = static_cast<uint8_t>(screen.param);
                _editLayer = static_cast<uint8_t>(screen.param2);
                const KeyAction current = _kbd->keyAction(_editLayer, _editKey);
                _editMods = current.type == ACTION_HID ? current.modifiers : 0;
                _editTargetLayer = 0; // repurposed below: store the chosen usage
                _renameBuf[0] = static_cast<char>(CHAR_PAGES[page].usages[gridResult]);
                pushScreen(SCR_KEY_MODS, _editKey, _editLayer);
            }
        }
        return MENU_HANDLED;
    }
    case SCR_KEY_MODS: {
        if (v == 100) {
            const uint8_t usage = static_cast<uint8_t>(_renameBuf[0]);
            _kbd->setKeyAction(_editLayer, _editKey, hidAction(usage, _editMods));
            _kbd->saveKeymaps(_editLayer);
            while (_depth > 1) popScreen();
        } else {
            _editMods ^= static_cast<uint8_t>(1U << v);
        }
        return MENU_NONE;
    }
    case SCR_KEY_LAYER_TARGET:
        _editLayer = static_cast<uint8_t>(screen.param2);
        _editKey = static_cast<uint8_t>(screen.param);
        _editTargetLayer = static_cast<uint8_t>(v);
        pushScreen(SCR_KEY_LAYER_MODE, _editKey, _editLayer);
        return MENU_NONE;
    case SCR_KEY_LAYER_MODE: {
        const KeyAction action = layerAction(_editTargetLayer, static_cast<LayerSwitchMode>(v));
        _kbd->setKeyAction(_editLayer, _editKey, action);
        _kbd->saveKeymaps(_editLayer);
        while (_depth > 1) popScreen();
        return MENU_NONE;
    }
    case SCR_PRESETS:
        pushScreen(SCR_PRESET_OPTS, v, 0);
        return MENU_NONE;
    case SCR_PRESET_OPTS:
        if (v == 0) {
            _kbd->savePresetSlot(static_cast<uint8_t>(screen.param));
        } else {
            _kbd->loadPresetSlot(static_cast<uint8_t>(screen.param));
            _kbd->applyGlobalSettings();
        }
        popScreen();
        return MENU_NONE;
    case SCR_LED_MENU:
        pushScreen(static_cast<ScreenId>(v));
        return MENU_NONE;
    case SCR_LED_GENERAL: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        switch (result) {
        case 0: editValue("ENABLED 0/1", s.enabled, 0, 1, 1, false, VT_LED_ENABLE); break;
        case 1: editValue("BRIGHTNESS", s.brightness, 0, 255, 1, true, VT_BRIGHTNESS); break;
        case 2: editValue("EFFECT 0-4", s.effect, 0, 4, 1, false, VT_EFFECT); break;
        case 3: editValue("FX SPEED", s.effectSpeed, 1, 255, 1, false, VT_FX_SPEED); break;
        case 4: editValue("BACKLIGHT", s.backlightBrightness, 0, 255, 1, true, VT_BACKLIGHT); break;
        case 5: startColorPicker(CT_BACKLIGHT, 0, 0, CRGB(s.backlightR, s.backlightG, s.backlightB), 255); break;
        case 6: editValue("SLEEP ANIM 0-2", s.sleepAnimation, 0, 2, 1, false, VT_SLEEP_ANIM); break;
        case 7: editValue("SLEEP SEC", s.sleepTimeoutMs / 1000, 0, 3600, 5, false, VT_SLEEP_TIMEOUT); break;
        case 8: editValue("MIX 0-4", s.mixMode, 0, 4, 1, false, VT_MIX); break;
        }
        (void)line;
        return MENU_NONE;
    }
    case SCR_LED_LAYER_SEL:
        pushScreen(SCR_LED_LAYER, v, 0);
        return MENU_NONE;
    case SCR_LED_LAYER: {
        const LayerLedSettings s = _kbd->layerLed(static_cast<uint8_t>(screen.param));
        if (result == 0) {
            editValue("LAYER BRIGHT", s.brightness, 0, 255, 1, true, VT_LAYER_BRIGHT);
        } else if (result == 1) {
            editValue("EFFECT 0-4/255", s.effect, 0, 255, 1, false, VT_LAYER_EFFECT);
        } else {
            startColorPicker(CT_LAYER_COLOR, screen.param, 0,
                             CRGB(s.colorR, s.colorG, s.colorB), 255);
        }
        return MENU_NONE;
    }
    case SCR_LED_PERKEY:
        _pickKeyForColor = true;
        pushScreen(SCR_KEYGRID, _kbd->activeLayer(), 0);
        return MENU_NONE;
    case SCR_LED_FLASH: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        if (result == 0) {
            editValue("ENABLED 0/1", s.layerFlashEnabled, 0, 1, 1, false, VT_FLASH_ENABLE);
        } else if (result == 1) {
            startColorPicker(CT_FLASH_NEXT, 0, 0,
                             CRGB(s.nextLayerColorR, s.nextLayerColorG, s.nextLayerColorB), 255);
        } else if (result == 2) {
            startColorPicker(CT_FLASH_PREV, 0, 0,
                             CRGB(s.prevLayerColorR, s.prevLayerColorG, s.prevLayerColorB), 255);
        } else {
            editValue("FADE MS", s.layerFlashFadeMs, 100, 5000, 100, false, VT_FLASH_FADE);
        }
        return MENU_NONE;
    }
    case SCR_LED_KEYPRESS: {
        const LedGlobalSettings s = _kbd->led().captureSettings();
        if (result == 0) {
            editValue("ANIM 0-3", s.keypressAnimation, 0, 3, 1, false, VT_KEYPRESS_ANIM);
        } else {
            editValue("SPEED", s.keypressSpeed, 1, 255, 1, false, VT_KEYPRESS_SPEED);
        }
        return MENU_NONE;
    }
    case SCR_PALETTES:
        pushScreen(SCR_PALETTE_EDIT, v, 0);
        return MENU_NONE;
    case SCR_PALETTE_EDIT: {
        Palette &p = _kbd->palettes()[static_cast<uint8_t>(screen.param)];
        if (v == 100) {
            if (p.count < PALETTE_MAX_STOPS) {
                memcpy(p.color[p.count], p.color[p.count - 1], 3);
                ++p.count;
                _kbd->savePalettes();
            }
        } else if (v == 101) {
            if (p.count > 2) {
                --p.count;
                _kbd->savePalettes();
            }
        } else {
            startColorPicker(CT_PALETTE_STOP, screen.param, v,
                             CRGB(p.color[v][0], p.color[v][1], p.color[v][2]), 255);
        }
        return MENU_NONE;
    }
    case SCR_HID_MENU:
        if (v == SCR_MULTIKEY) {
            pushScreen(SCR_MULTIKEY);
        } else {
            const GlobalSettings &g = _kbd->globals();
            switch (result) {
            case 0: editValue("USB 0=HID 1=CFG", g.usbMode, 0, 1, 1, false, VT_USB_MODE); break;
            case 1: editValue("BT 0=HID 1=COM", g.btMode, 0, 1, 1, false, VT_BT_MODE); break;
            case 2: editValue("BT POWER 0-7", g.btTxPower, 0, 7, 1, false, VT_TXPOWER); break;
            case 3: editValue("ROLLOVER 0=6K 1=NKRO", g.rollover, 0, 1, 1, false, VT_ROLLOVER); break;
            }
        }
        return MENU_NONE;
    case SCR_MULTIKEY: {
        const GlobalSettings &g = _kbd->globals();
        switch (result) {
        case 0: editValue("ENABLED", g.multikeyEnabled, 0, 1, 1, false, VT_MK_ENABLE); break;
        case 1: editValue("KEYBOARD ID 0-5", g.multikeyId, 0, 5, 1, false, VT_MK_ID); break;
        case 2: editValue("SAVE SLAVE CFG", g.saveSlaveChanges, 0, 1, 1, false, VT_MK_SLAVE_SAVE); break;
        case 3: _kbd->link().pushConfig(); break;
        case 4: {
            const uint8_t slot = 0;
            _kbd->saveMultikeyConfigSlot(slot, "MK CFG");
            break;
        }
        }
        return MENU_NONE;
    }
    case SCR_OLED_MENU: {
        const GlobalSettings &g = _kbd->globals();
        switch (result) {
        case 0: editValue("SLEEP SEC", g.oledSleepTimeoutMs / 1000, 0, 3600, 5, false, VT_OLED_SLEEP); break;
        case 1: pushScreen(SCR_NAVKEYS); break;
        case 2: editValue("BOOT MENU", g.bootMenuEnabled, 0, 1, 1, false, VT_BOOT_MENU_ENABLE); break;
        case 3: editValue("BOOT MS (MIN 1000)", g.bootMenuTimeMs, 1000, 60000, 250, false, VT_BOOT_TIME); break;
        case 4: editValue("NAV LED BRIGHT", g.menuNavBrightness, 0, 255, 1, true, VT_NAV_BRIGHT); break;
        }
        return MENU_NONE;
    }
    case SCR_NAVKEYS: {
        // Wait for a physical key press and bind it.
        for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
            if (_kbd->justPressed(i)) {
                GlobalSettings &g = _kbd->globals();
                g.oledNavKeys[v] = i;
                _kbd->noteGlobalsChanged();
                _kbd->saveGlobals();
                break;
            }
        }
        return MENU_NONE;
    }
    case SCR_GAMES:
        _kbd->games().start(static_cast<uint8_t>(v), *_kbd);
        _state = STATE_GAME;
        return MENU_NONE;
    case SCR_CONFIRM:
        if (_confirmApply != nullptr) _confirmApply(v != 0, _confirmContext);
        _confirmApply = nullptr;
        popScreen();
        return MENU_NONE;
    default:
        return MENU_NONE;
    }
}
