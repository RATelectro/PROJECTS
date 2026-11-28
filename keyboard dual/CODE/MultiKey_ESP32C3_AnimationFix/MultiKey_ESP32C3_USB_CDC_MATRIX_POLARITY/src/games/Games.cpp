// Game registry: every game lives in its own file in this folder (see the
// description comment at the top of each) and is compiled in only when its
// GAME_* switch in KeyboardConfig.h is 1. Games never send HID commands and
// are never stored as configuration.
//
// Estimated flash cost of this file: <0.5 KB program space.

#include "Games.h"

uint8_t GameManager::count() const {
    uint8_t n = 0;
#if GAME_SNAKE
    ++n;
#endif
#if GAME_LIGHTS_OUT
    ++n;
#endif
#if GAME_SIMON
    ++n;
#endif
#if GAME_PONG
    ++n;
#endif
#if GAME_REACTION
    ++n;
#endif
#if GAME_WHACK_A_MOLE
    ++n;
#endif
    return n;
}

Game *GameManager::get(uint8_t index) {
    uint8_t n = 0;
#if GAME_SNAKE
    if (index == n) return snakeGameInstance();
    ++n;
#endif
#if GAME_LIGHTS_OUT
    if (index == n) return lightsOutGameInstance();
    ++n;
#endif
#if GAME_SIMON
    if (index == n) return simonGameInstance();
    ++n;
#endif
#if GAME_PONG
    if (index == n) return pongGameInstance();
    ++n;
#endif
#if GAME_REACTION
    if (index == n) return reactionGameInstance();
    ++n;
#endif
#if GAME_WHACK_A_MOLE
    if (index == n) return whackAMoleGameInstance();
    ++n;
#endif
    return nullptr;
}

const char *GameManager::name(uint8_t index) const {
    Game *game = const_cast<GameManager *>(this)->get(index);
    return game != nullptr ? game->name() : "?";
}

void GameManager::start(uint8_t index, MultiKey &keyboard) {
    _active = get(index);
    if (_active != nullptr) _active->begin(keyboard);
}

void GameManager::stop() {
    _active = nullptr;
}
