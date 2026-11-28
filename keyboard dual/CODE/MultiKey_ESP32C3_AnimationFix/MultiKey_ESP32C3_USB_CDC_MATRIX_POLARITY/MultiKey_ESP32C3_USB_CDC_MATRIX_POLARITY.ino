/*
 * MultiKey ESP32-C3 keyboard
 *
 * Arduino IDE:
 *   Board: ESP32C3 Dev Module (or your matching C3 board)
 *   USB Mode: Hardware CDC and JTAG (default; config/flashing over USB CDC)
 *             or OTG TinyUSB if you want USB HID output as well
 *   USB CDC On Boot: Enabled
 *
 * Libraries: FastLED 3.9+; BLE HID is supplied by Arduino-ESP32 3.x.
 * All application functionality is available through the `keyboard` object.
 */

#include <Arduino.h>

#if defined(ARDUINO_USB_MODE) && (ARDUINO_USB_MODE == 1)
// Hardware CDC and JTAG: USB is the config/flashing port. USB HID output
// requires "USB Mode: OTG" (ARDUINO_USB_MODE=0); the USB mode setting is
// still stored and reported to the configurator.
#endif
#if !defined(ARDUINO_USB_CDC_ON_BOOT) || (ARDUINO_USB_CDC_ON_BOOT != 1)
#error "Enable USB CDC On Boot; UART0 pins are used by the matrix."
#endif

#include "Keyboard.h"

MultiKey keyboard;

void setup() {
  keyboard.begin();
}

void loop() {
  keyboard.update();
}
