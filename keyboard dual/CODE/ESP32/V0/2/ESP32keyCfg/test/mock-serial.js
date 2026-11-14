/*
 * TEST-ONLY: scripted mock of the MultiKey firmware behind a Web Serial API
 * surface. Emulates SerialCli.cpp (SERIAL_PROTOCOL.md) closely enough to run
 * the configurator end-to-end without hardware. Loaded before js/app.js.
 */
(function () {
  const ROWS = 7, COLS = 5, KEYS = 35, MAX_LAYERS = 8;

  function factoryState() {
    const L0 = [
      [0x4C, 0], [0x27, 0], [0xE2, 0], [0x17, 0], [0x4F, 0],
      [0x2C, 0], [0x25, 0], [0x10, 0], [0x52, 0], [0x51, 0],
      [0x3D, 0], [0x23, 0], [0x15, 0], [0x09, 0], [0x50, 0],
      [0x3C, 0], [0x21, 0], [0x08, 0], [0x07, 0], [0x05, 0],
      [0x3B, 0], [0x1F, 0], [0x1A, 0], [0x16, 0], [0x1B, 0],
      [0x3A, 0], [0x1E, 0], [0x14, 0], [0x04, 0], [0x1D, 0],
      [0x29, 0], [0x35, 0x02], [0x2B, 0], [0xE1, 0], [0xE0, 0],
    ];
    const tr = () => ({ type: 'TRANSPARENT', usage: 0, mods: 0, target: 0, mode: 'MOMENTARY' });
    const keymaps = [];
    for (let l = 0; l < MAX_LAYERS; l++) keymaps.push(Array.from({ length: KEYS }, tr));
    L0.forEach(([u, m], i) => { keymaps[0][i] = { type: 'HID', usage: u, mods: m, target: 0, mode: 'MOMENTARY' }; });
    return {
      layerCount: 4, baseLayer: 0, holdMs: 500,
      names: ['base', 'numbers', 'layer 3', 'system', 'layer 5', 'layer 6', 'layer 7', 'layer 8'],
      keymaps, saveCount: 0, setKeyCount: 0,
      led: { enabled: 1, brightness: 50, effect: 0, color: '021400', keypress: 0, sleep: 1, sleepTimeout: 0, sleeping: 0, ble: 1 },
    };
  }

  const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, '0');

  function actionLine(s, layer, index, okPrefix) {
    const a = s.keymaps[layer][index];
    return `${okPrefix ? 'OK ' : ''}KEY layer=${layer} index=${index} row=${Math.floor(index / COLS)} col=${index % COLS} ` +
      `type=${a.type} usage=0x${hex2(a.usage || 0)} mods=0x${hex2(a.mods || 0)} target=${a.target || 0} mode=${a.mode || 'MOMENTARY'}`;
  }

  class MockPort {
    constructor() {
      this.state = factoryState();
      window.__mockState = this.state;
      this._listeners = {};
      this._ctrl = null;
      this._rx = '';
      this._rs = null;
      this._opened = false;
      this._enc = new TextEncoder();
      this._bootTimer = null;
      this._bootUntil = 0;
      this._lossBudget = 1; // one corrupted big keymap dump per connection
    }
    addEventListener(t, fn) { (this._listeners[t] = this._listeners[t] || []).push(fn); }
    async open() {
      this._opened = true;
      // Simulate the real ESP32-C3 reset-on-open: the board boots for a while,
      // ignores commands sent during boot, and prints its banner mid-boot.
      this._bootUntil = Date.now() + 1600;
      clearTimeout(this._bootTimer);
      this._bootTimer = setTimeout(() => {
        if (this._ctrl && this._opened) this._ctrl.enqueue(this._enc.encode('OK READY version=2.0.0 protocol=1\n'));
      }, 1300);
    }
    async close() { this._opened = false; clearTimeout(this._bootTimer); }
    get writable() {
      const port = this;
      return new WritableStream({ write(chunk) { port.feed(new TextDecoder().decode(chunk)); } });
    }
    get readable() {
      if (!this._rs) {
        const port = this;
        this._rs = new ReadableStream({ start(c) { port._ctrl = c; } });
      }
      return this._rs;
    }
    emit(lines) {
      lines.forEach((line, i) => setTimeout(() => {
        if (this._ctrl && this._opened) this._ctrl.enqueue(this._enc.encode(line + '\n'));
      }, 2 * (i + 1)));
    }
    feed(text) {
      this._rx += text;
      let i;
      while ((i = this._rx.search(/[\r\n]/)) !== -1) {
        const line = this._rx.slice(0, i);
        this._rx = this._rx.slice(i + 1);
        if (line.trim()) this.command(line.trim());
      }
    }
    command(line) {
      if (Date.now() < this._bootUntil) return; // firmware is still booting — bytes are lost
      const s = this.state;
      const args = line.split(/\s+/);
      const cmd = args[0].toUpperCase();
      const num = (x) => parseInt(x, 10);
      const reply = [];
      switch (cmd) {
        case 'PING': reply.push('OK PING'); break;
        case 'INFO':
          reply.push(`OK INFO version=2.0.0 protocol=1 rows=${ROWS} cols=${COLS} keys=${KEYS} max_layers=${MAX_LAYERS} layers=${s.layerCount} nkro=0 storage=1`);
          break;
        case 'HELP': reply.push('OK HELP', 'PING | INFO', 'END HELP'); break;
        case 'GET': {
          const what = (args[1] || '').toUpperCase();
          if (what === 'LAYERS') {
            reply.push(`OK LAYERS count=${s.layerCount} active=${s.baseLayer} base=${s.baseLayer}`);
            for (let l = 0; l < s.layerCount; l++) reply.push(`LAYER layer=${l} name=${s.names[l]}`);
            reply.push('END LAYERS');
          } else if (what === 'KEYMAP' || what === 'KEYMAPS') {
            const sel = args[2];
            const layers = (sel === undefined || sel.toUpperCase() === 'ALL' || sel === '*')
              ? Array.from({ length: s.layerCount }, (_, l) => l) : [num(sel)];
            const lines = [];
            for (const l of layers) {
              lines.push(`OK KEYMAP layer=${l} entries=${KEYS}`);
              for (let k = 0; k < KEYS; k++) lines.push(actionLine(s, l, k, false));
              lines.push(`END KEYMAP layer=${l}`);
            }
            // Simulate the ESP32-C3 USB-CDC TX buffer overflow: the first large
            // dump after connect loses its first bytes (block header + lines).
            if (layers.length > 1 && this._lossBudget > 0) {
              this._lossBudget--;
              this.emit(lines.slice(5));
            } else {
              this.emit(lines);
            }
          } else if (what === 'KEY') {
            reply.push(actionLine(s, num(args[2]), num(args[3]), true));
          } else if (what === 'HOLD') reply.push(`OK HOLD ms=${s.holdMs}`);
          else if (what === 'DIRTY') reply.push('OK DIRTY mask=0x00 metadata=0');
          else if (what === 'EVENTS') reply.push('OK EVENTS enabled=0');
          else if (what === 'STATE') reply.push('OK STATE pressed=0000000000000000 active=0 base=0 ble=ADVERTISING');
          else reply.push('ERR GET unknown_argument');
          break;
        }
        case 'SET': {
          const what = (args[1] || '').toUpperCase();
          if (what === 'KEY') {
            const l = num(args[2]), k = num(args[3]), type = (args[4] || '').toUpperCase();
            if (type === 'HID') s.keymaps[l][k] = { type: 'HID', usage: num(args[5]), mods: num(args[6]), target: 0, mode: 'MOMENTARY' };
            else if (type === 'NONE') s.keymaps[l][k] = { type: 'HID', usage: 0, mods: 0, target: 0, mode: 'MOMENTARY' };
            else if (type === 'LAYER') s.keymaps[l][k] = { type: 'LAYER', usage: 0, mods: 0, target: num(args[5]), mode: (args[6] || 'MOMENTARY').toUpperCase() };
            else if (type === 'TRANSPARENT') s.keymaps[l][k] = { type: 'TRANSPARENT', usage: 0, mods: 0, target: 0, mode: 'MOMENTARY' };
            s.setKeyCount++;
            reply.push(`OK SET KEY layer=${l} index=${k}`);
          } else if (what === 'LAYER_COUNT') {
            s.layerCount = num(args[2]);
            if (s.baseLayer >= s.layerCount) s.baseLayer = 0;
            reply.push(`OK SET LAYER_COUNT count=${s.layerCount}`);
          } else if (what === 'LAYER_NAME') {
            const l = num(args[2]);
            s.names[l] = line.slice(line.indexOf(args[3])).slice(0, 15);
            reply.push(`OK SET LAYER_NAME layer=${l} name=${s.names[l]}`);
          } else if (what === 'HOLD') {
            s.holdMs = num(args[2]);
            reply.push(`OK SET HOLD ms=${s.holdMs}`);
          } else if (what === 'LAYER') {
            s.baseLayer = num(args[2]);
            reply.push(`OK SET LAYER base=${s.baseLayer} active=${s.baseLayer}`);
          } else reply.push('ERR SET invalid_syntax');
          break;
        }
        case 'SAVE':
          s.saveCount++;
          reply.push('OK SAVE layer=ALL');
          break;
        case 'RECALL': case 'LOAD': reply.push(`OK ${cmd} layer=ALL`); break;
        case 'RESET':
          this.state = factoryState();
          window.__mockState = this.state;
          reply.push('OK RESET layer=ALL');
          break;
        case 'LED': {
          const sub = (args[1] || '').toUpperCase();
          if (sub === 'STATUS') {
            const L = s.led;
            reply.push(`OK LED enabled=${L.enabled} brightness=${L.brightness} effect=${L.effect} color=0x${L.color} keypress=${L.keypress} sleep=${L.sleep} sleep_timeout=${L.sleepTimeout} sleeping=${L.sleeping} ble_indicators=${L.ble}`);
          } else {
            if (sub === 'ENABLE') s.led.enabled = args[2].toUpperCase() === 'ON' ? 1 : 0;
            else if (sub === 'BRIGHTNESS') s.led.brightness = num(args[2]);
            else if (sub === 'EFFECT') s.led.effect = ['OFF', 'STATIC', 'RAINBOW', 'BREATHE', 'CUSTOM'].indexOf(args[2].toUpperCase());
            else if (sub === 'COLOR') s.led.color = [num(args[2]), num(args[3]), num(args[4])].map((x) => hex2(x)).join('');
            else if (sub === 'KEYPRESS') s.led.keypress = ['OFF', 'PULSE', 'SPARKLE', 'RIPPLE'].indexOf(args[2].toUpperCase());
            else if (sub === 'SLEEP') s.led.sleep = ['OFF', 'BREATHE', 'RAINBOW'].indexOf(args[2].toUpperCase());
            else if (sub === 'SLEEP_TIMEOUT') s.led.sleepTimeout = num(args[2]);
            else if (sub === 'BLE') s.led.ble = args[2].toUpperCase() === 'ON' ? 1 : 0;
            reply.push(`OK LED ${sub}`);
          }
          break;
        }
        case 'HELLO': reply.push('OK HELLO'); break;
        case 'BLE': reply.push('OK BLE state=DISCONNECTED advertising=1'); break;
        case 'EVENTS': reply.push(`OK EVENTS enabled=${args[1].toUpperCase() === 'ON' ? 1 : 0}`); break;
        default: reply.push(`ERR ${cmd} unknown_command`);
      }
      this.emit(reply);
    }
  }

  const fakeSerial = {
    _port: null,
    async requestPort() {
      if (!this._port) this._port = new MockPort();
      return this._port;
    },
    async getPorts() { return this._port ? [this._port] : []; },
    addEventListener() {},
  };

  Object.defineProperty(navigator, 'serial', { value: fakeSerial, configurable: true });
})();
