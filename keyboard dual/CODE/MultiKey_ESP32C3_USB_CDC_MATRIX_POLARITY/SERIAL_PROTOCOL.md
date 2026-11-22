# MultiKey USB CDC serial protocol

## Transport and framing

- Use the ESP32-C3 native USB CDC port. Baud rate is ignored by USB CDC; `115200` is only a conventional host setting.
- UTF-8/ASCII text, one command per line. Accept LF, CRLF, or CR. Maximum command length: 254 characters.
- Commands are case-insensitive. Numeric values are decimal or `0x`-prefixed hexadecimal.
- Key index is `row * 5 + col`, range `0..34`. Layers are zero-based.
- Normal replies begin with `OK`; failures begin with `ERR`. Multi-line replies end with an `END` line.
- With `EVENTS ON`, unsolicited `EVENT ...` lines can arrive between other lines. The editor must not treat them as command replies.
- No command echo is required from the editor. Wait for the final `OK`, `ERR`, or `END` response before sending another command.

## Connection and capability discovery

| Command | Response / purpose |
|---|---|
| `PING` | `OK PING` |
| `INFO` | Firmware/protocol version, matrix size, maximum/current layer count, NKRO and storage availability |
| `HELP` | Compact command summary |
| `BLE STATUS` | Connection and advertising state |
| `GET STATE` | Debounced pressed-key bitmap, active/base layer, BLE state |

`GET STATE` returns `pressed` as a 64-bit hexadecimal bitmap. Bit `n` is physical key `n`.

## Keymap editing

### Read

| Command | Response |
|---|---|
| `GET KEYMAP` | All keys on all currently enabled layers |
| `GET KEYMAP ALL` | Same as above |
| `GET KEYMAP <layer>` | One layer |
| `GET KEY <layer> <index>` | One action |

Each keymap block is:

```text
OK KEYMAP layer=0 entries=35
KEY layer=0 index=0 row=0 col=0 type=HID usage=0x4C mods=0x00 target=0 mode=MOMENTARY
...
END KEYMAP layer=0
```

For `LAYER` actions, only `target` and `mode` are meaningful. For `TRANSPARENT`, the remaining fields are zero. A transparent key falls through through lower-numbered layers until a non-transparent action is found.

### Write RAM

```text
SET KEY <layer> <index> HID <usage> <mods>
SET KEY <layer> <index> LAYER <target> <MOMENTARY|SWITCH|MODAL>
SET KEY <layer> <index> TRANSPARENT
```

`HID 0 0` disables a key on that layer. `usage` is a USB HID Keyboard/Keypad usage ID (`0..0xE7`), not an ASCII code. `mods` is a bitmask:

```text
01 LCtrl   02 LShift   04 LAlt   08 LGUI
10 RCtrl   20 RShift   40 RAlt   80 RGUI
```

Layer modes:

- `MOMENTARY` (`WHILE_PRESSED` is accepted as an alias): target layer is active while held. Overlapping momentary keys use the most recently pressed target.
- `SWITCH`: press sets the base layer and leaves it there after release.
- `MODAL`: press toggles between the target and the base layer that was active before entering it.

Actions are latched when a physical key is pressed, so changing layers while keys are held does not change their HID release behavior.

The original command `SET <row> <col> <usage> <mods>` remains accepted as a layer-0 HID edit.

### Save, recall, reset

| Command | Behavior |
|---|---|
| `SAVE` / `SAVE ALL` | Atomically write all layers plus layer metadata and hold threshold to NVS |
| `SAVE <layer>` | Write only that layer's actions; preserve other saved layers |
| `RECALL` / `LOAD` | Reload all saved layers and metadata into RAM |
| `RECALL <layer>` / `LOAD <layer>` | Reload only that layer's actions |
| `RESET` / `RESET ALL` | Restore all compiled defaults in RAM; does not save |
| `RESET <layer>` | Restore one compiled default layer in RAM; does not save |
| `GET DIRTY` | Bitmask of RAM layers with unsaved changes, plus a metadata-dirty flag |

The typical editor apply sequence is: send all `SET` commands, verify with `GET KEYMAP`, then send `SAVE ALL`. Storage uses two CRC-checked NVS slots and chooses the newest valid generation at boot.

## Layer and hold settings

| Command | Behavior |
|---|---|
| `GET LAYERS` | Layer count, active/base layers, and names |
| `GET LAYER` | Active and base layer |
| `SET LAYER <layer>` | Set base layer immediately |
| `SET LAYER_COUNT <1..8>` | Enable/disable editor layers; metadata is saved by `SAVE ALL` |
| `SET LAYER_NAME <layer> <name>` | Set a name up to 15 ASCII characters; spaces are allowed |
| `GET HOLD` | Current hold threshold |
| `SET HOLD <1..65535>` | Set hold threshold in milliseconds; saved by `SAVE ALL` |

The default threshold is the compile-time `HOLD_DUR_MS` value, 500 ms.

## Events

```text
EVENTS ON
EVENTS OFF
GET EVENTS
```

Event lines:

```text
EVENT KEY type=PRESS index=6 row=1 col=1 layer=0 held_ms=0
EVENT KEY type=HOLD index=6 row=1 col=1 layer=0 held_ms=500
EVENT KEY type=RELEASE index=6 row=1 col=1 layer=0 held_ms=612
EVENT LAYER active=1 base=0
EVENT BLE state=CONNECTED
```

`HOLD` is emitted once per physical press when the configured threshold is reached. `RELEASE.held_ms` is the final duration.

## LED control

