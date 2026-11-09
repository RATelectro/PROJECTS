#include "Keymaps.h"
#include "HidUsages.h"
#include <string.h>

#define K(k) hidAction((uint8_t)(k), MOD_NONE)
#define KM(k, m) hidAction((uint8_t)(k), (uint8_t)(m))

// Original physical layout, flattened as index = row * COLS + col.
const KeyAction DEFAULT_BASE_KEYMAP[Config::NUM_KEYS] = {
    K(KEY_DELETE), K(KEY_0), K(KEY_LEFT_ALT), K(KEY_T), K(KEY_RIGHT_ARROW),
    K(KEY_SPACE), K(KEY_8), K(KEY_M), K(KEY_UP_ARROW), K(KEY_DOWN_ARROW),
    K(KEY_F4), K(KEY_6), K(KEY_R), K(KEY_F), K(KEY_LEFT_ARROW),
    K(KEY_F3), K(KEY_4), K(KEY_E), K(KEY_D), K(KEY_B),
    K(KEY_F2), K(KEY_2), K(KEY_W), K(KEY_S), K(KEY_X),
    K(KEY_F1), K(KEY_1), K(KEY_Q), K(KEY_A), K(KEY_Z),
    K(KEY_ESC), KM(KEY_GRAVE, MOD_LSHIFT), K(KEY_TAB), K(KEY_LEFT_SHIFT), K(KEY_LEFT_CTRL)
};

const char DEFAULT_LAYER_NAMES[Config::MAX_LAYERS][Config::LAYER_NAME_LENGTH] = {
    "base", "numbers", "layer 3", "system", "layer 5", "layer 6", "layer 7", "layer 8"
};

void loadDefaultKeymaps(KeyAction maps[Config::MAX_LAYERS][Config::NUM_KEYS]) {
    for (uint8_t layer = 0; layer < Config::MAX_LAYERS; ++layer) {
        for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
            maps[layer][index] = (layer == 0)
                ? DEFAULT_BASE_KEYMAP[index]
                : transparentAction();
        }
    }
}

#undef K
#undef KM
