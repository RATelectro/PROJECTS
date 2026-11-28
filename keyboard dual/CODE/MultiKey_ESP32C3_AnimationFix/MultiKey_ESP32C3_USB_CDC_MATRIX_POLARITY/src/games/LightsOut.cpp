// ============================================================================
// LIGHTS OUT
//
// Classic Lights-Out puzzle on the 7x5 key matrix. Pressing a key toggles it
// and its four orthogonal neighbours; pressing an edge nav strip toggles the
// whole row or column. The board is scrambled from the solved state, so every
// puzzle is guaranteed solvable. Goal: turn all lights off in as few moves
// as possible.
//
// Controls:  any key press - toggle it + neighbours (Lights-Out style)
//            UP/DOWN       - toggle top / bottom row
//            LEFT/RIGHT    - toggle left / right column
//            ENTER         - new scrambled board
// LEDs:      lit cells glow cyan, dark cells are off.
// Display:   title, move counter, SOLVED banner.
//
// Estimated flash cost: ~1.5 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_LIGHTS_OUT

namespace {

class LightsOutGame : public Game {
public:
    const char *name() const override { return "LIGHTS OUT"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _moves = 0;
        memset(_board, 0, sizeof(_board));
        // Scramble from the solved state by random presses (always solvable).
        for (uint8_t i = 0; i < 12; ++i) {
            toggle(random(0, Config::ROWS), random(0, Config::COLS));
        }
        _moves = 0;
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        (void)nowMs;
        if (in.enter) {
            begin(*_kbd);
            return;
        }
        int8_t row = -1;
        int8_t col = -1;
        if (in.up) row = 0;
        if (in.down) row = Config::ROWS - 1;
        if (in.left) col = 0;
        if (in.right) col = Config::COLS - 1;
        if (in.enterHeld) { begin(*_kbd); return; }
        // Any edge key press toggles its row/column strip; a body press
        // toggles the 3x3 neighbourhood, Lights-Out style.
        if (row < 0 && col < 0) {
            for (uint8_t r = 1; r + 1 < Config::ROWS; ++r) {
                for (uint8_t c = 1; c + 1 < Config::COLS; ++c) {
                    if (in.pressedBody && in.bodyRow == r && in.bodyCol == c) {
                        toggle(r, c);
                        toggle(r - 1, c);
                        toggle(r + 1, c);
                        toggle(r, c - 1);
                        toggle(r, c + 1);
                        ++_moves;
                        return;
                    }
                }
            }
            return;
        }
        if (row >= 0) {
            for (uint8_t c = 0; c < Config::COLS; ++c) toggle(row, c);
            ++_moves;
        }
        if (col >= 0) {
            for (uint8_t r = 0; r < Config::ROWS; ++r) toggle(r, col);
            ++_moves;
        }
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "LIGHTS OUT", true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        char line[20];
        snprintf(line, sizeof(line), "MOVES %u", _moves);
        display.drawText(1, 14, line, true);
        bool solved = true;
        for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
            if (_board[i]) { solved = false; break; }
        }
        if (solved) {
            display.drawText(1, 30, "SOLVED!", true);
        }
        display.drawText(1, 52, "KEYS - TOGGLE", true);
        display.drawText(80, 52, "ENT-NEW", true);
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    void toggle(uint8_t row, uint8_t col) {
        const uint8_t index = row * Config::COLS + col;
        _board[index] = !_board[index];
        _kbd->led().setOverlayColor(index, _board[index] ? CRGB(0, 200, 255) : CRGB(0, 0, 0));
    }

    MultiKey *_kbd = nullptr;
    bool _board[Config::NUM_KEYS] = {};
    uint16_t _moves = 0;
    bool _finished = false;
};

LightsOutGame lightsOutGame;

} // namespace

Game *lightsOutGameInstance() { return &lightsOutGame; }

#endif // GAME_LIGHTS_OUT
