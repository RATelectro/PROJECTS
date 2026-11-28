// ============================================================================
// WHACK-A-MOLE
//
// 30 seconds of chaos: a "mole" pops up on a random key (orange glow) and
// vanishes faster and faster as your score climbs. Hit it before it escapes:
// +1 point. Hit a dark key: -1 point (never below zero). Occasionally a
// golden "bonus mole" appears and is worth 3 points.
//
// Controls:  ENTER     - start the 30 s round
//            any key   - whack (mole or miss)
// LEDs:      orange mole, gold bonus mole, everything else dark.
// Display:   title, countdown, live score, final score + best of session.
//
// Estimated flash cost: ~1.5 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_WHACK_A_MOLE

namespace {

class WhackAMoleGame : public Game {
public:
    const char *name() const override { return "WHACK-A-MOLE"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _running = false;
        _mole = 255;
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        if (!_running) {
            if (in.enter) {
                _running = true;
                _score = 0;
                _roundStartMs = nowMs;
                spawn(nowMs);
            }
            return;
        }

        if (static_cast<uint32_t>(nowMs - _roundStartMs) >= ROUND_MS) {
            _running = false;
            _mole = 255;
            if (_score > _best) _best = _score;
            return;
        }

        if (in.pressedBody) {
            const uint8_t hit = in.bodyRow * Config::COLS + in.bodyCol;
            if (hit == _mole) {
                _score += _bonus ? 3 : 1;
                spawn(nowMs);
            } else if (_score > 0) {
                --_score;
            }
        } else if (_mole != 255 && nowMs >= _moleUntilMs) {
            spawn(nowMs); // escaped
        }

        if (_mole != 255) {
            const CRGB color = _bonus ? CRGB(255, 190, 0) : CRGB(255, 90, 0);
            _kbd->led().setOverlayColor(_mole, color);
        }
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "WHACK-A-MOLE", true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        char line[22];
        if (_running) {
            const uint32_t left = (ROUND_MS - (millis() - _roundStartMs)) / 1000;
            snprintf(line, sizeof(line), "TIME %lu  SCORE %u", (unsigned long)left, _score);
            display.drawText(1, 20, line, true);
            display.drawText(1, 40, "HIT THE MOLES!", true);
        } else {
            snprintf(line, sizeof(line), "LAST %u  BEST %u", _score, _best);
            display.drawText(1, 20, line, true);
            display.drawText(1, 40, "ENTER - START", true);
        }
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    static constexpr uint32_t ROUND_MS = 30000;

    void spawn(uint32_t nowMs) {
        _mole = static_cast<uint8_t>(random(0, Config::NUM_KEYS));
        _bonus = random(0, 8) == 0; // ~12% golden mole
        // Moles get quicker as the score climbs: 900 ms down to 380 ms.
        const uint16_t base = 900;
        const uint16_t drop = _score * 12;
        _moleUntilMs = nowMs + (drop > 520 ? 380 : base - drop);
    }

    MultiKey *_kbd = nullptr;
    bool _running = false;
    uint8_t _mole = 255;
    bool _bonus = false;
    uint16_t _score = 0;
    uint16_t _best = 0;
    uint32_t _roundStartMs = 0;
    uint32_t _moleUntilMs = 0;
    bool _finished = false;
};

WhackAMoleGame whackAMoleGame;

} // namespace

Game *whackAMoleGameInstance() { return &whackAMoleGame; }

#endif // GAME_WHACK_A_MOLE
