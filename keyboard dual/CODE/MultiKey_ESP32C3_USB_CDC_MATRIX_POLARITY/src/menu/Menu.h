#pragma once

#include <Arduino.h>
#include "../../KeyboardConfig.h"
#include "../display/Ssd1306.h"

// Edge-triggered menu input for one update tick.
struct MenuInput {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool enter = false;
    bool upHeld = false;   // held past the long-press threshold
    bool downHeld = false;
    bool enterHeld = false;
    bool anyHeld = false;  // any key currently down
    // Non-nav key press (used by games for playfield input).
    bool pressedBody = false;
    uint8_t bodyRow = 0;
    uint8_t bodyCol = 0;
};

constexpr int8_t MENU_NONE = -1;
constexpr int8_t MENU_BACK = -2; // left pressed on the top level of a screen
constexpr int8_t MENU_HANDLED = -3; // consumed by a custom control

struct MenuItem {
    char label[22];
    int16_t value;
};

constexpr uint8_t MENU_MAX_ITEMS = 16;
constexpr uint8_t MENU_VISIBLE_ROWS = 7; // 64 px - 8 px title bar, 8 px per row

// Scrollable vertical list. The selected row is marked with a ">" arrow
// cursor at the left edge (colour-independent, unlike the fixed yellow band
// of two-colour panels); all items keep normal (non-inverted) text.
class MenuList {
public:
    void begin(const char *title) {
        _title = title;
        _count = 0;
        _top = 0;
        // _selected is deliberately NOT reset here: the menu app rebuilds the
        // list every update tick and restores the cursor via select().
    }

    int8_t addItem(const char *label, int16_t value = 0) {
        if (_count >= MENU_MAX_ITEMS) return -1;
        strncpy(_items[_count].label, label, sizeof(_items[0].label) - 1);
        _items[_count].label[sizeof(_items[0].label) - 1] = '\0';
        _items[_count].value = value;
        return static_cast<int8_t>(_count++);
    }

    void setLabel(uint8_t index, const char *label) {
        if (index >= _count) return;
        strncpy(_items[index].label, label, sizeof(_items[0].label) - 1);
        _items[index].label[sizeof(_items[0].label) - 1] = '\0';
    }

    // Returns the activated item index on enter, MENU_BACK on left,
    // MENU_NONE otherwise.
    int8_t handleInput(const MenuInput &in) {
        if (in.up && _selected > 0) --_selected;
        if (in.down && _selected + 1 < _count) ++_selected;
        if (_selected < _top) _top = _selected;
        if (_selected >= _top + MENU_VISIBLE_ROWS) _top = _selected - MENU_VISIBLE_ROWS + 1;
        if (in.enter && _count > 0) return _selected;
        if (in.left) return MENU_BACK;
        return MENU_NONE;
    }

    void render(Ssd1306 &d, int16_t xOff, uint32_t nowMs, bool showTitle = true) const {
        const uint8_t titleH = showTitle ? 9 : 0;
        for (uint8_t row = 0; row < MENU_VISIBLE_ROWS && _top + row < _count; ++row) {
            const uint8_t index = _top + row;
            const int16_t y = static_cast<int16_t>(titleH + row * 8);
            const bool isSelected = index == _selected;
            const char *label = _items[index].label;
            const uint8_t labelWidth = d.textWidth(label);

            // Arrow cursor for the selected row; blink it gently.
            if (isSelected && ((nowMs / 400) & 1) == 0) {
                d.drawText(xOff, y + 1, ">", true);
            }

            // Long labels scroll back and forth so they stay readable.
            const int16_t textLeft = xOff + 7; // room for the arrow
            const int16_t textRight = Config::OLED_WIDTH - 2;
            int16_t textX = textLeft;
            if (labelWidth > textRight - textLeft) {
                const uint16_t span = labelWidth - (textRight - textLeft);
                const uint32_t phase = (nowMs / 3) % (2UL * span + 40);
                const int16_t shift = phase <= span ? static_cast<int16_t>(phase)
                                      : phase <= span + 40 ? static_cast<int16_t>(span)
                                                           : static_cast<int16_t>(2 * span + 40 - phase);
                textX = textLeft - shift;
            }
            d.drawText(textX, y + 1, label, true);
        }
        if (showTitle && _title != nullptr) {
            d.drawText(xOff + 1, 0, _title, true);
            d.hline(xOff, 8, Config::OLED_WIDTH, true);
        }
    }

    uint8_t count() const { return _count; }
    uint8_t selected() const { return _selected; }
    void select(uint8_t index) {
        if (index >= _count) return;
        _selected = index;
        if (_selected < _top) _top = _selected;
        if (_selected >= _top + MENU_VISIBLE_ROWS) _top = _selected - MENU_VISIBLE_ROWS + 1;
    }
    int16_t value(uint8_t index) const { return _items[index].value; }
    const char *label(uint8_t index) const { return _items[index].label; }

private:
    MenuItem _items[MENU_MAX_ITEMS];
    const char *_title = nullptr;
    uint8_t _count = 0;
    uint8_t _selected = 0;
    uint8_t _top = 0;
};
