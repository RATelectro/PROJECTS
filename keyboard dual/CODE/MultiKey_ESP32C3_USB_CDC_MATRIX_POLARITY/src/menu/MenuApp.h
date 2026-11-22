#pragma once

#include <Arduino.h>
#include <FastLED.h>
#include "../../KeyboardConfig.h"
#include "../../Settings.h"
#include "../../KeyAction.h"
#include "Menu.h"

class MultiKey;
class Ssd1306;
class Game;

// On-device menu application: boot chooser, configuration menus, key editor,
// colour picker and the games launcher. While active() is true the keyboard
// suppresses HID output and routes keys to the UI.
class MenuApp {
public:
    void begin(MultiKey &keyboard);
    void update(uint32_t nowMs);

    bool active() const { return _state != STATE_IDLE; }
    // Enter the boot chooser / menu tree. After the main menu's EXIT item has
    // been used, open() refuses until reboot; force=true (CLI) overrides.
    void open(bool force = false);
    void close();     // Immediately return to keyboard mode.
    void startGame(uint8_t index); // Start a game directly (also used by CLI).

    enum State : uint8_t {
        STATE_IDLE = 0,
        STATE_BOOT_CHOOSER,
        STATE_SCREEN,
        STATE_TRANSITION,
        STATE_VALUE_EDIT,
        STATE_COLOR_PICKER,
        STATE_GAME,
    };

    enum ScreenId : uint8_t {
        SCR_MAIN = 0,
        SCR_KB_MENU,
        SCR_LAYERS,
        SCR_LAYER_OPTS,
        SCR_RENAME_LAYER,
        SCR_KEYGRID,
        SCR_KEY_ACTION,
        SCR_KEY_CHARS,
        SCR_KEY_MODS,
        SCR_KEY_LAYER_TARGET,
        SCR_KEY_LAYER_MODE,
        SCR_PRESETS,
        SCR_PRESET_OPTS,
        SCR_LED_MENU,
        SCR_LED_GENERAL,
        SCR_LED_LAYER_SEL,
        SCR_LED_LAYER,
        SCR_LED_PERKEY,
        SCR_LED_FLASH,
        SCR_LED_KEYPRESS,
        SCR_PALETTES,
        SCR_PALETTE_EDIT,
        SCR_HID_MENU,
        SCR_MULTIKEY,
        SCR_OLED_MENU,
        SCR_NAVKEYS,
        SCR_GAMES,
        SCR_CONFIRM,
    };

    struct Screen {
        ScreenId id;
        int16_t param;
        int16_t param2;
        uint8_t selected = 0; // list cursor, persists while the screen is stacked
    };

private:
    // --- helpers -----------------------------------------------------------
    MenuInput readInput(uint32_t nowMs);
    void setOverlay(uint32_t nowMs); // nav-key LED overlay
    void pushScreen(ScreenId id, int16_t param = 0, int16_t param2 = 0);
    void replaceScreen(ScreenId id, int16_t param = 0, int16_t param2 = 0);
    void popScreen();
    void startTransition(bool forward);
    void drawFrame(Ssd1306 &display, int16_t xOff, uint32_t nowMs);
    void drawScreen(Screen &screen, Ssd1306 &display, int16_t xOff, uint32_t nowMs);

    // --- screen handlers ---------------------------------------------------
    void handleScreen(const MenuInput &in, uint32_t nowMs);
    void buildList(Screen &screen, MenuList &list);
    int8_t handleList(Screen &screen, const MenuInput &in, MenuList &list);
    const char *confirmText() const;

    // --- specific screens --------------------------------------------------
    void drawKeyGrid(Ssd1306 &display, int16_t xOff, uint8_t layer);
    void drawColorPicker(Ssd1306 &display, int16_t xOff, uint32_t nowMs);
    void handleColorPicker(const MenuInput &in, uint32_t nowMs);
    void drawValueEdit(Ssd1306 &display, int16_t xOff);
    void handleValueEdit(const MenuInput &in);
    void drawBootChooser(Ssd1306 &display, uint32_t nowMs);
    void handleBootChooser(const MenuInput &in, uint32_t nowMs);
    void applyPickedColor();

