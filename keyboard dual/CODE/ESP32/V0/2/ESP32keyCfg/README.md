# MultiKey Configurator

GUI configurator for the MultiKey ESP32-C3 keyboard (`MultiKey_ESP32C3_USB_CDC_MATRIX_POLARITY`).
It connects to the keyboard over **USB CDC (Web Serial)** and speaks the text protocol from
`../MultiKey_ESP32C3_USB_CDC_MATRIX_POLARITY/SERIAL_PROTOCOL.md`. The keyboard firmware is
**not** modified in any way.

## Running

- **Recommended:** double-click `start.bat`. It starts a tiny local server and opens
  `http://localhost:8321` in your browser.
- Or open `index.html` directly in **Chrome** or **Edge** (Web Serial needs a Chromium
  browser; if the page is blocked from opening the port, use `start.bat`).
- Click **Connect**, pick the keyboard's CDC port (ESP32-C3, USB VID `0x303A`), and the app
  reads the current keymap, layers, hold threshold and LED state.

No installation and no internet connection are required — the app is plain HTML/CSS/JS
with zero dependencies.

## Features

### Keymap tab
- Visual 7×5 matrix (row 1 = physical top row), color-coded key caps
  (key / modifier combo / layer key / transparent / disabled).
- Transparent keys show the label they inherit from lower layers.
- Layer tabs: add/remove layers (1–8), rename layers (15 printable chars, like the firmware),
  pick the base layer, edit the hold threshold (HOLD event, ms).
- Click a key to assign: a HID key with optional modifier chord
  (Characters / Modifiers / Movement / Special / F-keys / Numpad + search), a layer key
  (Momentary / Switch / Modal), Transparent, or Disabled.
- **Apply to keyboard** sends only the changed keys, re-reads the keymap to verify, then
  `SAVE ALL`. Flashing is refused if verification fails.

### LED tab (global keyboard settings)
LEDs on/off, global brightness (0–255), base effect (OFF/STATIC/RAINBOW/BREATHE/CUSTOM),
static/pulse color, keypress animation (OFF/PULSE/SPARKLE/RIPPLE), sleep animation
(OFF/BREATHE/RAINBOW), sleep timeout (ms, 0 = never), BLE connect indicators, hello-sweep test.
These settings are runtime-only in the firmware and are not persisted across reboots.

### Device tab
Firmware/protocol info, BLE status, live pressed-key monitor, manual
`SAVE ALL` / `RECALL ALL` / `RESET ALL`.

### Presets
Save the current layer or the whole keymap as a named preset (stored in the browser's
IndexedDB), apply presets back, export/import them as JSON. A built-in "Factory defaults"
preset matches the compiled firmware defaults.

### Backups
Before **every** flash (Apply or Restore), the app dumps the complete current keymap
(layers, names, hold threshold, LED state) and stores it as a backup — up to **100**
backups, oldest removed automatically. Backups can be restored to the keyboard
(the current state is backed up first), exported as JSON files, or deleted individually.

### Log / console
The drawer at the bottom shows all serial traffic and accepts raw firmware commands
(e.g. `HELP`, `GET STATE`, `EVENTS ON`).

## Files

| Path | Purpose |
|---|---|
| `index.html` | page layout |
| `style.css` | dark theme |
| `js/protocol.js` | pure protocol/model logic: parsers, serializers, diffing, validation, factory defaults |
| `js/app.js` | Web Serial client and all UI logic (backups/presets use IndexedDB) |
| `test/protocol.test.mjs` | Node test-suite for `js/protocol.js` — `node test/protocol.test.mjs` |
| `test/harness.html` + `test/mock-serial.js` + `test/scenario.js` | Browser end-to-end test with a scripted mock firmware (no hardware needed): serve the folder, open `test/harness.html`, all 22 checks must show PASS in the page title |
| `start.bat` | local-server launcher for Windows |

## Safety notes

- Protocol major version is checked at connect (only version 1 is supported).
- Opening the CDC port resets the ESP32-C3: the app waits for the `OK READY`
  boot banner, then retries `PING` until the board accepts commands.
- Commands are sent one at a time; every reply must match the command's expected
  shape (late replies from timed-out commands are discarded), and `SET KEY`
  replies are matched by layer/key index — `EVENT …` and boot noise can never
  desync a session.
- Keymap dumps are validated (every layer and key present) and retried; if the
  ESP32-C3 drops bytes from the large `GET KEYMAP ALL` reply (small USB-CDC TX
  buffer), the app falls back to per-layer and then per-key reads, which use
  single-line replies that cannot overflow.
- A flash always creates a backup first; if the backup cannot be made, nothing
  is written. If verification fails, nothing is saved — use RECALL ALL (or power
  the keyboard off and on) to revert the unsaved RAM changes.
