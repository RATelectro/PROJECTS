#pragma once

#include <Arduino.h>
#include "../../KeyboardConfig.h"
#include "../menu/Menu.h"

class MultiKey;
class Ssd1306;

// A game runs instead of the keyboard: it receives the menu nav input, draws
// on the OLED and paints the LED matrix through the controller overlay. It
// never sends HID commands and is never stored as configuration.
class Game {
public:
    virtual ~Game() {}
    virtual const char *name() const = 0;
    virtual void begin(MultiKey &keyboard) = 0;
    // Called every tick; set LED overlays here (they are applied on the next
    // LED frame and cleared automatically).
    virtual void update(const MenuInput &input, uint32_t nowMs) = 0;
    virtual void render(Ssd1306 &display) = 0;
    virtual bool finished() const = 0; // 'left' exits
    virtual void exit() {}             // long-'left' requested exit
};

class GameManager {
public:
    uint8_t count() const;
    const char *name(uint8_t index) const;
    Game *get(uint8_t index);
    Game *active() const { return _active; }

    void start(uint8_t index, MultiKey &keyboard);
    void stop();

private:
    Game *_active = nullptr;
};

// Each game lives in its own file and exposes its singleton through one of
// these accessors (declared only when the game is compiled in).
#if GAME_SNAKE
Game *snakeGameInstance();
#endif
#if GAME_LIGHTS_OUT
Game *lightsOutGameInstance();
#endif
#if GAME_SIMON
Game *simonGameInstance();
#endif
#if GAME_PONG
Game *pongGameInstance();
#endif
#if GAME_REACTION
Game *reactionGameInstance();
#endif
#if GAME_WHACK_A_MOLE
Game *whackAMoleGameInstance();
#endif
