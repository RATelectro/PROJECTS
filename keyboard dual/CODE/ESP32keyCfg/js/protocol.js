/*
 * MultiKey Configurator — protocol and model helpers.
 *
 * Pure logic only: no DOM, no WebSerial. Used by the app in the browser
 * (exposed as window.MK) and by the Node test-suite (module.exports).
 *
 * Mirrors the firmware serial protocol described in SERIAL_PROTOCOL.md and
 * the data structures in the sketch (KeyAction.h, KeyboardConfig.h,
 * Keymaps.cpp, HidUsages.h). Firmware must not change to match this file;
 * this file only describes what the firmware already speaks.
 */
(function (root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  root.MK = api;
})(typeof self !== 'undefined' ? self : globalThis, function () {
  'use strict';

  const MAX_LAYERS = 8;
  const LAYER_NAME_MAX = 15;
  const DEFAULT_HOLD_MS = 500;

  const LAYER_MODES = ['MOMENTARY', 'SWITCH', 'MODAL'];
  const LED_EFFECTS = ['OFF', 'STATIC', 'RAINBOW', 'BREATHE', 'CUSTOM'];
  const KEYPRESS_ANIMS = ['OFF', 'PULSE', 'SPARKLE', 'RIPPLE'];
  const SLEEP_ANIMS = ['OFF', 'BREATHE', 'RAINBOW'];

  // Modifier bitmask, bit order matches the firmware (mods byte in the HID report).
  const MODS = [
    { bit: 0x01, short: 'C',  name: 'LCtrl' },
    { bit: 0x02, short: 'S',  name: 'LShift' },
    { bit: 0x04, short: 'A',  name: 'LAlt' },
    { bit: 0x08, short: 'G',  name: 'LGUI' },
    { bit: 0x10, short: 'RC', name: 'RCtrl' },
    { bit: 0x20, short: 'RS', name: 'RShift' },
    { bit: 0x40, short: 'RA', name: 'RAlt' },
    { bit: 0x80, short: 'RG', name: 'RGUI' },
  ];

  // ---------------------------------------------------------------------------
  // HID usage table (USB Keyboard/Keypad page, see firmware HidUsages.h)
  // ---------------------------------------------------------------------------

  const LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
  const DIGIT_SHIFT = ['!', '@', '#', '$', '%', '^', '&', '*', '(', ')'];
  const PUNCT = [
    [0x2D, '-', '_'], [0x2E, '=', '+'], [0x2F, '[', '{'], [0x30, ']', '}'],
    [0x31, '\\', '|'], [0x33, ';', ':'], [0x34, "'", '"'], [0x35, '`', '~'],
    [0x36, ',', '<'], [0x37, '.', '>'], [0x38, '/', '?'],
  ];

  const HID_TABLE = [];
  const USAGE_MAP = {};

  function addUsage(u, cat, label, sub) {
    const e = { u, cat, label, sub: sub || '' };
    HID_TABLE.push(e);
    USAGE_MAP[u] = e;
  }

  for (let i = 0; i < 26; i++) addUsage(0x04 + i, 'chars', LETTERS[i].toLowerCase(), LETTERS[i]);
  for (let i = 0; i < 10; i++) addUsage(0x1E + i, 'chars', String((i + 1) % 10), DIGIT_SHIFT[i]);
  for (const [u, a, b] of PUNCT) addUsage(u, 'chars', a, b);

  addUsage(0xE0, 'modifier', 'LCtrl');
  addUsage(0xE1, 'modifier', 'LShift');
  addUsage(0xE2, 'modifier', 'LAlt');
  addUsage(0xE3, 'modifier', 'LGUI');
  addUsage(0xE4, 'modifier', 'RCtrl');
  addUsage(0xE5, 'modifier', 'RShift');
  addUsage(0xE6, 'modifier', 'RAlt');
  addUsage(0xE7, 'modifier', 'RGUI');

  addUsage(0x4F, 'movement', 'Right', '→');
  addUsage(0x50, 'movement', 'Left', '←');
  addUsage(0x51, 'movement', 'Down', '↓');
  addUsage(0x52, 'movement', 'Up', '↑');
  addUsage(0x4A, 'movement', 'Home');
  addUsage(0x4D, 'movement', 'End');
  addUsage(0x4B, 'movement', 'PgUp');
  addUsage(0x4E, 'movement', 'PgDn');

  addUsage(0x28, 'special', 'Enter');
  addUsage(0x29, 'special', 'Esc');
  addUsage(0x2A, 'special', 'Bksp');
  addUsage(0x2B, 'special', 'Tab');
  addUsage(0x2C, 'special', 'Space');
  addUsage(0x39, 'special', 'Caps');
  addUsage(0x46, 'special', 'PrtSc');
  addUsage(0x47, 'special', 'ScrLk');
  addUsage(0x48, 'special', 'Pause');
  addUsage(0x49, 'special', 'Ins');
  addUsage(0x4C, 'special', 'Del');
  addUsage(0x53, 'special', 'NumLk');
  addUsage(0x65, 'special', 'Menu');

  for (let i = 0; i < 12; i++) addUsage(0x3A + i, 'function', 'F' + (i + 1));
  for (let i = 0; i < 12; i++) addUsage(0x68 + i, 'function', 'F' + (i + 13));

  addUsage(0x54, 'numpad', 'Num /');
  addUsage(0x55, 'numpad', 'Num *');
  addUsage(0x56, 'numpad', 'Num -');
  addUsage(0x57, 'numpad', 'Num +');
  addUsage(0x58, 'numpad', 'NumEnt');
  for (let i = 0; i < 9; i++) addUsage(0x59 + i, 'numpad', 'Num ' + (i + 1));
  addUsage(0x62, 'numpad', 'Num 0');
  addUsage(0x63, 'numpad', 'Num .');

  const KEY_CATEGORIES = [
    { id: 'chars', label: 'Characters' },
    { id: 'modifier', label: 'Modifiers' },
    { id: 'movement', label: 'Movement' },
    { id: 'special', label: 'Special' },
    { id: 'function', label: 'F-keys' },
    { id: 'numpad', label: 'Numpad' },
  ];

  // ---------------------------------------------------------------------------
  // Actions
  // ---------------------------------------------------------------------------

  function transparentAction() { return { t: 'transparent' }; }
  function hidAction(u, m) { return { t: 'hid', u: u | 0, m: m | 0 }; }
  function layerAction(target, mode) {
    return { t: 'layer', target: target | 0, mode: LAYER_MODES.includes(mode) ? mode : 'MOMENTARY' };
  }

  function normalizeAction(a) {
    if (!a || typeof a !== 'object' || typeof a.t !== 'string') return null;
    if (a.t === 'transparent') return transparentAction();
    if (a.t === 'hid') {
      const u = toInt(a.u), m = toInt(a.m);
      if (u === null || m === null || u < 0 || u > 0xE7 || m < 0 || m > 0xFF) return null;
      return hidAction(u, m);
    }
    if (a.t === 'layer') {
      const target = toInt(a.target);
      if (target === null || target < 0 || target > MAX_LAYERS - 1) return null;
      return layerAction(target, a.mode);
    }
    return null;
  }

  function actionsEqual(a, b) {
    if (!a || !b || a.t !== b.t) return false;
    if (a.t === 'hid') return a.u === b.u && a.m === b.m;
    if (a.t === 'layer') return a.target === b.target && a.mode === b.mode;
    return true;
  }

  function serializeAction(a) {
    if (a.t === 'hid') return `HID ${a.u} ${a.m}`;
    if (a.t === 'layer') return `LAYER ${a.target} ${a.mode}`;
    return 'TRANSPARENT';
  }

  // ---------------------------------------------------------------------------
  // Labels for key caps
  // ---------------------------------------------------------------------------

  function modPrefix(m) {
    return MODS.filter((x) => (m & x.bit) !== 0).map((x) => x.short).join('+');
  }

  function modsLabel(m) {
    const names = MODS.filter((x) => (m & x.bit) !== 0).map((x) => x.name);
    return names.length ? names.join('+') : '';
  }

  function usageInfo(u) {
    return USAGE_MAP[u] || { label: '0x' + u.toString(16).toUpperCase(), sub: '' };
  }

  // Returns {main, sub, cls} for rendering a key cap.
  function actionLabel(a) {
    if (!a) return { main: '?', sub: '', cls: '' };
    if (a.t === 'transparent') return { main: '▽', sub: '', cls: 'k-transparent' };
    if (a.t === 'layer') {
      const sub = { MOMENTARY: 'hold', SWITCH: 'set', MODAL: 'tgl' }[a.mode] || 'hold';
      return { main: 'L' + (a.target + 1), sub, cls: 'k-layer' };
    }
    if (a.u === 0) {
      if (a.m === 0) return { main: '—', sub: '', cls: 'k-none' };
      return { main: modsLabel(a.m), sub: '', cls: 'k-mod' };
    }
    const info = usageInfo(a.u);
    const prefix = modPrefix(a.m);
    const shifted = (a.m & 0x22) !== 0 && info.sub; // LShift/RShift with a shifted variant
    const main = (prefix ? prefix + '+' : '') + info.label;
    const sub = !prefix || shifted ? info.sub : '';
    return { main, sub, cls: a.m ? 'k-mod' : 'k-hid' };
  }

  function actionDescription(a) {
    if (a.t === 'transparent') return 'Transparent — falls through to lower layers';
    if (a.t === 'layer') {
      const mode = { MOMENTARY: 'momentary (while held)', SWITCH: 'switch (set base layer)', MODAL: 'modal (toggle)' }[a.mode];
      return `Layer ${a.target + 1} — ${mode}`;
    }
    if (a.u === 0) return a.m === 0 ? 'Disabled (no key)' : `Modifier only: ${modsLabel(a.m)}`;
    const info = usageInfo(a.u);
    const mods = modsLabel(a.m);
    return mods ? `${mods} + ${info.label} (usage 0x${a.u.toString(16).toUpperCase()})`
                : `${info.label} (usage 0x${a.u.toString(16).toUpperCase()})`;
  }

  // ---------------------------------------------------------------------------
  // Line parsers
  // ---------------------------------------------------------------------------

  function toInt(v) {
    if (typeof v === 'number' && Number.isFinite(v)) return Math.floor(v);
    if (typeof v === 'string' && /^-?\d+$/.test(v)) return parseInt(v, 10);
    return null;
  }

  function parseInfo(line) {
    const m = /^OK INFO version=(\S+) protocol=(\d+) rows=(\d+) cols=(\d+) keys=(\d+) max_layers=(\d+) layers=(\d+) nkro=(\d+) storage=(\d+)\s*$/.exec(line || '');
    if (!m) return null;
    return {
      version: m[1], protocol: +m[2], rows: +m[3], cols: +m[4], keys: +m[5],
      maxLayers: +m[6], layers: +m[7], nkro: +m[8] === 1, storage: +m[9] === 1,
    };
  }

  const KEY_LINE_RE = /^(?:OK )?KEY layer=(\d+) index=(\d+) row=(\d+) col=(\d+) type=(HID|LAYER|TRANSPARENT) usage=0x([0-9A-Fa-f]{1,2}) mods=0x([0-9A-Fa-f]{1,2}) target=(\d+) mode=([A-Za-z_]+)\s*$/;

  function parseKeyLine(line) {
    const m = KEY_LINE_RE.exec(line || '');
    if (!m) return null;
    const type = m[5];
    let action;
    if (type === 'HID') action = hidAction(parseInt(m[6], 16), parseInt(m[7], 16));
    else if (type === 'LAYER') action = layerAction(+m[8], m[9].toUpperCase());
    else action = transparentAction();
    return { layer: +m[1], index: +m[2], row: +m[3], col: +m[4], action };
  }

  // Parses the collected lines of `GET KEYMAP [ALL]` into an array indexed by
  // layer, each entry an array of actions indexed by key index. Missing entries
  // are filled with TRANSPARENT, and `keymaps.incomplete` is set true so callers
  // can detect and retry — the ESP32-C3 USB-CDC TX buffer can drop bytes from
  // large replies, so a full dump is not always trustworthy.
  function parseGetKeymap(lines, expectedLayers) {
    const blocks = [];
    let current = null;
    let incomplete = false;
    for (const line of lines) {
      const hm = /^OK KEYMAP layer=(\d+) entries=(\d+)\s*$/.exec(line);
      if (hm) {
        current = { layer: +hm[1], entries: +hm[2], keys: [] };
        blocks[current.layer] = current;
        continue;
      }
      if (/^END KEYMAP layer=\d+\s*$/.test(line)) { current = null; continue; }
      const k = parseKeyLine(line);
      if (k && current) current.keys[k.index] = k.action;
      else if (line.startsWith('KEY ')) incomplete = true;
    }
    const keymaps = [];
    for (let l = 0; l < (expectedLayers || blocks.length); l++) {
      const block = blocks[l];
      if (!block) { keymaps.push(null); incomplete = true; continue; }
      const keys = [];
      for (let i = 0; i < block.entries; i++) {
        if (!block.keys[i]) incomplete = true;
        keys.push(block.keys[i] || transparentAction());
      }
      keymaps.push(keys);
    }
    keymaps.incomplete = incomplete;
    return keymaps;
  }

  function parseGetLayers(lines) {
    let header = null;
    const names = [];
    for (const line of lines) {
      const hm = /^OK LAYERS count=(\d+) active=(\d+) base=(\d+)\s*$/.exec(line);
      if (hm) { header = { count: +hm[1], active: +hm[2], base: +hm[3] }; continue; }
      const lm = /^LAYER layer=(\d+) name=(.*)$/.exec(line);
      if (lm) names[+lm[1]] = lm[2];
    }
    if (!header) return null;
    for (let l = 0; l < header.count; l++) if (names[l] === undefined) names[l] = `layer ${l + 1}`;
    return { count: header.count, active: header.active, base: header.base, names };
  }

  function parseHold(line) {
    const m = /^OK HOLD ms=(\d+)\s*$/.exec(line || '');
    return m ? +m[1] : null;
  }

  function parseDirty(line) {
    const m = /^OK DIRTY mask=0x([0-9A-Fa-f]+) metadata=(\d)\s*$/.exec(line || '');
    return m ? { mask: parseInt(m[1], 16), metadata: +m[2] === 1 } : null;
  }

  function parseLedStatus(line) {
    const m = /^OK LED enabled=(\d) brightness=(\d+) effect=(\d) color=0x([0-9A-Fa-f]{6}) keypress=(\d) sleep=(\d) sleep_timeout=(\d+) sleeping=(\d) ble_indicators=(\d)\s*$/.exec(line || '');
    if (!m) return null;
    return {
      enabled: m[1] === '1', brightness: +m[2], effect: LED_EFFECTS[+m[3]] || 'OFF',
      color: '#' + m[4].toLowerCase(), keypress: KEYPRESS_ANIMS[+m[5]] || 'OFF',
      sleep: SLEEP_ANIMS[+m[6]] || 'OFF', sleepTimeoutMs: +m[7],
      sleeping: m[8] === '1', bleIndicators: m[9] === '1',
    };
  }

  function parseState(line) {
    const m = /^OK STATE pressed=([0-9A-Fa-f]{1,16}) active=(\d+) base=(\d+) ble=(\w+)\s*$/.exec(line || '');
    if (!m) return null;
    const pressed = BigInt('0x' + m[1]);
    const indices = [];
    for (let i = 0; i < 64; i++) if ((pressed >> BigInt(i)) & 1n) indices.push(i);
    return { pressed, indices, active: +m[2], base: +m[3], ble: m[4] };
  }

  function parseBleStatus(line) {
    const m = /^OK BLE state=(\w+) advertising=(\d)\s*$/.exec(line || '');
    return m ? { state: m[1], advertising: m[2] === '1' } : null;
  }

  // ---------------------------------------------------------------------------
  // Model helpers
  // ---------------------------------------------------------------------------

  function clone(x) { return JSON.parse(JSON.stringify(x)); }

  function sanitizeLayerName(s) {
    let out = String(s == null ? '' : s).replace(/[^\x20-\x7E]/g, ' ').trim();
    if (out.length > LAYER_NAME_MAX) out = out.slice(0, LAYER_NAME_MAX);
    return out;
  }

  // Computes the minimal change set between the device snapshot and the model.
  function computeDiff(snapshot, model) {
    const errors = [];
    const count = model.layerCount;
    const keyChanges = [];
    for (let l = 0; l < count; l++) {
      const devLayer = snapshot.keymaps[l] || [];
      const modLayer = model.keymaps[l] || [];
      for (let i = 0; i < modLayer.length; i++) {
        const dev = devLayer[i] || transparentAction();
        if (!actionsEqual(dev, modLayer[i])) keyChanges.push({ layer: l, index: i, action: modLayer[i] });
      }
    }
    const names = [];
    for (let l = 0; l < count; l++) {
      if ((snapshot.layerNames[l] || '') !== (model.layerNames[l] || '')) names.push({ layer: l, name: model.layerNames[l] });
    }
    const holdMs = snapshot.holdMs !== model.holdMs ? model.holdMs : null;
    const baseLayer = snapshot.baseLayer !== model.baseLayer ? model.baseLayer : null;
    const layerCountChanged = snapshot.layerCount !== count;

    for (const c of keyChanges) {
      if (c.action.t === 'layer' && c.action.target >= count) {
        errors.push(`Key L${c.layer + 1} #${c.index + 1} targets layer ${c.action.target + 1}, which is disabled.`);
      }
    }
    if (baseLayer !== null && baseLayer >= count) errors.push('Base layer is disabled.');

    const hasChanges = layerCountChanged || keyChanges.length > 0 || names.length > 0 || holdMs !== null || baseLayer !== null;
    return { layerCount: count, layerCountChanged, keyChanges, names, holdMs, baseLayer, hasChanges, errors };
  }

  // ---------------------------------------------------------------------------
  // Validation / normalization of external data (backups, presets, imports)
  // ---------------------------------------------------------------------------

  function emptyLayer(keys) {
    return Array.from({ length: keys }, () => transparentAction());
  }

  // Validates a full keymap object. Returns {ok:true, value} or {ok:false, error}.
  function normalizeKeymapData(data, keys, maxLayers) {
    maxLayers = maxLayers || MAX_LAYERS;
    if (!data || typeof data !== 'object') return { ok: false, error: 'Not a keymap object' };
    const layerCount = toInt(data.layerCount);
    if (layerCount === null || layerCount < 1 || layerCount > maxLayers) {
      return { ok: false, error: `layerCount must be 1..${maxLayers}` };
    }
    if (!Array.isArray(data.keymaps) || data.keymaps.length < layerCount) {
      return { ok: false, error: 'keymaps array missing or too short' };
    }
    const keymaps = [];
    for (let l = 0; l < layerCount; l++) {
      const layer = data.keymaps[l];
      if (!Array.isArray(layer) || layer.length !== keys) {
        return { ok: false, error: `Layer ${l + 1} must have exactly ${keys} keys` };
      }
      const out = [];
      for (let i = 0; i < layer.length; i++) {
        const a = normalizeAction(layer[i]);
        if (!a) return { ok: false, error: `Invalid action at layer ${l + 1}, key ${i + 1}` };
        out.push(a);
      }
      keymaps.push(out);
    }
    let holdMs = toInt(data.holdMs);
    if (holdMs === null || holdMs < 1 || holdMs > 65535) holdMs = DEFAULT_HOLD_MS;
    const layerNames = [];
    for (let l = 0; l < layerCount; l++) {
      layerNames.push(sanitizeLayerName((data.layerNames || [])[l]) || `layer ${l + 1}`);
    }
    return { ok: true, value: { layerCount, layerNames, holdMs, keymaps } };
  }

  // Validates a single-layer preset. Returns {ok:true, value:{name, keys}} or {ok:false, error}.
  function normalizeLayerData(data, keys) {
    if (!data || typeof data !== 'object' || !Array.isArray(data.keys)) {
      return { ok: false, error: 'Layer preset must contain a keys array' };
    }
    if (data.keys.length !== keys) return { ok: false, error: `Layer preset must have exactly ${keys} keys` };
    const out = [];
    for (let i = 0; i < data.keys.length; i++) {
      const a = normalizeAction(data.keys[i]);
      if (!a) return { ok: false, error: `Invalid action at key ${i + 1}` };
      out.push(a);
    }
    return { ok: true, value: { name: sanitizeLayerName(data.name), keys: out } };
  }

  function makeBackupRecord(src) {
    return {
      kind: 'backup',
      createdAt: Date.now(),
      device: { version: src.info ? src.info.version : null, protocol: src.info ? src.info.protocol : null },
      layerCount: src.layerCount,
      layerNames: clone(src.layerNames),
      baseLayer: src.baseLayer,
      holdMs: src.holdMs,
      keymaps: clone(src.keymaps),
      led: src.led ? clone(src.led) : null,
    };
  }

  // ---------------------------------------------------------------------------
  // Factory defaults (mirror Keymaps.cpp / KeyboardConfig.h compiled defaults)
  // ---------------------------------------------------------------------------

  const FACTORY_LAYER0 = [
    [0x4C, 0], [0x27, 0], [0xE2, 0], [0x17, 0], [0x4F, 0],
    [0x2C, 0], [0x25, 0], [0x10, 0], [0x52, 0], [0x51, 0],
    [0x3D, 0], [0x23, 0], [0x15, 0], [0x09, 0], [0x50, 0],
    [0x3C, 0], [0x21, 0], [0x08, 0], [0x07, 0], [0x05, 0],
    [0x3B, 0], [0x1F, 0], [0x1A, 0], [0x16, 0], [0x1B, 0],
    [0x3A, 0], [0x1E, 0], [0x14, 0], [0x04, 0], [0x1D, 0],
    [0x29, 0], [0x35, 0x02], [0x2B, 0], [0xE1, 0], [0xE0, 0],
  ];

  const FACTORY_LAYER_NAMES = ['base', 'numbers', 'layer 3', 'system', 'layer 5', 'layer 6', 'layer 7', 'layer 8'];

  function factoryKeymap(keys) {
    keys = keys || 35;
    const keymaps = [];
    for (let l = 0; l < MAX_LAYERS; l++) {
      if (l === 0) keymaps.push(FACTORY_LAYER0.map(([u, m]) => hidAction(u, m)));
      else keymaps.push(emptyLayer(keys));
    }
    return {
      layerCount: 4,
      layerNames: FACTORY_LAYER_NAMES.slice(),
      holdMs: DEFAULT_HOLD_MS,
      baseLayer: 0,
      keymaps,
    };
  }

  return {
    MAX_LAYERS, LAYER_NAME_MAX, DEFAULT_HOLD_MS,
    LAYER_MODES, LED_EFFECTS, KEYPRESS_ANIMS, SLEEP_ANIMS, MODS,
    HID_TABLE, USAGE_MAP, KEY_CATEGORIES,
    transparentAction, hidAction, layerAction, normalizeAction, actionsEqual,
    serializeAction, actionLabel, actionDescription, usageInfo, modsLabel, modPrefix,
    parseInfo, parseKeyLine, parseGetKeymap, parseGetLayers, parseHold, parseDirty,
    parseLedStatus, parseState, parseBleStatus,
    clone, sanitizeLayerName, computeDiff,
    normalizeKeymapData, normalizeLayerData, makeBackupRecord,
    factoryKeymap, emptyLayer,
  };
});