| Command | Behavior |
|---|---|
| `LED STATUS` | Read all runtime LED settings |
| `LED ENABLE ON\|OFF` | Enable/disable output |
| `LED BRIGHTNESS <0..255>` | Global FastLED brightness |
| `LED COLOR <r> <g> <b>` | Static/pulse color |
| `LED EFFECT OFF\|STATIC\|RAINBOW\|BREATHE\|CUSTOM` | Base effect |
| `LED KEYPRESS OFF\|PULSE\|SPARKLE\|RIPPLE` | Optional keypress animation |
| `LED SLEEP OFF\|BREATHE\|RAINBOW` | Inactivity animation |
| `LED SLEEP_TIMEOUT <ms>` | Inactivity delay; `0` disables sleep |
| `LED BLE ON\|OFF` | Enable blue pending and green success animations |
| `LED SET <index> <r> <g> <b>` | Set one key's color and switch to `CUSTOM` |
| `LED CLEAR` | Clear all custom colors |
| `LED SAVE` | Persist LED settings to NVS immediately |
| `LED HELLO` / `HELLO` | Replay the retained startup sweep; blocks scanning during the sweep |

LED settings (enabled, brightness, base effect, color, keypress/sleep animations, sleep timeout, BLE indicators, custom colors) are persisted to a CRC-checked NVS blob automatically about 2 s after the last change, and are loaded at boot. They are independent of the keymap NVS slots. No keypress animation is enabled by default. BLE pending/success animations temporarily override base effects when enabled.

## Recommended editor workflow

1. Open the port, send `PING`, then `INFO`; reject an unsupported `protocol` major version.
2. Send `GET LAYERS` and `GET KEYMAP ALL` and parse until each matching `END` line.
3. Apply edits with `SET KEY`, layer metadata commands, and `SET HOLD` as needed.
4. Read back the edited layer(s), compare against intended data, then `SAVE ALL`.
5. For verification, send `RECALL ALL` followed by `GET KEYMAP ALL`.
6. Enable `EVENTS ON` only when live key/layer/BLE monitoring is wanted.

## Protocol version 2 (firmware 3.0) additions

`INFO` reports `protocol=2`. All version-1 commands keep working; `LED STATUS`
gains fields (extra key=value pairs — parsers must ignore unknown pairs).

### Extended LED

| Command | Response / purpose |
|---|---|
| `LED SPEED <EFFECT|KEYPRESS|SLEEP> <1..255>` | animation speed (128 = 1x) |
| `LED MIX <0..4>` | stacking algorithm: 0 ADD NORM, 1 ADD CLIP, 2 MAX, 3 SCREEN, 4 HUE |
| `LED BACKLIGHT <0..255> [r g b]` | idle-key backlight brightness + colour |
| `LED LAYERFLASH <ON|OFF> [nr ng nb pr pg pb] [fadeMs]` | layer-change flash colours and fade |
| `LED LAYER <layer> BRIGHT <0..255>` | per-layer brightness (stored in keymap) |
| `LED LAYER <layer> EFFECT <0..4|255>` | per-layer effect; 255 = follow global |
| `LED LAYER <layer> COLOR <r> <g> <b>` | per-layer colour; 0 0 0 = follow global |
| `GET LEDLAYER <layer>` | `OK LEDLAYER layer=N brightness=B effect=E color=RRGGBB` |

### Device globals

`GET GLOBAL` returns one line of key=value pairs: `rollover usb bt bt_power
mk mk_id mk_layout save_slave show_all lang oled_sleep nav=up,left,right,down,enter
boot boot_ms nav_bright`.

`SET GLOBAL <field> <value>` fields: `ROLLOVER (0|1|6KRO|NKRO)`, `USB (HID|CFG)`,
`BT (HID|COM)`, `BTPOWER (0..7)`, `MK (ON|OFF)`, `MKID (0..5)`, `MKLAYOUT (0..255)`,
`SAVECHANGES (ON|OFF)`, `SHOWALL (ON|OFF)`, `LANG (EN|RU)`, `OLEDSLEEP (seconds)`,
`BOOTMENU (ON|OFF)`, `BOOTTIME (ms, min 1000)`, `NAVBRIGHT (0..255)`,
`NAVKEYS <up> <left> <right> <down> <enter>`.

### Palettes (stored in the keymap)

| Command | Response / purpose |
|---|---|
| `GET PALETTE <0..15>` | `OK PALETTE index=N stops=C` then `STOP i r g b` lines, ends `END PALETTE` |
| `SET PALETTE <idx> <stop> <r> <g> <b>` | edit a palette stop (up to 8) |

### Presets and multikey

| Command | Response / purpose |
|---|---|
| `PRESET SAVE <1..6>` / `PRESET LOAD <n>` | save/load full global config snapshot |
| `PRESET LIST` | slot occupancy |
| `MULTIKEY STATUS` | role, slave count, options |
| `MULTIKEY PUSH` | push global config to connected slaves |
| `MULTIKEY SLOT SAVE <n>` / `LOAD <n>` | multikey configuration slots on the master |

### Menus and games

| Command | Purpose |
|---|---|
| `MENU OPEN` / `MENU CLOSE` | drive the on-device menu over CDC |
| `GAME LIST` / `GAME START <n>` / `GAME STOP` | games never send HID and are not stored |

### Bluetooth COM port

With `SET GLOBAL BT COM` the firmware exposes a Nordic-UART style service
(0xFFE0/0xFFE1/0xFFE2). Commands received there are executed like CDC input;
`EVENT` lines are mirrored to subscribed COM clients. Command replies remain
on the USB CDC port.