    MultiKey *_kbd = nullptr;
    State _state = STATE_IDLE;
    Screen _stack[8];
    uint8_t _depth = 0;
    uint32_t _stateEnteredMs = 0;
    uint32_t _lastActivityMs = 0;

    // Transition (slide) animation.
    bool _transitionForward = true;
    uint32_t _transitionStartedMs = 0;
    Screen _transitionTarget;

    // Value editor.
    enum ValueTarget : uint8_t {
        VT_NONE = 0, VT_BRIGHTNESS, VT_FX_SPEED, VT_KEYPRESS_SPEED, VT_SLEEP_TIMEOUT,
        VT_HOLD, VT_FLASH_FADE, VT_BACKLIGHT, VT_LAYER_BRIGHT, VT_EFFECT,
        VT_KEYPRESS_ANIM, VT_SLEEP_ANIM, VT_MIX, VT_USB_MODE, VT_BT_MODE, VT_TXPOWER,
        VT_ROLLOVER, VT_OLED_SLEEP, VT_BOOT_TIME, VT_NAV_BRIGHT, VT_MK_ID,
        VT_LAYER_EFFECT, VT_MK_ENABLE, VT_MK_SLAVE_SAVE, VT_BOOT_MENU_ENABLE,
        VT_LED_ENABLE, VT_FLASH_ENABLE
    };
    void editValue(const char *title, uint16_t current, uint16_t min, uint16_t max,
                   uint16_t step, bool brightness, uint8_t target);
    void applyValue(uint16_t value);
    const char *_valueTitle = nullptr;
    uint8_t _valueTarget = VT_NONE;
    uint16_t _valueMin = 0;
    uint16_t _valueMax = 255;
    uint16_t _valueStep = 1;
    uint16_t _valueWorking = 0;
    bool _valueBrightness = false;      // enables the nonlinear brightness palette
    uint8_t _valueBrightnessRange = 0;  // 0: direct, 1: 0-50, 2: 50-100, 3: 100-255
    uint32_t _brightnessPaletteEnteredMs = 0;

    // Colour picker.
    enum ColorTarget : uint8_t {
        CT_NONE = 0, CT_BACKLIGHT, CT_FLASH_NEXT, CT_FLASH_PREV, CT_LAYER_COLOR,
        CT_PERKEY, CT_PALETTE_STOP
    };
    void startColorPicker(uint8_t target, int16_t param, int16_t param2,
                          CRGB initial, uint8_t previewKey);
    void applyColor(CRGB color, uint8_t brightness);
    uint8_t _colorTarget = CT_NONE;
    int16_t _colorParam = 0;
    int16_t _colorParam2 = 0;
    uint8_t _pickMode = 0;    // 0 palette, 1 hue, 2 R, 3 G, 4 B, 5 depth
    uint8_t _pickPosition = 128;
    uint8_t _pickPalette = 0;
    CRGB _pickColor = CRGB::White;
    uint8_t _pickBrightness = 255;
    bool _pickPaletteMode = false; // inside palette mode (hold-to-exit)
    uint32_t _pickHoldSinceMs = 0;
    uint8_t _pickTargetKey = 0;    // key index to preview (255 = whole matrix)

    // Confirm dialog.
    void (*_confirmApply)(bool, void *) = nullptr;
    void *_confirmContext = nullptr;
    const char *_confirmMessage = nullptr;

    // Generic full-screen grid picker (character / layer grids).
    struct Grid {
        const char *labels[40];
        uint8_t count = 0;
        uint8_t cols = 6;
        uint8_t cursor = 0;
        uint8_t page = 0;
    } _grid;
    void drawGrid(Ssd1306 &display, int16_t xOff, const char *title);
    int8_t handleGrid(const MenuInput &in);

    // Key editor state.
    uint8_t _editLayer = 0;
    uint8_t _editKey = 0;
    uint8_t _editCharPage = 0;
    uint8_t _editMods = 0;
    uint8_t _editTargetLayer = 0;
    bool _pickKeyForColor = false;
    char _renameBuf[Config::LAYER_NAME_LENGTH] = {};

    // Boot chooser / main-menu exit.
    uint32_t _bootFlashStartedMs = 0; // 0 = not flashing
    bool _exited = false;             // EXIT used: menu only reopens after reboot
};
