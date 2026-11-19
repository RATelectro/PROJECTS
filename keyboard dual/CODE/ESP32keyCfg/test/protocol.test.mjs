/* Node test-suite for js/protocol.js. Run: node test/protocol.test.mjs */
import { createRequire } from 'module';

const require = createRequire(import.meta.url);
const MK = require('../js/protocol.js');

let passed = 0;
let failed = 0;

function eq(actual, expected, label) {
  const a = JSON.stringify(actual);
  const e = JSON.stringify(expected);
  if (a === e) { passed++; }
  else { failed++; console.error(`FAIL: ${label}\n  expected: ${e}\n  actual:   ${a}`); }
}

function ok(value, label) {
  if (value) { passed++; } else { failed++; console.error(`FAIL: ${label}`); }
}

// ---------------------------------------------------------------- INFO
const info = MK.parseInfo('OK INFO version=2.0.0 protocol=1 rows=7 cols=5 keys=35 max_layers=8 layers=4 nkro=0 storage=1');
ok(info !== null, 'parseInfo accepts reply');
eq(info, { version: '2.0.0', protocol: 1, rows: 7, cols: 5, keys: 35, maxLayers: 8, layers: 4, nkro: false, storage: true }, 'parseInfo fields');
ok(MK.parseInfo('OK INFO version=2.0.0 protocol=1 rows=7 cols=5 keys=35 max_layers=8 layers=4 nkro=0 storage=') === null, 'parseInfo rejects truncated line');

// ---------------------------------------------------------------- Key lines
const k1 = MK.parseKeyLine('KEY layer=0 index=0 row=0 col=0 type=HID usage=0x4C mods=0x00 target=0 mode=MOMENTARY');
eq(k1.action, { t: 'hid', u: 0x4C, m: 0 }, 'parseKeyLine HID');
const k2 = MK.parseKeyLine('KEY layer=2 index=7 row=1 col=2 type=LAYER usage=0x00 mods=0x00 target=3 mode=MODAL');
eq(k2.action, { t: 'layer', target: 3, mode: 'MODAL' }, 'parseKeyLine LAYER');
const k3 = MK.parseKeyLine('OK KEY layer=1 index=3 row=0 col=3 type=TRANSPARENT usage=0x00 mods=0x00 target=0 mode=MOMENTARY');
eq(k3.action, { t: 'transparent' }, 'parseKeyLine TRANSPARENT with OK prefix');
eq(k3.index, 3, 'parseKeyLine index with OK prefix');

// ---------------------------------------------------------------- KEYMAP block
const block = [
  'OK KEYMAP layer=0 entries=3',
  'KEY layer=0 index=0 row=0 col=0 type=HID usage=0x29 mods=0x00 target=0 mode=MOMENTARY',
  'KEY layer=0 index=1 row=0 col=1 type=LAYER usage=0x00 mods=0x00 target=2 mode=SWITCH',
  'KEY layer=0 index=2 row=0 col=2 type=TRANSPARENT usage=0x00 mods=0x00 target=0 mode=MOMENTARY',
  'END KEYMAP layer=0',
];
const km = MK.parseGetKeymap(block, 1);
eq(km.length, 1, 'parseGetKeymap layer count');
eq(km[0].length, 3, 'parseGetKeymap key count');
eq(km[0][1], { t: 'layer', target: 2, mode: 'SWITCH' }, 'parseGetKeymap action');
eq(km.incomplete, false, 'parseGetKeymap complete flag on full data');

const brokenBlock = block.filter((l) => !l.startsWith('KEY layer=0 index=1 '));
const kmBroken = MK.parseGetKeymap(brokenBlock, 1);
eq(kmBroken.incomplete, true, 'parseGetKeymap incomplete when a KEY line is missing');
eq(kmBroken[0][1], { t: 'transparent' }, 'parseGetKeymap fills the gap with TRANSPARENT');

const noHeader = block.filter((l) => !l.startsWith('OK KEYMAP'));
const kmNoHdr = MK.parseGetKeymap(noHeader, 1);
eq(kmNoHdr.incomplete, true, 'parseGetKeymap incomplete when a block header is missing');
eq(kmNoHdr[0], null, 'parseGetKeymap null layer when a block header is missing');

