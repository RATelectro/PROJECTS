// ============================================================================
// PONG
//
// One-player Pong against the keyboard itself. Your paddle is the bottom row
// of keys, the AI defends the top row. The ball speeds up slightly after
// every paddle hit; first to 5 points wins. A point is scored when the ball
// leaves the field on the opponent's side.
//
// Controls:  LEFT / RIGHT - move your paddle
//            ENTER        - serve / restart
// LEDs:      your paddle green, AI paddle red, ball white with a fading
//            orange trail.
// Display:   title, score (you : AI), serve prompt, win/lose banner.
//
// Estimated flash cost: ~2.5 KB program space (with FastLED already linked).
// ============================================================================

#include "Games.h"
#include "../../Keyboard.h"
#include "../display/Ssd1306.h"

#if GAME_PONG

namespace {

class PongGame : public Game {
public:
    const char *name() const override { return "PONG"; }

    void begin(MultiKey &keyboard) override {
        _kbd = &keyboard;
        _finished = false;
        _player = Config::COLS / 2;
        _ai = Config::COLS / 2;
        _playerScore = 0;
        _aiScore = 0;
        _over = false;
        _serving = true;
        _ballCol = _player;
        _ballRow = Config::ROWS - 2;
        _velRow = -1;
        _velCol = 1;
        _tickMs = 200;
        _lastTickMs = millis();
        _trailValid = false;
    }

    void update(const MenuInput &in, uint32_t nowMs) override {
        if (in.left && _player > 0) --_player;
        if (in.right && _player + PADDLE_W < Config::COLS) ++_player;
        if (in.enter) {
            if (_over) begin(*_kbd);
            else if (_serving) serve();
        }
        if (_serving || _over) { drawField(nowMs); return; }
        if (static_cast<uint32_t>(nowMs - _lastTickMs) < _tickMs) { drawField(nowMs); return; }
        _lastTickMs = nowMs;

        // AI follows the ball with one step per tick.
        if (_ai + PADDLE_W / 2 < _ballCol && _ai + PADDLE_W < Config::COLS) ++_ai;
        if (_ai + PADDLE_W / 2 > _ballCol && _ai > 0) --_ai;

        int8_t row = _ballRow + _velRow;
        int8_t col = _ballCol + _velCol;
        if (col < 0) { col = 0; _velCol = -_velCol; }
        if (col >= Config::COLS) { col = Config::COLS - 1; _velCol = -_velCol; }

        if (row < 0) { // AI side
            if (col >= _ai && col < _ai + PADDLE_W) {
                row = 0;
                _velRow = 1;
                speedUp();
            } else {
                ++_playerScore;
                afterPoint();
                return;
            }
        } else if (row >= Config::ROWS) { // player side
            if (col >= _player && col < _player + PADDLE_W) {
                row = Config::ROWS - 1;
                _velRow = -1;
                speedUp();
            } else {
                ++_aiScore;
                afterPoint();
                return;
            }
        }

        _trailRow = _ballRow;
        _trailCol = _ballCol;
        _trailValid = true;
        _ballRow = static_cast<uint8_t>(row);
        _ballCol = static_cast<uint8_t>(col);
        drawField(nowMs);
    }

    void render(Ssd1306 &display) override {
        display.drawText(1, 0, "PONG", true);
        char line[20];
        snprintf(line, sizeof(line), "%u : %u", _playerScore, _aiScore);
        display.drawText(50, 0, line, true);
        display.hline(0, 8, Config::OLED_WIDTH, true);
        if (_over) {
            display.drawText(1, 24, _playerScore > _aiScore ? "YOU WIN!" : "AI WINS", true);
            display.drawText(1, 40, "ENTER - RETRY", true);
        } else if (_serving) {
            display.drawText(1, 30, "ENTER - SERVE", true);
        } else {
            display.drawText(1, 52, "L/R - MOVE", true);
        }
    }

    bool finished() const override { return _finished; }
    void exit() override { _finished = true; }

private:
    static constexpr uint8_t PADDLE_W = 2;
    static constexpr uint8_t WIN_SCORE = 5;

    void serve() {
        _serving = false;
        _ballRow = Config::ROWS - 2;
        _ballCol = _player + PADDLE_W / 2;
        _velRow = -1;
        _velCol = random(0, 2) == 0 ? 1 : -1;
        _tickMs = 200;
        _lastTickMs = millis();
    }

    void speedUp() {
        if (_tickMs > 90) _tickMs -= 10;
    }

    void afterPoint() {
        _trailValid = false;
        if (_playerScore >= WIN_SCORE || _aiScore >= WIN_SCORE) {
            _over = true;
        } else {
            _serving = true;
            _tickMs = 200;
        }
    }

    void drawField(uint32_t nowMs) {
        (void)nowMs;
        for (uint8_t i = 0; i < PADDLE_W; ++i) {
            _kbd->led().setOverlayColor((Config::ROWS - 1) * Config::COLS + _player + i, CRGB(0, 200, 60));
            _kbd->led().setOverlayColor(_ai + i, CRGB(220, 30, 0));
        }
        if (!_serving && !_over) {
            if (_trailValid) {
                _kbd->led().setOverlayColor(_trailRow * Config::COLS + _trailCol, CRGB(90, 40, 0));
            }
            _kbd->led().setOverlayColor(_ballRow * Config::COLS + _ballCol, CRGB(255, 255, 255));
        }
    }

    MultiKey *_kbd = nullptr;
    uint8_t _player = 0;
    uint8_t _ai = 0;
    uint8_t _ballRow = 0;
    uint8_t _ballCol = 0;
    int8_t _velRow = -1;
    int8_t _velCol = 1;
    uint8_t _trailRow = 0;
    uint8_t _trailCol = 0;
    bool _trailValid = false;
    uint8_t _playerScore = 0;
    uint8_t _aiScore = 0;
    uint16_t _tickMs = 200;
    uint32_t _lastTickMs = 0;
    bool _serving = true;
    bool _over = false;
    bool _finished = false;
};

PongGame pongGame;

} // namespace

Game *pongGameInstance() { return &pongGame; }

#endif // GAME_PONG
