/*
 * MultiKey ESP32-C3 keyboard
 *
 * Arduino IDE:
 *   Board: ESP32C3 Dev Module (or your matching C3 board)
 *   USB Mode: Hardware CDC and JTAG
 *   USB CDC On Boot: Enabled
 *
 * Libraries: FastLED 3.9+; BLE HID is supplied by Arduino-ESP32 3.x.
 * All application functionality is available through the `keyboard` object.
 */

#include <Arduino.h>

#if !defined(ARDUINO_USB_MODE) || (ARDUINO_USB_MODE != 1)
#error "Set USB Mode to Hardware CDC and JTAG (ARDUINO_USB_MODE=1)."
#endif
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT != 1)
#error "Enable USB CDC On Boot; UART0 pins are used by the matrix."
#endif

#include "Keyboard.h"

MultiKey keyboard;

// Optional event API example. Register this in setup() if needed:
//
// void keyEvent(const KeyEvent &event, void *) {
//     if (event.type == KEY_EVENT_HOLD) {
//         // The key reached keyboard.holdDurationThreshold() (default 500 ms).
//     }
//     if (event.type == KEY_EVENT_RELEASE) {
//         // event.heldMs is the final hold duration.
//     }
// }

void setup() {
  // keyboard.onKeyEvent(keyEvent);
  keyboard.begin();
  keyboard.led().setBrightness(10);
}

void loop() {
  keyboard.update();

  // Polling API examples (inside loop, after update):
  // if (keyboard.justPressed(0)) { ... }
  // if (keyboard.justReleased(0)) { ... }
  // if (keyboard.isHeld(0)) { ... }
  // uint32_t heldMs = keyboard.holdDuration(0);
  // keyboard.led().setBrightness(80);
}