// ---------------------------------------------------------------- LAYERS
const layersLines = [
  'OK LAYERS count=4 active=1 base=0',
  'LAYER layer=0 name=base',
  'LAYER layer=1 name=numbers',
  'LAYER layer=2 name=layer 3',
  'LAYER layer=3 name=system',
  'END LAYERS',
];
const layers = MK.parseGetLayers(layersLines);
eq(layers, { count: 4, active: 1, base: 0, names: ['base', 'numbers', 'layer 3', 'system'] }, 'parseGetLayers incl. spaces in name');

// ---------------------------------------------------------------- Other single-line parsers
eq(MK.parseHold('OK HOLD ms=500'), 500, 'parseHold');
eq(MK.parseHold('OK HOLD ms=abc'), null, 'parseHold rejects garbage');
eq(MK.parseDirty('OK DIRTY mask=0x05 metadata=1'), { mask: 5, metadata: true }, 'parseDirty');
eq(MK.parseLedStatus('OK LED enabled=1 brightness=50 effect=2 color=0xFF0A10 keypress=1 sleep=2 sleep_timeout=30000 sleeping=0 ble_indicators=1'),
  { enabled: true, brightness: 50, effect: 'RAINBOW', color: '#ff0a10', keypress: 'PULSE', sleep: 'RAINBOW', sleepTimeoutMs: 30000, sleeping: false, bleIndicators: true },
  'parseLedStatus');
const st = MK.parseState('OK STATE pressed=0000000000000040 active=1 base=0 ble=CONNECTED');
eq(st.indices, [6], 'parseState pressed bit 6');
eq(st.active, 1, 'parseState active');
eq(MK.parseBleStatus('OK BLE state=CONNECTED advertising=0'), { state: 'CONNECTED', advertising: false }, 'parseBleStatus');

// ---------------------------------------------------------------- Serialization
eq(MK.serializeAction({ t: 'hid', u: 0x4C, m: 0x02 }), 'HID 76 2', 'serializeAction HID decimal');
eq(MK.serializeAction({ t: 'layer', target: 3, mode: 'MOMENTARY' }), 'LAYER 3 MOMENTARY', 'serializeAction LAYER');
eq(MK.serializeAction({ t: 'transparent' }), 'TRANSPARENT', 'serializeAction TRANSPARENT');

// ---------------------------------------------------------------- Diff
const snap = {
  layerCount: 2,
  layerNames: ['base', 'numbers'],
  baseLayer: 0,
  holdMs: 500,
  keymaps: [
    [{ t: 'hid', u: 0x04, m: 0 }, { t: 'hid', u: 0x05, m: 0 }],
    [{ t: 'transparent' }, { t: 'transparent' }],
  ],
};

const same = MK.clone(snap);
let d = MK.computeDiff(snap, same);
ok(!d.hasChanges && d.keyChanges.length === 0, 'diff: no changes');

const changed = MK.clone(snap);
changed.keymaps[1][1] = { t: 'layer', target: 0, mode: 'SWITCH' };
d = MK.computeDiff(snap, changed);
eq(d.keyChanges.length, 1, 'diff: one key change');
eq(d.keyChanges[0], { layer: 1, index: 1, action: { t: 'layer', target: 0, mode: 'SWITCH' } }, 'diff: change payload');

const renamed = MK.clone(snap);
renamed.layerNames[1] = 'fn';
d = MK.computeDiff(snap, renamed);
eq(d.names, [{ layer: 1, name: 'fn' }], 'diff: rename');

const more = MK.clone(snap);
more.layerCount = 3;
more.keymaps[2] = MK.emptyLayer(2);
d = MK.computeDiff(snap, more);
ok(d.layerCountChanged && d.hasChanges, 'diff: layer count increase');

const holdChanged = MK.clone(snap);
holdChanged.holdMs = 250;
d = MK.computeDiff(snap, holdChanged);
eq(d.holdMs, 250, 'diff: hold threshold');

const baseChanged = MK.clone(snap);
baseChanged.baseLayer = 1;
d = MK.computeDiff(snap, baseChanged);
eq(d.baseLayer, 1, 'diff: base layer');

const bad = MK.clone(snap);
bad.keymaps[0][0] = { t: 'layer', target: 5, mode: 'MOMENTARY' };
d = MK.computeDiff(snap, bad);
ok(d.errors.length === 1, 'diff: validation error for disabled target layer');

