# MultiKey ESP32-C3 Keyboard — Full Project Context Export
(Compacted chat history for continuation in another assistant. Source code intentionally excluded; this describes everything needed to continue working on the firmware and the configurator app.)

## 1. Hardware

- MCU: ESP32-C3 Super Mini (Arduino IDE, esp32 core 3.3.12, board "ESP32C3 Dev Module", USB CDC On Boot: Enabled).
- 35-key matrix, 7 rows × 5 cols, diodes per key.
  - `ROW_PINS = {20, 10, 9, 8, 7, 6, 5}` — driven, active HIGH.
  - `COL_PINS = {21, 0, 1, 4, 3}` — INPUT_PULLDOWN.
  - **Series resistors on the column lines were originally 6.8kΩ; this broke scanning on rows 3/4 (voltage divider with the OLED pull-ups dropped column voltage below VIH). User replaced them with 1.5kΩ — fixed. This was a real hardware bug, do not re-diagnose as software.**
- OLED: 0.96" SSD1306 clone, 128×64, I2C, **two-colour panel: fixed yellow band = top 8 px (page 0), blue = rest, 1 px gap between regions — this is normal for this panel**. Mounted with yellow band at the bottom in the keyboard, so firmware uses DISPLAY_ROTATE_180. No 2-px column offset (not a SH1106).
  - OLED I2C is **bit-banged on GPIO8 (SDA) / GPIO7 (SCL), which are shared with matrix rows 3/4**. Hardware I2C can never be used: the matrix scanner's pinMode/digitalWrite would reroute the peripheral. Pins are OUTPUT_OPEN_DRAIN; HIGH = released (module pull-ups), LOW = pulled low; the scanner's row drive levels coincide with I2C idle levels, so scanning and I2C chunks interleave per main-loop iteration with no pin reconfiguration.
  - **Bus ordering rule (critical, hard-won):** whenever SDA and SCL are pulled LOW outside a transaction, SCL must fall BEFORE SDA. SDA falling while SCL is high is an I2C START; spurious STARTs wedge this clone panel. This is enforced in `Ssd1306.cpp` (all hand-back sites) and in `MatrixScanner.cpp` (preamble iterates rows descending so row 4/SCL falls before row 3/SDA).
  - Bit-bang edges are padded to ~400 kHz with `delayMicroseconds(1)`; unpadded (~3 MHz) corrupts the panel.
- LEDs: WS2812 strip, 35 LEDs, GPIO2, default global brightness 50. LED index mapping: `ledIndex = (ROWS-1-row) + col*ROWS` from matrix (row,col).
- BLE HID (NimBLE via Arduino-ESP32 3.x) + USB CDC for config/CLI. UART0 pins are used by the matrix, so USB CDC On Boot is mandatory.

## 2. Key numbering (user's "picture" notation — ALWAYS use this with the user)

User refers to keys 1–35 left-to-right, top-to-bottom on the physical 7-wide × 5-tall layout (as on his schematic photo).
- picture key N → firmware matrix index: `((N-1) % 7) * 5 + (N-1) / 7`
- firmware index → picture key: `(index % COLS) * ROWS + index / COLS + 1`
- Nav keys: picture **11/17/19/25/18** = up, left, right, down, enter = firmware indices **16/12/22/18/17**.
- Boot-chooser red keys: the four corners, picture 1/7/29/35 = firmware 0/30/4/34.
- The GUI keymap editor grid is rotated 90° CCW to match this physical orientation; the on-device key-grid editor draws matrix rows horizontally, cols vertically.

## 3. Repository layout

