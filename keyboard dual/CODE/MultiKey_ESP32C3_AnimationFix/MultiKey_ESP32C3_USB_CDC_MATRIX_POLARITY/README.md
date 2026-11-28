# MultiKey ESP32-C3 firmware

Matrix 7x5 keyboard firmware (BLE HID + USB CDC config), with addressable
RGB LEDs, an SSD1306 OLED menu system, multi-keyboard grouping and games.

## Layout

All platform modules under `src/` — the Arduino IDE compiles `.cpp` sources in
the sketch's `src/` folder (sources in other subfolders are ignored):

```
MultiKey_ESP32C3_USB_CDC_MATRIX_POLARITY.ino   setup()/loop(), board checks
KeyboardConfig.h/.cpp                          pins, sizes, compile-time flags
KeyAction.h / Keymaps.h/.cpp                   key action model + factory keymaps
KeyNames.h                                     EN/RU display labels for HID usages
MatrixScanner.h/.cpp                           debounced matrix scanning
Keyboard.h/.cpp                                MultiKey: ties everything together
HidKeyboard.h/.cpp                             BLE HID, runtime 6KRO/NKRO, reports
SerialCli.h/.cpp                               USB CDC command line (protocol v2)
KeymapStore.h/.cpp                             NVS blobs: keymaps v3, LED, globals,
                                               presets, multikey slots
Settings.h                                     packed settings structs + defaults
LedController.h/.cpp                           60 fps LED framebuffer engine
src/led/Palettes.h/.cpp                         16 palettes, crossfade evaluator
src/display/Ssd1306.h/.cpp                      SSD1306 driver (chunked I2C)
src/display/Font4x6.h                           4x6 pixel font
src/display/Icons.h                             12x12 menu icons
src/menu/Menu.h/.cpp(h)                         list widget + input model
src/menu/MenuApp.h/.cpp                         boot chooser, all menus, key editor,
                                                colour picker, value editor, games host
src/multikey/LinkBle.h/.cpp                     master<->slave BLE link, COM port
src/games/Games.h/.cpp                          game interface + registry
src/games/Snake.cpp, LightsOut.cpp, Simon.cpp,  one game per file, each with a
  Pong.cpp, Reaction.cpp, WhackAMole.cpp          compile-time switch
```

## Arduino IDE settings

- Board: ESP32C3 Dev Module; USB CDC On Boot: Enabled.
- USB Mode: *Hardware CDC and JTAG* (default) — USB is the config/flashing port.
  USB HID output additionally requires *OTG* mode (TinyUSB); the USB mode
  setting is stored either way and shown in the configurator.
- Libraries: FastLED 3.9+, Arduino-ESP32 3.x (BLE stack).

## Compile-time options (KeyboardConfig.h / -D flags)

- `DISPLAY_ROTATE_180` (0/1) — mount the OLED upside down.
- `MULTIKEY_SLOT_COUNT` (6) — preset/multikey config slots. **Everything fits
  with 6 slots** (see flash note below); drop to 1 if you ever need to shrink.
- `GAME_SNAKE`, `GAME_LIGHTS_OUT`, `GAME_SIMON`, `GAME_PONG`,
  `GAME_REACTION`, `GAME_WHACK_A_MOLE` (1/0) — games included.
- `MULTIKEY_ENABLE_USB_HID` (1) — TinyUSB USB HID (OTG builds).
- `MULTIKEY_USE_NKRO` — legacy define; rollover is now a runtime setting.

## OLED display

The SSD1306 (SCL=GPIO7, SDA=GPIO8) shares pins with matrix rows 3/4. The
hardware I2C peripheral cannot stay attached to them (the scanner's GPIO
writes would reroute the pins), so the driver bit-bangs I2C with
digitalWrite() on permanently open-drain pins — open-drain HIGH doubles as
the matrix "active row" level via the display module's pull-ups, so scanning
and I2C interleave with zero pin reconfiguration. One 128-byte page slice is
pushed per main-loop iteration; a 1 KB frame is 8 slices paced to 60 fps.

Boot: 1 s rainbow LED sweep → 2 s boot chooser (hold all four RED keys to
open the main menu, otherwise the window expires). The main menu has an EXIT
item; after EXIT it reopens only on reboot (or via the CLI `MENU OPEN`).
Menus use the
nav keys — in schematic picture numbering (cols 1..7 × rows 1..5, keys 1..35
left-to-right top-to-bottom): default 11 up / 17 left / 19 right / 25 down /
18 enter — lit red
at `menuNavBrightness` (direct 0..255, default 128); enter is a different
colour. Internally these are matrix indices 16/12/22/18/17.

## LEDs

The LED matrix is a 60 fps framebuffer. Animation planes (base effect,
layer-change flash, keypress fx, BLE indicator, menu/game overlay) are mixed
with a selectable algorithm (`MIX ADD NORM` default — adds and rescales so
hues stay saturated and never clip to white; also ADD CLIP / MAX / SCREEN /
HUE). Every animation family has a speed parameter (128 = 1x). Backlight
glows idle keys with its colour; keys touched by an animation only get a
minimum-brightness floor, colour untouched, and backlight turns off in sleep.
Per-layer LED settings (brightness, effect override, colour) are stored
inside the keymap and move with presets/dumps.

## Multikey groups

`keyboard_0` is the master: it is the HID device toward the PC and merges
key events from slaves 1..5 into its reports (6KRO or NKRO). Slaves run their
own keymaps and advertise as `MK-S<id>`; the master scans, connects, and
forwards configuration (MULTIKEY PUSH). With "save slave changes" on, a
slave's local config edits are stored into the master's multikey config
slots. The multikey layout mode is stored in global settings so the
configurator can restore the view when a keymap is dumped or loaded.
Note: simultaneous BLE connections are limited by the NimBLE build
(default ~3); raise `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` for 5 slaves.

## Games

Six games live in `games/`, one per file, and are selected from the GAMES
menu (or `GAME START <n>`). They use the LED matrix + OLED, never send HID,
and are never stored as configuration. Each file has a full description
(rules, controls, LED/display behaviour) and its estimated flash cost in a
comment at the top: Snake ~2.5 KB, Lights Out ~1.5 KB, Simon ~2 KB,
Pong ~2.5 KB, Reaction ~1.5 KB, Whack-a-mole ~1.5 KB (≈12 KB total with all
six enabled).

## FLASH usage

Previous builds consumed ~53%. The firmware 3.0 additions (LED engine,
display, menus, games, link, CLI) are estimated at +90…110 KB of program
space and ~6 KB of NVS — everything **fits with all 6 multikey slots** at
roughly 57-62% depending on the partition scheme. If your build exceeds the
budget, set `-D MULTIKEY_SLOT_COUNT=1` and disable games first.

## Serial protocol

See `SERIAL_PROTOCOL.md`. Protocol version 2.