// ---------------------------------------------------------------- Normalization
const factory = MK.factoryKeymap(35);
eq(factory.keymaps.length, 8, 'factory: 8 layers');
eq(factory.keymaps[0].length, 35, 'factory: 35 keys');
eq(factory.keymaps[0][31], { t: 'hid', u: 0x35, m: 0x02 }, 'factory: Shift+Grave at index 31');
eq(factory.keymaps[3][0], { t: 'transparent' }, 'factory: layer 4 transparent');
eq(factory.layerNames[3], 'system', 'factory: default name');
eq(factory.holdMs, 500, 'factory: hold threshold');
eq(factory.layerCount, 4, 'factory: default layer count');

const norm = MK.normalizeKeymapData(factory, 35, 8);
ok(norm.ok, 'normalizeKeymapData accepts factory');
eq(norm.value.layerCount, 4, 'normalize: layerCount');

ok(!MK.normalizeKeymapData({ layerCount: 9, keymaps: [] }, 35, 8).ok, 'normalize rejects layerCount > max');
ok(!MK.normalizeKeymapData({ layerCount: 2, keymaps: [[], []] }, 35, 8).ok, 'normalize rejects wrong key count');
ok(!MK.normalizeKeymapData({ layerCount: 1, keymaps: [[{ t: 'bogus' }]] }, 1, 8).ok, 'normalize rejects bad action');
ok(!MK.normalizeKeymapData(null, 35, 8).ok, 'normalize rejects non-object');

const ln = MK.normalizeLayerData({ name: 'fn', keys: MK.emptyLayer(35) }, 35);
ok(ln.ok && ln.value.keys.length === 35, 'normalizeLayerData accepts layer preset');
ok(!MK.normalizeLayerData({ keys: MK.emptyLayer(34) }, 35).ok, 'normalizeLayerData rejects wrong length');

// ---------------------------------------------------------------- Labels
eq(MK.actionLabel({ t: 'hid', u: 0x04, m: 0 }).main, 'a', 'label: letter lowercase');
eq(MK.actionLabel({ t: 'hid', u: 0x1E, m: 0x02 }).main, 'S+1', 'label: shift+digit');
eq(MK.actionLabel({ t: 'hid', u: 0x1E, m: 0x02 }).sub, '!', 'label: shifted symbol as sub');
eq(MK.actionLabel({ t: 'hid', u: 0x4C, m: 0x05 }).main, 'C+A+Del', 'label: modifier chord');
eq(MK.actionLabel({ t: 'hid', u: 0, m: 0 }).main, '—', 'label: disabled');
eq(MK.actionLabel({ t: 'hid', u: 0, m: 0x01 }).main, 'LCtrl', 'label: modifier only');
eq(MK.actionLabel({ t: 'layer', target: 2, mode: 'MOMENTARY' }).main, 'L3', 'label: layer target');
eq(MK.actionLabel({ t: 'layer', target: 2, mode: 'MODAL' }).sub, 'tgl', 'label: layer mode');
eq(MK.actionLabel({ t: 'transparent' }).main, '▽', 'label: transparent');

// ---------------------------------------------------------------- Names / backup
eq(MK.sanitizeLayerName("badname"), "bad name", "sanitizeLayerName replaces control chars with space (firmware behavior)");
eq(MK.sanitizeLayerName('a'.repeat(20)).length, 15, 'sanitizeLayerName caps at 15');

const backup = MK.makeBackupRecord({
  info, layerCount: factory.layerCount, layerNames: factory.layerNames.slice(0, 4),
  baseLayer: 0, holdMs: 500, keymaps: factory.keymaps.slice(0, 4), led: null,
});
eq(backup.kind, "backup", "backup kind");
eq(backup.device.version, "2.0.0", "backup device version");
eq(backup.keymaps.length, 4, "backup keymaps");
ok(MK.normalizeKeymapData(backup, 35, 8).ok, "backup round-trips through normalizeKeymapData");

// actionDescription smoke tests
ok(MK.actionDescription({ t: 'layer', target: 1, mode: 'SWITCH' }).includes('switch'), 'description: layer switch');
ok(MK.actionDescription({ t: 'hid', u: 0x29, m: 0 }).includes('Esc'), 'description: Esc');

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
