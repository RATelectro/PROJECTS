// ============================================================================
// SIMON
//
// Memory game: the 7x5 matrix is split into four coloured zones (green, red,
// yellow, blue). The game flashes a growing sequence of zones; repeat it by
// pressing any key inside the lit zone. One mistake ends the run. The best
// streak is kept for the session.
//
// Controls:  ENTER        - start the game
//            any zone key - repeat the shown sequence
// LEDs:      whole zones light up in their colour while flashing/pressed.
// Display:   title, round counter, WATCH/REPEAT prompts, best streak.
//
// Estimated flash cost: ~2 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_SIMON

namespace {

class SimonGame : public Game {
public:
    const char *name() const override { return "SIMON"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _round = 0;
        _phase = PHASE_IDLE;
        memset(_sequence, 0, sizeof(_sequence));
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        if (_phase == PHASE_IDLE) {
            if (in.enter) startRound();
            return;
        }
        if (_phase == PHASE_SHOW) {
            if (static_cast<uint32_t>(nowMs - _stepStartedMs) < (_showStep ? 320 : 160)) return;
            if (_showStep) {
                lightZone(_sequence[_showIndex], false);
                _showStep = false;
            } else {
                ++_showIndex;
                _showStep = true;
                if (_showIndex > _round) {
                    _phase = PHASE_INPUT;
                    _inputIndex = 0;
                } else {
                    lightZone(_sequence[_showIndex], true);
                }
            }
            _stepStartedMs = nowMs;
            return;
        }
        if (_phase == PHASE_INPUT) {
            uint8_t zone = 255;
            if (in.pressedBody) zone = zoneOf(in.bodyRow, in.bodyCol);
            if (zone != 255) {
                lightZone(zone, true);
                _kbd->led().show();
                if (zone == _sequence[_inputIndex]) {
                    ++_inputIndex;
                    if (_inputIndex > _round) startRound();
                } else {
                    _phase = PHASE_IDLE;
                }
            }
        }
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "SIMON", true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        char line[20];
        if (_phase == PHASE_IDLE) {
            snprintf(line, sizeof(line), "BEST %u", _best);
            display.drawText(1, 20, line, true);
            display.drawText(1, 40, "ENTER - START", true);
        } else if (_phase == PHASE_SHOW) {
            snprintf(line, sizeof(line), "ROUND %u", _round + 1);
            display.drawText(1, 20, line, true);
            display.drawText(1, 34, "WATCH...", true);
        } else {
            snprintf(line, sizeof(line), "ROUND %u", _round + 1);
            display.drawText(1, 20, line, true);
            display.drawText(1, 34, "REPEAT", true);
        }
        display.drawText(1, 52, "ZONES: KEY PAD", true);
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    enum Phase : uint8_t { PHASE_IDLE, PHASE_SHOW, PHASE_INPUT };

    static uint8_t zoneOf(uint8_t row, uint8_t col) {
        const bool top = row < (Config::ROWS + 1) / 2;
        const bool left = col < (Config::COLS + 1) / 2;
        if (top && left) return 0;
        if (top && !left) return 1;
        if (!top && left) return 2;
        return 3;
    }

    void lightZone(uint8_t zone, bool on) {
        static const CRGB colors[4] = { CRGB(0, 200, 0), CRGB(255, 0, 0), CRGB(255, 200, 0), CRGB(0, 60, 255) };
        for (uint8_t r = 0; r < Config::ROWS; ++r) {
            for (uint8_t c = 0; c < Config::COLS; ++c) {
                if (zoneOf(r, c) == zone) {
                    _kbd->led().setOverlayColor(r * Config::COLS + c, on ? colors[zone] : CRGB(0, 0, 0));
                }
            }
        }
    }

    void startRound() {
        if (_phase == PHASE_INPUT && _round + 1 > _best) _best = _round + 1;
        _sequence[_round] = random(0, 4);
        _round = _round < 31 ? _round + 1 : 31;
        _phase = PHASE_SHOW;
        _showIndex = 0;
        _showStep = true;
        _stepStartedMs = millis();
        lightZone(_sequence[0], true);
    }

    MultiKey *_kbd = nullptr;
    uint8_t _sequence[32] = {};
    uint8_t _round = 0;
    uint8_t _best = 0;
    Phase _phase = PHASE_IDLE;
    uint8_t _showIndex = 0;
    bool _showStep = true;
    uint8_t _inputIndex = 0;
    uint32_t _stepStartedMs = 0;
    bool _finished = false;
};

SimonGame simonGame;

} // namespace

Game *simonGameInstance() { return &simonGame; }

#endif // GAME_SIMON