- Firmware: `D:\YandexDisk\Stud\PROJECTS\keyboard dual\CODE\MultiKey_ESP32C3_USB_CDC_MATRIX_POLARITY\`
  (sketch .ino same name; modules: `Keyboard.cpp/.h`, `MatrixScanner`, `KeymapStore` (NVS), `Settings.h`, `Keymaps`, `HidKeyboard`, `SerialCli`, `src/display/Ssd1306.*`, `src/menu/MenuApp.*`, `src/menu/Menu.h`, `src/led/LedController.* + Palettes`, `src/games/`, `src/link/` (multi-keyboard)).
- Configurator webapp: `...\CODE\ESP32keyCfg\` (browser app, WebSerial USB CDC).
- Diagnostic sketches (archives, no longer needed): `CODE\MenuNavTest\`, `CODE\SSD1306_I2C_Test\`.
- The old working directory `CODE\ESP32\V0\2` was deleted during cleanup — do not reference it.
- User compiles/flashes via Arduino IDE himself; the assistant cannot compile (Windows box, no arduino-cli set up). User reports results verbally/with photos.

## 4. GUI configurator app (ESP32keyCfg) — delivered features

- Connects over USB CDC (WebSerial). Connect button sits left, right after app name/version in the header.
- Layer/keymap editing; key grid rotated 90° CCW to match physical layout.
- Presets: store/recall layer presets and complete keymaps (device slots + app-side).
- Backups: before flashing a new keymap, the app dumps the existing one to a temp file; history of up to 100 backup files.
- Global settings tab: global LED brightness, base effect, LED sleep timeout, palettes, per-layer LED settings, multikey settings, nav-key bindings, OLED sleep, boot menu on/off + time, HID label language (EN/RU), USB/BLE modes, BT TX power, rollover.
- Fixed during the session: "read from keyboard doesn't fill the GUI" and "Apply → Verify failed (Layer 1, key 1: device=TRANSPARENT expected=HID 76 0) — changes left unsaved in RAM" (LED settings already worked). Cause was on the app side (dump/verify flow), fixed.
- Serial CLI (CDC) also exposes: `MENU OPEN`, `SET GLOBAL NAVKEYS 16 12 22 18 17`, `SET GLOBAL NAVBRIGHT 128`, etc. Protocol documented in `SERIAL_PROTOCOL.md`.

## 5. Firmware features delivered this session

- **Global LED settings persisted to ROM (NVS)**: global animation + parameters save/restore via `KeymapStore`. `LedSettings` is a packed wire/storage struct with `static_assert(sizeof==119)` — an earlier edit made it 118 and broke the build; fixed by correcting the assert/struct to match. If you change the struct, update the assert deliberately.
- **On-device menu system** (`src/menu/`), on the OLED, navigated by the 5 nav keys:
  - Boot flow: at power-on, the four corner keys glow red for `bootMenuTimeMs` (default 2 s, user asked 30 s at one point — check current NVS/global value; settable in menu). If all four red keys are held during the window, they flash once and the **main menu** opens. Otherwise normal keyboard operation; **the menu is only reachable at boot or via CLI `MENU OPEN` — no hold-key shortcut after EXIT** (by user request).
  - Main menu items: KEY CFG (LAYERS / EDIT KEYS / HOLD THRESHOLD / LABELS EN-RU / PRESETS / EXIT), LED CFG (GENERAL / PER-LAYER / PER-KEY / LAYER CHANGE / KEYPRESS FX / PALETTES), HID & CONNECT, GAMES, SCREEN (OLED sleep, NAV KEYS binding, BOOT MENU on/off, BOOT TIME, NAV LED BRIGHT).
  - Selection cursor is a blinking `>` arrow (400 ms blink), NOT colour highlighting — because the panel's yellow band is fixed hardware. Menu navigation is one step per press, no auto-repeat (by design).
  - Menu overlays LEDs: nav keys glow red (`menuNavBrightness`, direct 0..255, no gamma), ENTER greenish, pressed keys brighten; overlay plane renders last so animations can't cover nav keys.
- **LED energy saving**: after LED sleep timeout (default 1 min), if BLE is not connected, only key 0 pulses blue instead of the whole matrix.
- BLE-disconnected indicator no longer wipes out menu/nav LEDs.

## 6. Bugs found & fixed (do NOT re-diagnose these)

1. **6.8kΩ column resistors broke rows 3/4 scanning** → user swapped to 1.5kΩ. Hardware fix, done.
2. **Spurious I2C START on hand-back** (SDA pulled low before SCL after each transaction) wedged the clone panel → fixed ordering everywhere (SCL falls first), documented in Ssd1306.cpp header comment.
3. **Stale NVS globals** made nav keys invisible (navBrightness 30 at FastLED brightness 50) → bumped `GLOBAL_VERSION` 1→2 in KeymapStore.h so defaults ({16,12,22,18,17}, 128) load. Bump this version again whenever GlobalSettings defaults must be force-refreshed on user devices.
4. **Menu selection reset**: `MenuList::begin()` reset `_selected=0` every tick (lists rebuilt every loop) → selection now persists in `Screen.selected`, restored via `list.select(screen.selected)` in BOTH `handleScreen` (input path) AND `drawScreen` (render path — the last "cursor doesn't move" bug was the render path missing the restore; fixed).
5. **ScreenId/item-value collisions** in SCR_KB_MENU dispatch (HOLD/LABELS values 2/3 collided with SCR_LAYERS/SCR_LAYER_OPTS) → sentinel values 100/101; dispatch is fully value-based, `v==-1` = EXIT/pop.
6. **LEFT/RIGHT swapped** → `oledNavKeys` semantics are now `[up, RIGHT, LEFT, down, enter]` (index 1 = RIGHT key 12, index 2 = LEFT key 22). Nav-keys binding screen labels and idle status line updated to match.
7. **Display never recovered from cold panel / unplug** (the last fix): root cause — `Display.begin()` ran exactly once at boot and the driver never re-initialized. MenuNavTest "fixed" panels only because it re-ran full init at every phase. Fix in `src/display/Ssd1306.*`: read the I2C ACK bit on every transaction's address byte; 3 consecutive NACKs → panel declared offline → `update()` retries bus-recovery (9 SCL clocks + STOP) + full init every 500 ms until ACK; boot `begin()` retries init 3×. Result: unplug/reconnect and power-cycled panels recover on their own within ~0.5–1 s. **This fix was just written and NOT yet confirmed flashed/working by the user.**
8. Earlier compile errors fixed along the way: `SDA`/`SCL` name clash with variant `pins_arduino.h` (renamed locals to OLED_SDA/OLED_SCL), missing `menu/MenuApp.h` include path (wrong sketch folder copy), `i2cStart` etc. not declared during the hw-i2c experiment (that experiment was fully reverted — `DISPLAY_HW_I2C` no longer exists).

## 7. Current state / open items

- Last delivered change: display auto-reconnect/re-init (section 6, item 7). **Awaiting user flash test**: (a) cold boot, (b) unplug/replug while running → should recover in ≤1 s, (c) reset button, (d) boot with display disconnected then connect later.
- Before that, user confirmed: menu opens, cursor flashes and moves, ENTER works, LAYERS enterable, nav keys visible in menu, left/right swap requested and applied (also unverified by flash).
- The full menu tree (LED CFG editors, GAMES, layer editing, presets, colour picker, palette editor) has NOT been systematically tested on device after the selection fixes — expect follow-up bug reports there.
- OLED yellow band = title bar area (top 8 px after rotation); list body is in the blue area.
- Boot chooser currently: 2 s window (user once asked 30 s — verify `bootMenuTimeMs` if he complains), red corners, EXIT item leaves menu until reboot.
- Known panel quirk: 1 px gap between yellow/blue regions — normal, not a bug.
- User communicates tersely; reply plainly, cite file:line, tell him exactly what to flash and what to watch for. He cannot be asked to run compilers himself beyond Arduino IDE "Verify/Upload".
