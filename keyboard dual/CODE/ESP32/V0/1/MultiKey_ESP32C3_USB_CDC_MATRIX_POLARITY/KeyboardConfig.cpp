#include "KeyboardConfig.h"

namespace Config {

// Names retain the original sketch's software matrix orientation.
// ROW_PINS are driven outputs; COL_PINS are sensed with pull-downs.
const uint8_t ROW_PINS[ROWS] = {20, 10, 9, 8, 7, 6, 5};
const uint8_t COL_PINS[COLS] = {21, 0, 1, 2, 3};

} // namespace Config
