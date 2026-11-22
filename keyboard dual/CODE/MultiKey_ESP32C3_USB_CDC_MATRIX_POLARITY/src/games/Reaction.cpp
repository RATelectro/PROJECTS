// ============================================================================
// REACTION
//
// Reflex trainer. The whole matrix glows red: WAIT. After a random delay
// (1..3.5 s) it flips to green and the clock starts; hit any key as fast as
// you can. Pressing too early is a fault. The reaction time in milliseconds
// and your session best are shown on the display.
//
// Controls:  ENTER - start a round
//            any key (while green) - stop the clock
//            any key (while red)   - false start, try again
// LEDs:      all keys red while waiting, all green when it's time, cyan
//            flash showing the result.
// Display:   title, state prompt, last time, best time.
//
// Estimated flash cost: ~1.5 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_REACTION

namespace {

class ReactionGame : public Game {
public:
    const char *name() const override { return "REACTION"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _phase = PHASE_IDLE;
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        switch (_phase) {
        case PHASE_IDLE:
            if (in.enter) {
                _phase = PHASE_WAIT;
                _flipAtMs = nowMs + random(1000, 3500);
            }
            break;
        case PHASE_WAIT:
            if (in.pressedBody || in.enter) { // false start
                _phase = PHASE_FAULT;
            } else if (nowMs >= _flipAtMs) {
                _phase = PHASE_GO;
                _goAtMs = nowMs;
            }
            break;
        case PHASE_GO:
            if (in.pressedBody || in.enter) {
                _lastMs = nowMs - _goAtMs;
                if (_bestMs == 0 || _lastMs < _bestMs) _bestMs = _lastMs;
                _phase = PHASE_RESULT;
            }
            break;
        case PHASE_FAULT:
        case PHASE_RESULT:
            if (in.enter) _phase = PHASE_IDLE;
            break;
        }

        if (_phase == PHASE_WAIT) paintAll(CRGB(200, 0, 0));
        else if (_phase == PHASE_GO) paintAll(CRGB(0, 220, 0));
        else if (_phase == PHASE_RESULT && ((nowMs / 200) & 1) == 0) paintAll(CRGB(0, 160, 220));
        else if (_phase == PHASE_FAULT) paintAll(CRGB(200, 0, 160));
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "REACTION", true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        char line[24];
        switch (_phase) {
        case PHASE_IDLE:
            display.drawText(1, 20, "ENTER - START", true);
            break;
        case PHASE_WAIT:
            display.drawText(1, 20, "WAIT FOR GREEN", true);
            break;
        case PHASE_GO:
            display.drawText(1, 20, "HIT ANY KEY!", true);
            break;
        case PHASE_FAULT:
            display.drawText(1, 20, "TOO EARLY!", true);
            display.drawText(1, 40, "ENTER - BACK", true);
            break;
        case PHASE_RESULT:
            snprintf(line, sizeof(line), "TIME %lu MS", (unsigned long)_lastMs);
            display.drawText(1, 18, line, true);
            snprintf(line, sizeof(line), "BEST %lu MS", (unsigned long)_bestMs);
            display.drawText(1, 30, line, true);
            display.drawText(1, 46, "ENTER - AGAIN", true);
            break;
        }
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    enum Phase : uint8_t { PHASE_IDLE, PHASE_WAIT, PHASE_GO, PHASE_RESULT, PHASE_FAULT };

    void paintAll(const CRGB &color) {
        for (uint8_t i = 0; i < Config::NUM_KEYS; ++i) {
            _kbd->led().setOverlayColor(i, color);
        }
    }

    MultiKey *_kbd = nullptr;
    Phase _phase = PHASE_IDLE;
    uint32_t _flipAtMs = 0;
    uint32_t _goAtMs = 0;
    uint32_t _lastMs = 0;
    uint32_t _bestMs = 0;
    bool _finished = false;
};

ReactionGame reactionGame;

} // namespace

Game *reactionGameInstance() { return &reactionGame; }

#endif // GAME_REACTION
