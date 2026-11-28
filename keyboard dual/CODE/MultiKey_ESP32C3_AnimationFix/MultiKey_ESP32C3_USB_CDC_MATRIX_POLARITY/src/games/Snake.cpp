// ============================================================================
// SNAKE
//
// The classic: steer a growing snake across the 7x5 key matrix and eat the
// blinking food. Each meal makes the snake longer and the game faster.
// Hitting a wall or the snake's own body ends the run.
//
// Controls:  UP / DOWN / LEFT / RIGHT  - steer
//            ENTER - restart after game over
//            LEFT  - (when a menu exit is requested) back to the games menu
// LEDs:      the snake is a green gradient (bright head), food blinks red.
// Display:   title, live score, game-over banner.
//
// Estimated flash cost: ~2.5 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_SNAKE

namespace {

class SnakeGame : public Game {
public:
    const char *name() const override { return "SNAKE"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _length = 3;
        _dir = 1; // right
        _pendingDir = _dir;
        const uint8_t cx = Config::COLS / 2;
        const uint8_t cy = Config::ROWS / 2;
        for (uint8_t i = 0; i < _length; ++i) {
            _body[i] = static_cast<uint8_t>((cy) * Config::COLS + (cx - i));
        }
        _score = 0;
        _tickMs = 350;
        _lastTickMs = millis();
        _over = false;
        placeFood();
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        if (in.up && _dir != 2) _pendingDir = 0;
        if (in.down && _dir != 0) _pendingDir = 2;
        if (in.left && _dir != 1) _pendingDir = 3;
        if (in.right && _dir != 3) _pendingDir = 1;
        if (_over) {
            if (in.enter) begin(*_kbd);
            return;
        }
        if (static_cast<uint32_t>(nowMs - _lastTickMs) < _tickMs) return;
        _lastTickMs = nowMs;
        _dir = _pendingDir;

        const uint8_t head = _body[0];
        int8_t row = head / Config::COLS;
        int8_t col = head % Config::COLS;
        if (_dir == 0) --row;
        if (_dir == 2) ++row;
        if (_dir == 3) --col;
        if (_dir == 1) ++col;
        if (row < 0 || col < 0 || row >= Config::ROWS || col >= Config::COLS) {
            _over = true;
            return;
        }
        const uint8_t next = static_cast<uint8_t>(row * Config::COLS + col);
        for (uint8_t i = 0; i < _length; ++i) {
            if (_body[i] == next && !(i == _length - 1 && next != _food)) {
                _over = true;
                return;
            }
        }

        const bool eats = next == _food;
        if (!eats) {
            for (uint8_t i = _length; i > 0; --i) _body[i] = _body[i - 1];
        } else {
            if (_length < Config::NUM_KEYS - 1) ++_length;
            for (uint8_t i = _length - 1; i > 0; --i) _body[i] = _body[i - 1];
            ++_score;
            _tickMs = _tickMs > 130 ? _tickMs - 8 : 130;
            placeFood();
        }
        _body[0] = next;

        // LED overlay: body as a green gradient, head bright, food blinking.
        for (uint8_t i = 0; i < _length; ++i) {
            const uint8_t v = i == 0 ? 255 : 200 - i * 12;
            _kbd->led().setOverlayColor(_body[i], CRGB(0, v, 40));
        }
        if (((nowMs / 250) & 1) == 0) _kbd->led().setOverlayColor(_food, CRGB(255, 0, 0));
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "SNAKE", true);
        char line[20];
        snprintf(line, sizeof(line), "SCORE %u", _score);
        display.drawText(40, 0, line, true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        if (_over) {
            display.drawText(1, 24, "GAME OVER", true);
            display.drawText(1, 40, "ENTER - RETRY", true);
            display.drawText(1, 50, "LEFT - EXIT", true);
        } else {
            display.drawText(1, 52, "ARROWS - STEER", true);
        }
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    void placeFood() {
        while (true) {
            const uint8_t candidate = random(0, Config::NUM_KEYS);
            bool onSnake = false;
            for (uint8_t i = 0; i < _length; ++i) {
                if (_body[i] == candidate) { onSnake = true; break; }
            }
            if (!onSnake) {
                _food = candidate;
                return;
            }
        }
    }

    MultiKey *_kbd = nullptr;
    uint8_t _body[Config::NUM_KEYS] = {};
    uint8_t _length = 3;
    uint8_t _dir = 1;
    uint8_t _pendingDir = 1;
    uint8_t _food = 0;
    uint16_t _score = 0;
    uint16_t _tickMs = 350;
    uint32_t _lastTickMs = 0;
    bool _over = false;
    bool _finished = false;
};

SnakeGame snakeGame;

} // namespace

Game *snakeGameInstance() { return &snakeGame; }

#endif // GAME_SNAKE
