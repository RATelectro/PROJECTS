# MultiKey ESP32-C3 keyboard

Modular Arduino IDE sketch for the original 7x5 ESP32-C3 SuperMini hardware.

## Build

1. Open `MultiKey_ESP32C3_USB_CDC_MATRIX_POLARITY.ino` in Arduino IDE. Keep every `.h` and `.cpp` file in the same sketch folder.
2. Install Arduino-ESP32 3.x and FastLED 3.9 or newer.
3. Select the matching ESP32-C3 board, **Hardware CDC and JTAG** USB mode, and **USB CDC On Boot: Enabled**.
4. Compile and upload. Do not use UART0 for logging: GPIO20/21 are matrix pins. The browser editor uses native USB CDC.

## Files

- `Keyboard.h/.cpp`: public keyboard API, key events, hold timing, layer engine.
- `MatrixScanner.h/.cpp`: electrical scan and debounce only.
- `HidKeyboard.h/.cpp`: BLE HID reports, connection state, compatible keymap GATT service.
- `KeymapStore.h/.cpp`: versioned, CRC-checked dual-slot NVS persistence.
- `Keymaps.h/.cpp` and `HidUsages.h`: compiled defaults and HID constants.
- `LedController.h/.cpp`: manual LED control, hello sweep, keypress/sleep/BLE animations.
- `SerialCli.h/.cpp`: native USB CDC command parser.
- `SERIAL_PROTOCOL.md`: complete browser-editor command list and response examples.

## Main API

The global `keyboard` object exposes:

```cpp
keyboard.update();
keyboard.isPressed(index);
keyboard.rawIsPressed(index);
keyboard.justPressed(index);
keyboard.justReleased(index);
keyboard.holdDuration(index);
keyboard.heldFor(index, durationMs);
keyboard.isHeld(index);                 // configured HOLD_DUR_MS threshold
keyboard.holdThresholdReached(index);   // one-shot HOLD event has fired
keyboard.onKeyEvent(callback);
keyboard.onLayerEvent(callback);
keyboard.onBleEvent(callback);

keyboard.activeLayer();
keyboard.baseLayer();
keyboard.setBaseLayer(layer);
keyboard.keyAction(layer, index);
keyboard.setKeyAction(layer, index, action);
keyboard.saveKeymaps();                 // all; optional layer argument
keyboard.recallKeymaps();
keyboard.resetKeymaps();                // RAM only

keyboard.led();                         // complete LedController API
keyboard.hid();                         // BLE/HID state and report control
keyboard.processSerialCommand(line);   // optional command injection
```

Key event callbacks receive press, one-shot threshold hold, and release events. Release and hold events include `heldMs`; each event also includes index, row, column, active layer, and the latched action.

## Hardware notes

The active-HIGH matrix polarity, pins, physical LED index transformation, startup rainbow sweep, BLE HID identity, and legacy three-byte GATT keymap writes are preserved. GPIO2/8/9 remain strapping-pin considerations from the original sketch.

`MULTIKEY_USE_NKRO` defaults to `0` (6KRO), as in the uploaded working sketch. Setting the macro to `1` in `KeyboardConfig.h` enables the 31-byte NKRO bitmap report.
