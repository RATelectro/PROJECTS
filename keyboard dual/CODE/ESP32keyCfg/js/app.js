/*
 * MultiKey Configurator — application logic.
 *
 * Talks to the keyboard over Web Serial (USB CDC) using the text protocol
 * from SERIAL_PROTOCOL.md. All protocol parsing/model logic lives in
 * js/protocol.js (window.MK).
 */
'use strict';

(function () {
  const MK = window.MK;
  const APP_VERSION = '1.0.0';
  const BACKUP_LIMIT = 100;
  const PROTOCOL_SUPPORTED = 1;

  // ---------------------------------------------------------------------------
  // State
  // ---------------------------------------------------------------------------

  const model = {
    info: null,
    layerCount: 0,
    layerNames: [],
    baseLayer: 0,
    activeLayer: 0,
    holdMs: MK.DEFAULT_HOLD_MS,
    keymaps: [],
    led: null,
  };

  let snapshot = null;          // last state read from the device (diff baseline)
  let connected = false;
  let protocolOk = true;
  let selLayer = 0;

  const ui = { monitorTimer: null, busyDepth: 0 };

  // ---------------------------------------------------------------------------
  // Small DOM helpers
  // ---------------------------------------------------------------------------

  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => Array.from(document.querySelectorAll(sel));

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function esc(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }

  function fmtDate(ms) {
    const d = new Date(ms);
    const p = (n) => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
  }

  function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

  // ---------------------------------------------------------------------------
  // Toasts and log
  // ---------------------------------------------------------------------------

  function toast(msg, kind, durationMs) {
    const t = el('div', 'toast' + (kind ? ' ' + kind : ''), msg);
    $('#toast-root').appendChild(t);
    setTimeout(() => t.remove(), durationMs || (kind === 'error' ? 7000 : 4000));
  }

  const logLines = [];
  function log(text, cls) {
    const time = new Date().toLocaleTimeString('en-GB', { hour12: false });
    logLines.push({ time, text, cls: cls || 'info' });
    if (logLines.length > 500) logLines.shift();
    const pre = $('#log-lines');
    const line = el('div', cls || 'info', `${time}  ${text}`);
    pre.appendChild(line);
    while (pre.childNodes.length > 500) pre.firstChild.remove();
    pre.scrollTop = pre.scrollHeight;
  }

  function setConnText(text, dotCls) {
    $('#conn-text').textContent = text;
    const dot = $('#conn-dot');
    dot.className = 'dot ' + dotCls;
  }

  // ---------------------------------------------------------------------------
  // IndexedDB (backups + presets)
  // ---------------------------------------------------------------------------

  const idb = (() => {
    const DB = 'multikey-configurator';
    let dbPromise = null;

    function open() {
      if (!dbPromise) {
        dbPromise = new Promise((resolve, reject) => {
          const req = indexedDB.open(DB, 1);
          req.onupgradeneeded = () => {
            const db = req.result;
            db.createObjectStore('backups', { keyPath: 'id', autoIncrement: true });
            db.createObjectStore('presets', { keyPath: 'id', autoIncrement: true });
          };
          req.onsuccess = () => resolve(req.result);
          req.onerror = () => reject(req.error);
        });
      }
      return dbPromise;
    }

    async function store(name, mode) {
      const db = await open();
      return db.transaction(name, mode).objectStore(name);
    }

    function reqToPromise(req) {
      return new Promise((resolve, reject) => {
        req.onsuccess = () => resolve(req.result);
        req.onerror = () => reject(req.error);
      });
    }

    return {
      async add(storeName, record) {
        const os = await store(storeName, 'readwrite');
        return reqToPromise(os.add(record));
      },
      async put(storeName, record) {
        const os = await store(storeName, 'readwrite');
        return reqToPromise(os.put(record));
      },
      async all(storeName) {
        const os = await store(storeName, 'readonly');
        return reqToPromise(os.getAll());
      },
      async remove(storeName, id) {
        const os = await store(storeName, 'readwrite');
        return reqToPromise(os.delete(id));
      },
      async clear(storeName) {
        const os = await store(storeName, 'readwrite');
        return reqToPromise(os.clear());
      },
    };
  })();

  async function addBackupRecord(record) {
    await idb.add('backups', record);
    const all = await idb.all('backups');
    all.sort((a, b) => a.createdAt - b.createdAt);
    while (all.length > BACKUP_LIMIT) await idb.remove('backups', all.shift().id);
  }

  // ---------------------------------------------------------------------------
  // Serial client
  // ---------------------------------------------------------------------------

  // Derives the expected first reply line for a command from the command text.
  // Replies that don't match are foreign strays (late replies from lost commands
  // or boot noise) and can never complete the wrong command.
  function expectPattern(cmd) {
    const words = cmd.trim().toUpperCase().split(/\s+/);
    const w0 = words[0];
    if (w0 === 'HELP') return /^OK HELP/;
    if (w0 === 'INFO') return /^OK INFO/;
    if (w0 === 'PING') return /^OK PING/;
    if (w0 === 'BLE') return /^OK BLE/;
    if (w0 === 'DUMP') return /^OK KEYMAP/;
    if (w0 === 'HELLO') return /^OK LED HELLO/;
    if (w0 === 'GET') {
      // Replies drop the GET: GET KEYMAP -> "OK KEYMAP ...", GET KEY -> "OK KEY layer=..."
      const sub = words[1] || '';
      if (sub === 'KEY') return /^OK KEY layer=/;
      return new RegExp('^OK ' + sub.replace(/[.*+?^${}()|[\]\\]/g, '') + '\\b');
    }
    if (w0 === 'SET' || w0 === 'LED') {
      // LED STATUS is the exception: the firmware replies "OK LED enabled=...",
      // while all other LED subcommands reply "OK LED <SUB>".
      if (w0 === 'LED' && words[1] === 'STATUS') return /^OK LED enabled=/;
      return new RegExp('^OK ' + w0 + ' ' + (words[1] || '') + '\\b');
    }
    return new RegExp('^OK ' + w0 + '\\b');
  }

  // For SET KEY commands, the reply echoes layer/index — verify them so a late
  // reply from a previous SET KEY (same shape) can't cascade through the queue.
  function makeVerifyLine(cmd) {
    const m = /^SET KEY (\d+) (\d+)\b/i.exec(cmd.trim());
    if (!m) return null;
    const l = +m[1], i = +m[2];
    return (line) => {
      const r = /^OK SET KEY layer=(\d+) index=(\d+)$/.exec(line);
      return !!r && +r[1] === l && +r[2] === i;
    };
  }

  class SerialClient {
    constructor() {
      this.port = null;
      this.writer = null;
      this.buf = '';
      this.decoder = new TextDecoder();
      this.queue = Promise.resolve();
      this.pending = null;
      this.reading = false;
      this.closing = false;
    }

    attach(port) {
      try { if (this.writer) this.writer.releaseLock(); } catch (_) { /* noop */ }
      this.port = port;
      this.writer = port.writable.getWriter();
      this.buf = '';
      this.closing = false;
    }

    async startReading() {
      this.reading = true;
      while (this.port && this.port.readable) {
        const reader = this.port.readable.getReader();
        try {
          for (;;) {
            const { value, done } = await reader.read();
            if (done) break;
            if (value) this.onBytes(value);
          }
        } catch (err) {
          if (!this.closing) log('Serial read error: ' + err.message, 'err');
        } finally {
          try { reader.releaseLock(); } catch (_) { /* noop */ }
        }
      }
      this.reading = false;
    }

    onBytes(bytes) {
      this.buf += this.decoder.decode(bytes, { stream: true });
      let i;
      while ((i = this.buf.search(/[\r\n]/)) !== -1) {
        const line = this.buf.slice(0, i);
        let rest = this.buf.slice(i + 1);
        if (this.buf[i] === '\r' && rest[0] === '\n') rest = rest.slice(1);
        this.buf = rest;
        if (line.trim().length) this.dispatch(line.trim());
      }
    }

    dispatch(line) {
      if (this.pending) {
        this.pending.onLine(line);
      } else {
        log('← ' + line, 'rx');
      }
    }

    // Waits for the firmware boot banner "OK READY ..." that the keyboard prints
    // when it resets on port open. Resolves true if seen, false on timeout
    // (board was already running — nothing printed).
    waitForBanner(timeoutMs) {
      return new Promise((resolve) => {
        const p = {
          onLine: (line) => {
            if (line.startsWith('EVENT ')) return;
            if (line.startsWith('OK READY')) {
              clearTimeout(timer);
              if (this.pending === p) this.pending = null;
              log('← ' + line, 'rx');
              resolve(true);
              return;
            }
            // Other boot noise before the banner: swallow it.
          },
        };
        const timer = setTimeout(() => {
          if (this.pending === p) this.pending = null;
          resolve(false);
        }, timeoutMs);
        this.pending = p;
      });
    }

    // Sends one command and waits for its reply. Options:
    //   ends: number of `END ...` lines expected (multi-line replies). null = single-line reply.
    //   expect: regex the first reply line must match (defaults to expectPattern(cmd)).
    //   verifyLine: extra check on the final line for single-line replies.
    //   timeoutMs, quiet: suppress rx logging (used by the live monitor).
    send(cmd, opts) {
      const task = () => this.execute(cmd, opts || {});
      this.queue = this.queue.then(task, task);
      return this.queue;
    }

    execute(cmd, opts) {
      const ends = opts.ends === undefined ? null : opts.ends;
      const timeoutMs = opts.timeoutMs || 3000;
      const quiet = !!opts.quiet;
      const expect = opts.expect || expectPattern(cmd);
      const verifyLine = opts.verifyLine || makeVerifyLine(cmd);
      if (!quiet) log('→ ' + cmd, 'tx');

      return new Promise((resolve, reject) => {
        const lines = [];
        let timer = null;
        const finish = (result) => {
          if (timer) clearTimeout(timer);
          this.pending = null;
          if (!quiet) for (const l of lines) log('← ' + l, 'rx');
          resolve(result);
        };
        const fail = (err) => {
          if (timer) clearTimeout(timer);
          this.pending = null;
          reject(err);
        };

        this.pending = {
          onLine: (line) => {
            if (lines.length === 0) {
              if (line.startsWith('ERR')) {
                lines.push(line);
                return fail(new Error(`${cmd}: ${line}`));
              }
              // Drop foreign lines: boot banner leftovers and late replies from
              // previously timed-out commands can never satisfy this command.
              if (!expect.test(line)) return;
              lines.push(line);
              if (ends === null) {
                if (verifyLine && !verifyLine(line)) {
                  lines.pop();
                  return; // right shape, wrong sequence — wait for the real reply
                }
                return finish({ ok: true, lines });
              }
              return; // multi-line reply: keep collecting until enough END lines
            }
            lines.push(line);
            if (ends !== null) {
              const endCount = lines.filter((l) => l.startsWith('END')).length;
              if (endCount >= ends) return finish({ ok: true, lines });
            }
          },
        };

        timer = setTimeout(() => fail(new Error(`Timeout waiting for reply to "${cmd}"`)), timeoutMs);

        this.writer.write(new TextEncoder().encode(cmd + '\n')).catch((err) => {
          fail(new Error('Write failed: ' + err.message));
        });
      });
    }

    // Console use: send a command, collect every line for a fixed window.
    sendRaw(cmd, waitMs) {
      const task = () => new Promise((resolve) => {
        log('→ ' + cmd, 'tx');
        const lines = [];
        const finish = () => {
          this.pending = null;
          for (const l of lines) log('← ' + l, 'rx');
          resolve({ ok: true, lines });
        };
        this.pending = { onLine: (line) => { lines.push(line); } };
        setTimeout(finish, waitMs || 700);
        this.writer.write(new TextEncoder().encode(cmd + '\n')).catch(() => {
          this.pending = null;
          resolve({ ok: false, lines: [] });
        });
      });
      this.queue = this.queue.then(task, task);
      return this.queue;
    }

    async close() {
      const port = this.port;
      this.port = null;
      this.pending = null;
      this.closing = true;
      try { if (this.writer) this.writer.releaseLock(); } catch (_) { /* noop */ }
      this.writer = null;
      try { await port.close(); } catch (_) { /* noop */ }
    }
  }

  const client = new SerialClient();

  // ---------------------------------------------------------------------------
  // Busy overlay / progress
  // ---------------------------------------------------------------------------

  function busyPush(label) {
    ui.busyDepth++;
    $('#busy-label').textContent = label;
    $('#busy-detail').textContent = '';
    $('#busy-bar').removeAttribute('value');
    $('#busy').classList.remove('hidden');
    setConnText(label, 'dot-busy');
  }

  function busyProgress(done, total, detail) {
    const bar = $('#busy-bar');
    if (total > 0) { bar.max = total; bar.value = done; } else bar.removeAttribute('value');
    if (detail !== undefined) $('#busy-detail').textContent = detail;
  }

  function busyPop() {
    ui.busyDepth = Math.max(0, ui.busyDepth - 1);
    if (ui.busyDepth === 0) {
      $('#busy').classList.add('hidden');
      setConnText('Connected', 'dot-on');
    }
  }

  // ---------------------------------------------------------------------------
  // Generic confirm / prompt modals
  // ---------------------------------------------------------------------------

  function uiConfirm(message, okText, danger) {
    return new Promise((resolve) => {
      const root = $('#modal-root');
      root.innerHTML = '';
      const overlay = el('div', 'modal-overlay');
      const box = el('div', 'modal');
      box.style.maxWidth = '460px';
      box.appendChild(el('p', null, message));
      const foot = el('div', 'modal-foot');
      const spacer = el('span', 'spacer');
      const ok = el('button', 'btn ' + (danger ? 'danger' : 'primary'), okText || 'OK');
      const cancel = el('button', 'btn', 'Cancel');
      ok.onclick = () => { root.innerHTML = ''; resolve(true); };
      cancel.onclick = () => { root.innerHTML = ''; resolve(false); };
      foot.append(spacer, cancel, ok);
      box.appendChild(foot);
      overlay.appendChild(box);
      overlay.onclick = (e) => { if (e.target === overlay) { root.innerHTML = ''; resolve(false); } };
      root.appendChild(overlay);
      ok.focus();
    });
  }

  function uiPrompt(message, def) {
    return new Promise((resolve) => {
      const root = $('#modal-root');
      root.innerHTML = '';
      const overlay = el('div', 'modal-overlay');
      const box = el('div', 'modal');
      box.style.maxWidth = '460px';
      box.appendChild(el('p', null, message));
      const input = el('input');
      input.type = 'text';
      input.value = def || '';
      input.style.width = '100%';
      input.maxLength = 60;
      box.appendChild(input);
      const foot = el('div', 'modal-foot');
      const spacer = el('span', 'spacer');
      const ok = el('button', 'btn primary', 'Save');
      const cancel = el('button', 'btn', 'Cancel');
      const done = (v) => { root.innerHTML = ''; resolve(v); };
      ok.onclick = () => done(input.value.trim() || null);
      cancel.onclick = () => done(null);
      input.onkeydown = (e) => { if (e.key === 'Enter') done(input.value.trim() || null); if (e.key === 'Escape') done(null); };
      foot.append(spacer, cancel, ok);
      box.appendChild(foot);
      overlay.appendChild(box);
      root.appendChild(overlay);
      input.focus();
      input.select();
    });
  }

  // ---------------------------------------------------------------------------
  // Connect / read
  // ---------------------------------------------------------------------------

  async function connect() {
    if (!navigator.serial) return;
    let port;
    try {
      port = await navigator.serial.requestPort({ filters: [{ usbVendorId: 0x303a }] });
    } catch (_) {
      return; // picker cancelled or no device selected
    }
    try {
      busyPush('Connecting…');
      // Reopening an already-open port throws; reuse it instead.
      if (!(client.port === port && port.opened)) {
        await port.open({ baudRate: 115200 });
      }
      client.attach(port);
      client.startReading();

      // Opening the port usually resets the ESP32-C3: it boots for ~1 s and
      // prints an "OK READY" banner, and commands sent during boot are lost.
      // Wait for the banner (short wait — if the board was already running it
      // prints nothing), then PING with retries until the board answers.
      const sawBanner = await client.waitForBanner(2000);
      if (sawBanner) log('Keyboard booted, waiting for it to accept commands…', 'info');
      let pong = false;
      for (let attempt = 1; attempt <= 6 && !pong; attempt++) {
        try {
          await client.send('PING', { timeoutMs: 1500 });
          pong = true;
        } catch (err) {
          log(`PING attempt ${attempt}/6: ${err.message}`, 'warn');
          await sleep(400);
        }
      }
      if (!pong) throw new Error('Keyboard is not responding to PING');

      await readAllFromDevice();
      connected = true;
      protocolOk = !model.info || model.info.protocol === PROTOCOL_SUPPORTED;
      $('#protocol-warning').classList.toggle('hidden', protocolOk);
      document.body.classList.remove('offline');
      setConnText('Connected', 'dot-on');
      $('#btn-connect').textContent = 'Disconnect';
      if (model.info) {
        $('#device-summary').textContent =
          `fw ${model.info.version} · protocol ${model.info.protocol} · ` +
          `${model.info.rows}×${model.info.cols} (${model.info.keys} keys) · ` +
          `${model.info.layers}/${model.info.maxLayers} layers`;
      }
      port.addEventListener('disconnect', onDisconnect);
      log('Connected to ' + (model.info ? 'firmware ' + model.info.version : 'keyboard'), 'info');
      if (!protocolOk) log('Unsupported protocol version: ' + model.info.protocol, 'warn');
      renderAll();
      toast('Connected to keyboard', 'success');
    } catch (err) {
      log('Connect failed: ' + err.message, 'err');
      toast('Connect failed: ' + err.message, 'error');
      setConnText('Not connected', 'dot-off');
      // Close the port so the next Connect attempt starts clean.
      await client.close().catch(() => {});
    } finally {
      busyPop();
    }
  }

  async function disconnect() {
    stopMonitor();
    await client.close().catch(() => {});
    onDisconnect();
  }

  function onDisconnect() {
    if (!connected) return;
    connected = false;
    document.body.classList.add('offline');
    setConnText('Disconnected', 'dot-off');
    $('#btn-connect').textContent = 'Connect';
    $('#device-summary').textContent = '';
    log('Disconnected', 'warn');
  }

  function ensureConnected() {
    if (!connected) { toast('Not connected to the keyboard', 'error'); return false; }
    if (!protocolOk) { toast('Unsupported firmware protocol version', 'error'); return false; }
    return true;
  }

  // ---------------------------------------------------------------------------
  // Keymap reading (loss-tolerant)
  // ---------------------------------------------------------------------------
  //
  // Large `GET KEYMAP ALL` replies (~9 KB dumped at once) can lose bytes: the
  // ESP32-C3 USB-CDC TX buffer is small and the firmware prints blocks as fast
  // as it can. Single-line replies (GET KEY, LED STATUS, SET ...) are tiny and
  // safe. So: try the fast full dump first, validate it, retry, then fall back
  // to per-layer reads, and finally to per-key reads.

  function validLayer(layer, keys) {
    return Array.isArray(layer) && layer.length === keys && layer.every((a) => !!a);
  }

  async function readLayerKeyByKey(layer, keys) {
    const out = [];
    for (let i = 0; i < keys; i++) {
      const res = await client.send(`GET KEY ${layer} ${i}`, { timeoutMs: 3000 });
      const k = MK.parseKeyLine(res.lines[0]);
      if (!k || k.layer !== layer || k.index !== i) throw new Error(`GET KEY ${layer} ${i}: unexpected reply`);
      out.push(k.action);
    }
    return out;
  }

  async function readKeymapsRobust(layerCount, keys) {
    // Fast path: one big dump, retried while it fails validation.
    for (let attempt = 1; attempt <= 3; attempt++) {
      try {
        const res = await client.send('GET KEYMAP ALL', { ends: layerCount, timeoutMs: 8000 });
        const keymaps = MK.parseGetKeymap(res.lines, layerCount);
        if (!keymaps.incomplete && keymaps.slice(0, layerCount).every((l) => validLayer(l, keys))) {
          return keymaps;
        }
        log(`GET KEYMAP ALL reply incomplete (attempt ${attempt}/3), retrying…`, 'warn');
      } catch (err) {
        log(`GET KEYMAP ALL attempt ${attempt}/3 failed: ${err.message}`, 'warn');
      }
    }

    // Fallback: per-layer reads (smaller replies), key-by-key as last resort.
    log('Falling back to per-layer keymap reads', 'warn');
    const keymaps = [];
    for (let l = 0; l < layerCount; l++) {
      let layer = null;
      for (let attempt = 1; attempt <= 3 && !layer; attempt++) {
        try {
          const res = await client.send(`GET KEYMAP ${l}`, { ends: 1, timeoutMs: 5000 });
          const parsed = MK.parseGetKeymap(res.lines, l + 1);
          if (!parsed.incomplete && validLayer(parsed[l], keys)) layer = parsed[l];
          else log(`GET KEYMAP ${l} reply incomplete (attempt ${attempt}/3), retrying…`, 'warn');
        } catch (err) {
          log(`GET KEYMAP ${l} attempt ${attempt}/3 failed: ${err.message}`, 'warn');
        }
      }
      if (!layer) {
        log(`Layer ${l + 1}: reading key by key`, 'warn');
        layer = await readLayerKeyByKey(l, keys);
      }
      keymaps.push(layer);
    }
    return keymaps;
  }

  async function readAllFromDevice() {
    const infoRes = await client.send('INFO');
    const info = MK.parseInfo(infoRes.lines[0]);
    if (!info) throw new Error('Unexpected INFO reply');

    const layersRes = await client.send('GET LAYERS', { ends: 1 });
    const layers = MK.parseGetLayers(layersRes.lines);
    if (!layers) throw new Error('Unexpected GET LAYERS reply');

    const keymaps = await readKeymapsRobust(layers.count, info.keys);

    let hold = MK.DEFAULT_HOLD_MS;
    try { hold = MK.parseHold((await client.send('GET HOLD')).lines[0]) ?? hold; } catch (_) { /* keep default */ }

    let led = null;
    try { led = MK.parseLedStatus((await client.send('LED STATUS', { timeoutMs: 5000 })).lines[0]); } catch (_) { /* optional */ }

    model.info = info;
    model.layerCount = layers.count;
    model.layerNames = layers.names.slice(0, layers.count);
    model.baseLayer = layers.base;
    model.activeLayer = layers.active;
    model.holdMs = hold;
    model.keymaps = keymaps;
    model.led = led;

    // grow local storage to the device maximum so higher layers can be edited
    for (let l = model.layerCount; l < info.maxLayers; l++) {
      model.keymaps[l] = MK.emptyLayer(info.keys);
      model.layerNames[l] = `layer ${l + 1}`;
    }
    snapshot = snapshotFromModel();
    if (selLayer >= model.layerCount) selLayer = 0;
  }

  function snapshotFromModel() {
    return {
      layerCount: model.layerCount,
      layerNames: model.layerNames.slice(),
      baseLayer: model.baseLayer,
      holdMs: model.holdMs,
      keymaps: MK.clone(model.keymaps),
    };
  }

  async function readFromKeyboard() {
    if (!ensureConnected()) return;
    busyPush('Reading keymap…');
    try {
      await readAllFromDevice();
      renderAll();
      toast('Keymap read from keyboard', 'success');
    } catch (err) {
      log('Read failed: ' + err.message, 'err');
      toast('Read failed: ' + err.message, 'error');
    } finally {
      busyPop();
    }
  }

  // ---------------------------------------------------------------------------
  // Flash flow (with mandatory backup)
  // ---------------------------------------------------------------------------

  async function createBackupOnDevice() {
    busyProgress(0, 0, 'Dumping current keymap…');
    const layersRes = await client.send('GET LAYERS', { ends: 1 });
    const layers = MK.parseGetLayers(layersRes.lines);
    const keymaps = await readKeymapsRobust(layers.count, model.info ? model.info.keys : 35);
    const hold = MK.parseHold((await client.send('GET HOLD')).lines[0]);
    let led = null;
    try { led = MK.parseLedStatus((await client.send('LED STATUS', { timeoutMs: 5000 })).lines[0]); } catch (_) { /* optional */ }
    return MK.makeBackupRecord({
      info: model.info,
      layerCount: layers.count,
      layerNames: layers.names.slice(0, layers.count),
      baseLayer: layers.base,
      holdMs: hold == null ? MK.DEFAULT_HOLD_MS : hold,
      keymaps,
      led,
    });
  }

  // Flashes `target` (a model-like object) to the keyboard.
  // Creates a backup first — never flashes without one.
  async function flashModel(target, purpose) {
    busyPush(purpose || 'Flashing keymap…');
    try {
      const diff = MK.computeDiff(snapshot, target);
      if (diff.errors.length) {
        toast(diff.errors[0], 'error');
        log('Validation failed: ' + diff.errors.join(' | '), 'err');
        return false;
      }
      if (!diff.hasChanges) {
        toast('Nothing to write — keymap already matches');
        return true;
      }

      // 1. Mandatory backup of the current keymap before flashing.
      busyProgress(0, 0, 'Creating backup…');
      const backup = await createBackupOnDevice();
      await addBackupRecord(backup);
      log(`Backup created (${fmtDate(backup.createdAt)})`, 'info');
      renderBackups();

      // 2. Apply changes.
      let total = diff.keyChanges.length + diff.names.length + (diff.holdMs !== null ? 1 : 0) +
                  (diff.baseLayer !== null ? 1 : 0) + 2; // + verify + save
      if (diff.layerCountChanged) total += 2;            // count cmd + metadata re-read
      let done = 0;

      if (diff.layerCountChanged) {
        busyProgress(++done, total, `Enabling ${target.layerCount} layer(s)…`);
        await client.send(`SET LAYER_COUNT ${target.layerCount}`);
        // Newly enabled layers reveal names stored on the device — sync them.
        const layersRes = await client.send('GET LAYERS', { ends: 1 });
        const layers = MK.parseGetLayers(layersRes.lines);
        for (let l = 0; l < layers.count; l++) target.layerNames[l] = layers.names[l];
        snapshot.layerNames = layers.names.slice(0, layers.count);
        snapshot.layerCount = layers.count;
      }

      for (const c of diff.keyChanges) {
        busyProgress(++done, total, `Key ${c.index + 1}/${target.keymaps[c.layer].length} of layer ${c.layer + 1}`);
        await client.send(`SET KEY ${c.layer} ${c.index} ${MK.serializeAction(c.action)}`);
      }
      for (const n of diff.names) {
        busyProgress(++done, total, `Naming layer ${n.layer + 1}`);
        await client.send(`SET LAYER_NAME ${n.layer} ${MK.sanitizeLayerName(n.name)}`);
      }
      if (diff.holdMs !== null) {
        busyProgress(++done, total, 'Hold threshold…');
        await client.send(`SET HOLD ${diff.holdMs}`);
      }
      if (diff.baseLayer !== null) {
        busyProgress(++done, total, 'Base layer…');
        await client.send(`SET LAYER ${diff.baseLayer}`);
      }

      // 3. Verify before saving.
      busyProgress(++done, total, 'Verifying…');
      const written = await readKeymapsRobust(target.layerCount, target.keymaps[0] ? target.keymaps[0].length : 35);
      let mismatch = null;
      for (let l = 0; l < target.layerCount && !mismatch; l++) {
        for (let i = 0; i < target.keymaps[l].length && !mismatch; i++) {
          if (!written[l] || !MK.actionsEqual(written[l][i], target.keymaps[l][i])) {
            mismatch = `Layer ${l + 1}, key ${i + 1}: device=${written[l] ? MK.serializeAction(written[l][i]) : '(no data)'} expected=${MK.serializeAction(target.keymaps[l][i])}`;
          }
        }
      }
      if (mismatch) {
        log('Verify failed: ' + mismatch, 'err');
        toast('Verify failed (' + mismatch.slice(0, 140) + ') — changes left unsaved in RAM. Use RECALL ALL to revert.', 'error', 10000);
        return false;
      }

      // 4. Persist.
      busyProgress(++done, total, 'Saving to flash…');
      await client.send('SAVE ALL', { timeoutMs: 10000 });
      const dirty = MK.parseDirty((await client.send('GET DIRTY')).lines[0]);
      if (dirty && (dirty.mask !== 0 || dirty.metadata)) {
        log(`Warning: device still reports unsaved changes (mask=0x${dirty.mask.toString(16)})`, 'warn');
      }

      // 5. Re-sync local state from the device.
      await readAllFromDevice();
      renderAll();
      log('Keymap saved to flash', 'info');
      toast('Keymap flashed and saved', 'success');
      return true;
    } catch (err) {
      log('Flash failed: ' + err.message, 'err');
      toast('Flash failed: ' + err.message, 'error');
      return false;
    } finally {
      busyPop();
    }
  }

  async function applyToKeyboard() {
    if (!ensureConnected()) return;
    await flashModel(model, 'Flashing keymap…');
  }

  // ---------------------------------------------------------------------------
  // Keymap tab rendering
  // ---------------------------------------------------------------------------

  function markDirtyAndRender() {
    renderDirty();
    renderLayerTabs();
    renderGrid();
  }

  function localDiff() {
    if (!snapshot) return null;
    return MK.computeDiff(snapshot, model);
  }

  function renderDirty() {
    const d = localDiff();
    const status = $('#dirty-status');
    const applyBtn = $('#btn-apply');
    if (!d || !d.hasChanges) {
      status.textContent = '';
      applyBtn.disabled = !connected;
      return;
    }
    const parts = [];
    if (d.layerCountChanged) parts.push(`layers ${snapshot.layerCount}→${d.layerCount}`);
    if (d.keyChanges.length) parts.push(`${d.keyChanges.length} key(s)`);
    if (d.names.length) parts.push(`${d.names.length} name(s)`);
    if (d.holdMs !== null) parts.push('hold threshold');
    if (d.baseLayer !== null) parts.push('base layer');
    status.textContent = `Unsaved changes: ${parts.join(', ')}`;
    applyBtn.disabled = !connected;
  }

  function renderLayerTabs() {
    const tabs = $('#layer-tabs');
    tabs.innerHTML = '';
    for (let l = 0; l < model.layerCount; l++) {
      const b = el('button', 'layer-tab' + (l === selLayer ? ' selected' : ''));
      b.appendChild(el('span', null, `${l + 1} ${model.layerNames[l] || ''}`));
      if (l === model.baseLayer) b.appendChild(el('span', 'base-mark', '●base'));
      b.title = `Layer ${l + 1}${l === model.baseLayer ? ' (base layer)' : ''}`;
      b.onclick = () => { selLayer = l; renderKeymapTab(); };
      tabs.appendChild(b);
    }
    $('#btn-add-layer').disabled = !connected || model.layerCount >= (model.info ? model.info.maxLayers : MK.MAX_LAYERS);
    $('#btn-remove-layer').disabled = !connected || model.layerCount <= 1;
    $('#layer-name').value = model.layerNames[selLayer] || '';
    const baseSel = $('#base-layer');
    baseSel.innerHTML = '';
    for (let l = 0; l < model.layerCount; l++) {
      const o = el('option', null, `${l + 1} — ${model.layerNames[l] || ''}`);
      o.value = l;
      baseSel.appendChild(o);
    }
    baseSel.value = model.baseLayer;
    $('#hold-ms').value = model.holdMs;
  }

  function resolveGhost(layer, index) {
    for (let l = layer - 1; l >= 0; l--) {
      const a = model.keymaps[l] && model.keymaps[l][index];
      if (a && a.t !== 'transparent') return MK.actionLabel(a).main;
    }
    return '';
  }

  // Keys are drawn in the physical orientation from the schematic: 7 columns
  // (matrix rows) wide by 5 rows (matrix cols) tall, numbered 1..35
  // left-to-right top-to-bottom. Picture position n (0-based) maps to firmware
  // index i as i = (n % rows) * cols + floor(n / rows).
  function firmwareIndex(n, rows, cols) {
    return (n % rows) * cols + Math.floor(n / rows);
  }

  function renderGrid() {
    const matrix = $('#matrix');
    matrix.innerHTML = '';
    const keys = model.info ? model.info.keys : 35;
    const rows = model.info ? model.info.rows : 7;
    const cols = model.info ? model.info.cols : 5;
    matrix.style.gridTemplateColumns = `repeat(${rows}, minmax(62px, 76px))`;
    const layer = model.keymaps[selLayer] || [];
    $('#matrix-title').textContent = `Layer ${selLayer + 1} — “${model.layerNames[selLayer] || ''}” (${keys} keys)`;
    for (let n = 0; n < keys; n++) {
      const i = firmwareIndex(n, rows, cols);
      const a = layer[i] || MK.transparentAction();
      const lab = MK.actionLabel(a);
      const b = el('button', 'kb-key ' + lab.cls);
      b.appendChild(el('span', 'k-pos', String(n + 1)));
      b.appendChild(el('span', 'k-main', lab.main));
      const sub = el('span', 'k-sub');
      if (a.t === 'transparent') {
        const ghost = resolveGhost(selLayer, i);
        sub.textContent = ghost ? `↳ ${ghost}` : 'transparent';
        sub.classList.add('k-ghost');
      } else {
        sub.textContent = lab.sub;
      }
      b.appendChild(sub);
      b.title = `Key ${n + 1} · matrix row ${Math.floor(i / cols) + 1}, col ${(i % cols) + 1} (index ${i})\n${MK.actionDescription(a)}`;
      b.onclick = () => openKeyModal(i);
      matrix.appendChild(b);
    }
  }

  function renderKeymapTab() {
    renderLayerTabs();
    renderGrid();
    renderDirty();
  }

  // ---------------------------------------------------------------------------
  // Key editor modal
  // ---------------------------------------------------------------------------

  let kmState = null;

  function openKeyModal(index) {
    const current = model.keymaps[selLayer][index];
    kmState = {
      index,
      type: current.t === 'hid' && current.u === 0 && current.m === 0 ? 'none'
          : current.t === 'hid' ? 'hid'
          : current.t,
      u: current.t === 'hid' ? current.u : 0x04,
      m: current.t === 'hid' ? current.m : 0,
      target: current.t === 'layer' ? current.target : (selLayer + 1 < model.layerCount ? selLayer + 1 : 0),
      mode: current.t === 'layer' ? current.mode : 'MOMENTARY',
      cat: 'chars',
      search: '',
    };
    renderKeyModal();
  }

  function renderKeyModal() {
    const root = $('#modal-root');
    root.innerHTML = '';
    const s = kmState;
    const overlay = el('div', 'modal-overlay');
    const box = el('div', 'modal');
    const rows = model.info ? model.info.rows : 7;
    const cols = model.info ? model.info.cols : 5;
    const pic = (s.index % cols) * rows + Math.floor(s.index / cols) + 1;
    box.appendChild(el('h3', null, `Key ${pic} · layer ${selLayer + 1}`));

    const seg = el('div', 'seg');
    for (const [id, label] of [['hid', 'Key (HID)'], ['layer', 'Layer'], ['transparent', 'Transparent'], ['none', 'Disabled']]) {
      const b = el('button', 'seg-btn' + (s.type === id ? ' selected' : ''), label);
      b.onclick = () => { s.type = id; renderKeyModal(); };
      seg.appendChild(b);
    }
    box.appendChild(seg);

    const body = el('div');
    if (s.type === 'hid' || s.type === 'none') {
      if (s.type === 'none') {
        body.appendChild(el('div', 'km-note', 'This key does nothing on this layer. It will not fall through to lower layers.'));
      } else {
        body.appendChild(renderKeyPicker(s));
      }
    } else if (s.type === 'layer') {
      body.appendChild(renderLayerPicker(s));
    } else {
      body.appendChild(el('div', 'km-note',
        'Transparent: this key is inherited from the highest lower layer that defines it. Use this to build overlay layers.'));
    }
    box.appendChild(body);

    const foot = el('div', 'modal-foot');
    const preview = el('span', 'preview');
    preview.textContent = 'Result: ' + previewText();
    const spacer = el('span', 'spacer');
    const cancel = el('button', 'btn', 'Cancel');
    cancel.onclick = () => { root.innerHTML = ''; };
    const save = el('button', 'btn primary', 'Save');
    save.onclick = saveKeyModal;
    foot.append(preview, spacer, cancel, save);
    box.appendChild(foot);
    overlay.appendChild(box);
    overlay.onclick = (e) => { if (e.target === overlay) root.innerHTML = ''; };
    root.appendChild(overlay);
  }

  function previewText() {
    const s = kmState;
    if (s.type === 'transparent') return 'TRANSPARENT';
    if (s.type === 'none') return 'HID 0 0 (disabled)';
    if (s.type === 'layer') return `LAYER ${s.target} ${s.mode} → layer ${s.target + 1}`;
    return `HID usage 0x${s.u.toString(16).toUpperCase()} mods 0x${s.m.toString(16).toUpperCase()}` +
      (s.m ? ` (${MK.modsLabel(s.m)} + ${MK.usageInfo(s.u).label})` : ` (${MK.usageInfo(s.u).label})`);
  }

  function renderKeyPicker(s) {
    const wrap = el('div');
    const search = el('input');
    search.className = 'km-search';
    search.placeholder = 'Search keys…';
    search.value = s.search;
    const cats = el('div', 'km-cats');
    const keys = el('div', 'km-keys');

    function paintKeys() {
      keys.innerHTML = '';
      const q = s.search.trim().toLowerCase();
      const list = MK.HID_TABLE.filter((k) => {
        if (q) return k.label.toLowerCase().includes(q) || k.u.toString(16).includes(q);
        return k.cat === s.cat;
      });
      for (const k of list) {
        const b = el('button', 'km-key-btn' + (s.u === k.u ? ' selected' : ''));
        b.appendChild(el('span', null, k.label));
        b.appendChild(el('small', null, k.sub ? k.sub : '0x' + k.u.toString(16).toUpperCase()));
        b.onclick = () => { s.u = k.u; s.type = 'hid'; paintKeys(); updatePreview(); };
        keys.appendChild(b);
      }
      if (!list.length) keys.appendChild(el('div', 'km-note', 'No matches'));
    }

    for (const c of MK.KEY_CATEGORIES) {
      const b = el('button', 'cat-btn' + (s.cat === c.id && !s.search ? ' selected' : ''), c.label);
      b.onclick = () => { s.cat = c.id; s.search = ''; search.value = ''; paintKeys(); paintCats(); };
      b.dataset.cat = c.id;
      cats.appendChild(b);
    }
    function paintCats() {
      for (const b of cats.childNodes) b.classList.toggle('selected', b.dataset.cat === s.cat && !s.search);
    }

    search.oninput = () => { s.search = search.value; paintKeys(); paintCats(); };
    paintKeys();

    const mods = el('div', 'km-mods');
    for (const mod of MK.MODS) {
      const label = el('label');
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = (s.m & mod.bit) !== 0;
      cb.onchange = () => { s.m = cb.checked ? (s.m | mod.bit) : (s.m & ~mod.bit); updatePreview(); };
      label.append(cb, el('span', null, mod.name));
      mods.appendChild(label);
    }

    wrap.append(search, cats, keys, mods);
    return wrap;
  }

  function renderLayerPicker(s) {
    const wrap = el('div');
    const row = el('div', 'km-field-row');
    row.appendChild(el('span', null, 'Target layer:'));
    const sel = document.createElement('select');
    for (let l = 0; l < model.layerCount; l++) {
      const o = el('option', null, `${l + 1} — ${model.layerNames[l] || ''}`);
      o.value = l;
      sel.appendChild(o);
    }
    sel.value = s.target;
    sel.onchange = () => { s.target = +sel.value; updatePreview(); };
    row.appendChild(sel);
    wrap.appendChild(row);

    const modes = el('div', 'km-modes');
    const modeInfo = [
      ['MOMENTARY', 'Momentary', 'Layer is active only while the key is held.'],
      ['SWITCH', 'Switch', 'Press sets the base layer permanently.'],
      ['MODAL', 'Modal', 'Press toggles between the target and the previous layer.'],
    ];
    for (const [id, name, desc] of modeInfo) {
      const d = el('div', 'km-mode' + (s.mode === id ? ' selected' : ''));
      d.appendChild(el('b', null, name));
      d.appendChild(el('span', null, desc));
      d.onclick = () => { s.mode = id; renderKeyModal(); };
      modes.appendChild(d);
    }
    wrap.appendChild(modes);
    return wrap;
  }

  function updatePreview() {
    const preview = $('#modal-root .preview');
    if (preview) preview.textContent = 'Result: ' + previewText();
  }

  function saveKeyModal() {
    const s = kmState;
    let action;
    if (s.type === 'transparent') action = MK.transparentAction();
    else if (s.type === 'none') action = MK.hidAction(0, 0);
    else if (s.type === 'layer') action = MK.layerAction(s.target, s.mode);
    else action = MK.hidAction(s.u, s.m);
    model.keymaps[selLayer][s.index] = action;
    $('#modal-root').innerHTML = '';
    markDirtyAndRender();
  }

  // ---------------------------------------------------------------------------
  // LED tab
  // ---------------------------------------------------------------------------

  function fillSelect(sel, names) {
    sel.innerHTML = '';
    for (const n of names) sel.appendChild(el('option', null, n));
  }

  function renderLedTab() {
    fillSelect($('#led-effect'), MK.LED_EFFECTS);
    fillSelect($('#led-keypress'), MK.KEYPRESS_ANIMS);
    fillSelect($('#led-sleep'), MK.SLEEP_ANIMS);
    paintLed();
  }

  function paintLed() {
    const led = model.led;
    const has = !!led;
    $('#led-enabled').checked = has && led.enabled;
    $('#led-brightness').value = has ? led.brightness : 50;
    $('#led-brightness-val').textContent = has ? led.brightness : '—';
    $('#led-effect').value = has ? led.effect : 'OFF';
    $('#led-color').value = has ? led.color : '#021400';
    $('#led-keypress').value = has ? led.keypress : 'OFF';
    $('#led-sleep').value = has ? led.sleep : 'BREATHE';
    $('#led-sleep-timeout').value = has ? led.sleepTimeoutMs : 0;
    $('#led-ble').checked = has && led.bleIndicators;
    $('#led-flags').textContent = has
      ? (led.sleeping ? 'LEDs are currently asleep (idle).' : 'LEDs are awake.') +
        (led.effect === 'CUSTOM' ? ' Base effect CUSTOM is set via per-key LED commands.' : '')
      : 'Not read yet — press “Read from keyboard”.';
  }

  async function ledCommand(cmd, update) {
    if (!connected) { toast('Not connected', 'error'); renderLedTab(); return; }
    try {
      await client.send(cmd);
      if (update && model.led) Object.assign(model.led, update);
      paintLed();
    } catch (err) {
      log('LED command failed: ' + err.message, 'err');
      toast('LED command failed: ' + err.message, 'error');
    }
  }

  async function readLed() {
    if (!connected) { toast('Not connected', 'error'); return; }
    try {
      model.led = MK.parseLedStatus((await client.send('LED STATUS')).lines[0]);
      paintLed();
    } catch (err) {
      toast('LED STATUS failed: ' + err.message, 'error');
    }
  }

  // ---------------------------------------------------------------------------
  // Device tab
  // ---------------------------------------------------------------------------

  function renderDeviceTab() {
    const t = $('#info-table');
    t.innerHTML = '';
    const info = model.info;
    const rows = info
      ? [['Firmware version', info.version], ['Protocol version', info.protocol + (protocolOk ? '' : '  (unsupported)')],
         ['Matrix', `${info.rows} rows × ${info.cols} cols`], ['Keys', info.keys],
         ['Layers enabled', `${info.layers} of ${info.maxLayers} max`],
         ['NKRO reports', info.nkro ? 'yes' : 'no (6KRO)'], ['NVS storage', info.storage ? 'available' : 'missing']]
      : [['Firmware version', '—']];
    for (const [k, v] of rows) {
      const tr = el('tr');
      tr.appendChild(el('td', null, k));
      tr.appendChild(el('td', null, String(v)));
      t.appendChild(tr);
    }
    renderMonitorGrid([]);
  }

  function renderMonitorGrid(pressedIndices) {
    const g = $('#monitor-grid');
    const info = model.info;
    if (!info) { g.innerHTML = ''; return; }
    if (!g.childNodes.length) {
      // Same physical orientation as the keymap grid: picture keys 1..35
      // left-to-right top-to-bottom, each cell remembering its firmware index.
      g.style.gridTemplateColumns = `repeat(${info.rows}, 44px)`;
      for (let n = 0; n < info.keys; n++) {
        const d = el('div', 'mon-key', String(n + 1));
        d.dataset.index = firmwareIndex(n, info.rows, info.cols);
        g.appendChild(d);
      }
    }
    const set = new Set(pressedIndices);
    for (const child of g.childNodes) {
      child.classList.toggle('pressed', set.has(Number(child.dataset.index)));
    }
  }

  function startMonitor() {
    if (ui.monitorTimer) return;
    ui.monitorTimer = setInterval(async () => {
      if (!connected || ui.busyDepth > 0) return;
      try {
        const res = await client.send('GET STATE', { quiet: true, timeoutMs: 1500 });
        const st = MK.parseState(res.lines[0]);
        if (st) {
          renderMonitorGrid(st.indices);
          $('#monitor-layer').textContent =
            `Active layer: ${st.active + 1} · base layer: ${st.base + 1} · BLE: ${st.ble}`;
        }
      } catch (_) { /* transient */ }
    }, 150);
  }

  function stopMonitor() {
    if (ui.monitorTimer) { clearInterval(ui.monitorTimer); ui.monitorTimer = null; }
    $('#monitor-toggle').checked = false;
    renderMonitorGrid([]);
    $('#monitor-layer').textContent = '';
  }

  async function refreshBle() {
    if (!connected) return;
    try {
      const st = MK.parseBleStatus((await client.send('BLE STATUS')).lines[0]);
      $('#ble-status').textContent = st ? `state: ${st.state}, advertising: ${st.advertising ? 'yes' : 'no'}` : '—';
    } catch (err) {
      $('#ble-status').textContent = 'BLE STATUS failed';
    }
  }

  // ---------------------------------------------------------------------------
  // Presets tab
  // ---------------------------------------------------------------------------

  function download(filename, text) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    a.download = filename;
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
  }

  function presetSummary(rec) {
    if (rec.kind === 'layer') return `layer · ${rec.data.keys.length} keys`;
    const km = MK.normalizeKeymapData(rec.data, model.info ? model.info.keys : 35);
    return km.ok ? `keymap · ${km.value.layerCount} layer(s)` : 'keymap · (invalid)';
  }

  async function renderPresets() {
    const list = $('#preset-list');
    list.innerHTML = '';
    const factory = el('div', 'list-item');
    factory.appendChild(el('span', 'li-icon', '🏭'));
    factory.appendChild(el('span', 'li-name', 'Factory defaults'));
    factory.appendChild(el('span', 'li-meta', 'built-in · keymap · 4 layers'));
    const factActions = el('div', 'li-actions');
    const factApply = el('button', 'btn small', 'Apply');
    factApply.disabled = !connected;
    factApply.onclick = async () => {
      if (!(await uiConfirm('Replace the whole local keymap with the compiled factory defaults?', 'Apply'))) return;
      const f = MK.factoryKeymap(model.info ? model.info.keys : 35);
      applyKeymapData(f, 'Factory defaults');
    };
    factActions.appendChild(factApply);
    factory.appendChild(factActions);
    list.appendChild(factory);

    const recs = await idb.all('presets');
    recs.sort((a, b) => b.createdAt - a.createdAt);
    if (!recs.length) {
      list.appendChild(el('div', 'list-empty', 'No presets yet — save the current layer or keymap above.'));
      return;
    }
    for (const rec of recs) {
      const item = el('div', 'list-item');
      item.appendChild(el('span', 'li-icon', rec.kind === 'layer' ? '▤' : '⌨'));
      item.appendChild(el('span', 'li-name', rec.name + (rec.multikey ? ' [MK]' : '')));
      item.appendChild(el('span', 'li-meta', `${presetSummary(rec)} · ${fmtDate(rec.createdAt)}`));
      const actions = el('div', 'li-actions');
      const apply = el('button', 'btn small', 'Apply');
      apply.disabled = !connected;
      apply.onclick = () => applyPreset(rec);
      const exportBtn = el('button', 'btn small', 'Export');
      exportBtn.onclick = () => download(`multikey-preset-${rec.name.replace(/[^\w-]+/g, '_')}.json`, JSON.stringify(rec, null, 2));
      const del = el('button', 'btn small danger', 'Delete');
      del.onclick = async () => {
        if (!(await uiConfirm(`Delete preset “${rec.name}”?`, 'Delete', true))) return;
        await idb.remove('presets', rec.id);
        renderPresets();
      };
      actions.append(apply, exportBtn, del);
      item.appendChild(actions);
      list.appendChild(item);
    }
  }

  function applyKeymapData(data, label) {
    model.layerCount = data.layerCount;
    model.layerNames = data.layerNames.slice();
    model.holdMs = data.holdMs;
    model.keymaps = MK.clone(data.keymaps);
    if (selLayer >= model.layerCount) selLayer = 0;
    renderKeymapTab();
    toast(`“${label}” loaded into the editor — press “Apply to keyboard” to flash it`, 'success');
    log(`Preset “${label}” loaded into editor`, 'info');
  }

  async function applyPreset(rec) {
    const keys = model.info ? model.info.keys : 35;
    if (rec.kind === 'layer') {
      const norm = MK.normalizeLayerData(rec.data, keys);
      if (!norm.ok) { toast('Invalid layer preset: ' + norm.error, 'error'); return; }
      model.keymaps[selLayer] = norm.value.keys;
      markDirtyAndRender();
      toast(`Layer preset “${rec.name}” applied to layer ${selLayer + 1}`, 'success');
    } else {
      if (!(await uiConfirm(`Replace the whole local keymap with preset “${rec.name}”?`, 'Apply'))) return;
      const norm = MK.normalizeKeymapData(rec.data, keys, model.info ? model.info.maxLayers : MK.MAX_LAYERS);
      if (!norm.ok) { toast('Invalid keymap preset: ' + norm.error, 'error'); return; }
      applyKeymapData(norm.value, rec.name);
    }
  }

  async function savePreset(kind) {
    const name = await uiPrompt(
      kind === 'layer' ? `Preset name (layer ${selLayer + 1} — “${model.layerNames[selLayer] || ''}”):` : 'Preset name (whole keymap):',
      kind === 'layer' ? model.layerNames[selLayer] || '' : 'My keymap');
    if (!name) return;
    const data = kind === 'layer'
      ? { name: MK.sanitizeLayerName(model.layerNames[selLayer]), keys: MK.clone(model.keymaps[selLayer]) }
      : {
          layerCount: model.layerCount,
          layerNames: model.layerNames.slice(0, model.layerCount),
          holdMs: model.holdMs,
          keymaps: MK.clone(model.keymaps.slice(0, model.layerCount)),
        };
    await idb.add('presets', { kind, name, createdAt: Date.now(), multikey: !!document.querySelector('#preset-multikey')?.checked, data });
    renderPresets();
    toast(`Preset “${name}” saved`, 'success');
  }

  async function importPresets(file) {
    let json;
    try { json = JSON.parse(await file.text()); } catch (_) { toast('Not a valid JSON file', 'error'); return; }
    const recs = [];
    if (json && json.kind === 'preset') recs.push(json);
    else if (json && Array.isArray(json.presets)) recs.push(...json.presets.filter((p) => p && p.kind));
    else if (json && (json.kind === 'backup' || json.keymaps)) {
      // A backup file or plain keymap data: import as a full keymap preset.
      const norm = MK.normalizeKeymapData(json, model.info ? model.info.keys : 35);
      if (!norm.ok) { toast('Import failed: ' + norm.error, 'error'); return; }
      recs.push({ kind: 'keymap', name: json.kind === 'backup' ? `Backup ${fmtDate(json.createdAt || Date.now())}` : file.name, data: norm.value });
    }
    if (!recs.length) { toast('No presets found in this file', 'error'); return; }
    for (const r of recs) {
      await idb.add('presets', { kind: r.kind, name: String(r.name || 'imported'), createdAt: r.createdAt || Date.now(), data: r.data });
    }
    renderPresets();
    toast(`Imported ${recs.length} preset(s)`, 'success');
  }

  // ---------------------------------------------------------------------------
  // Backups tab
  // ---------------------------------------------------------------------------

  async function renderBackups() {
    const list = $('#backup-list');
    list.innerHTML = '';
    const recs = await idb.all('backups');
    recs.sort((a, b) => b.createdAt - a.createdAt);
    $('#backup-count').textContent = `${recs.length} / ${BACKUP_LIMIT} backups`;
    if (!recs.length) {
      list.appendChild(el('div', 'list-empty', 'No backups yet — one is created automatically before every flash.'));
      return;
    }
    for (const rec of recs) {
      const item = el('div', 'list-item');
      item.appendChild(el('span', 'li-icon', '💾'));
      item.appendChild(el('span', 'li-name', fmtDate(rec.createdAt)));
      item.appendChild(el('span', 'li-meta',
        `fw ${rec.device && rec.device.version ? rec.device.version : '?'} · ${rec.layerCount} layer(s) · ` +
        `${rec.keymaps ? rec.keymaps.length : 0} layer dump · hold ${rec.holdMs} ms`));
      const actions = el('div', 'li-actions');
      const restore = el('button', 'btn small primary', 'Restore');
      restore.disabled = !connected;
      restore.onclick = () => restoreBackup(rec);
      const exportBtn = el('button', 'btn small', 'Export');
      exportBtn.onclick = () =>
        download(`multikey-backup-${new Date(rec.createdAt).toISOString().replace(/[:.]/g, '-')}.json`, JSON.stringify(rec, null, 2));
      const del = el('button', 'btn small danger', 'Delete');
      del.onclick = async () => {
        if (!(await uiConfirm(`Delete backup from ${fmtDate(rec.createdAt)}?`, 'Delete', true))) return;
        await idb.remove('backups', rec.id);
        renderBackups();
      };
      actions.append(restore, exportBtn, del);
      item.appendChild(actions);
      list.appendChild(item);
    }
  }

  async function restoreBackup(rec) {
    if (!ensureConnected()) return;
    if (!(await uiConfirm(
      `Restore the keymap backed up at ${fmtDate(rec.createdAt)}?\nThe current keymap will be backed up first.`,
      'Restore', false))) return;
    const keys = model.info ? model.info.keys : 35;
    const norm = MK.normalizeKeymapData(rec, keys, model.info ? model.info.maxLayers : MK.MAX_LAYERS);
    if (!norm.ok) { toast('Backup is not valid for this keyboard: ' + norm.error, 'error'); return; }
    const target = {
      layerCount: norm.value.layerCount,
      layerNames: norm.value.layerNames,
      holdMs: norm.value.holdMs,
      baseLayer: rec.baseLayer !== undefined && rec.baseLayer < norm.value.layerCount ? rec.baseLayer : 0,
      keymaps: MK.clone(model.keymaps), // base for layers above the backup's count
    };
    for (let l = 0; l < norm.value.layerCount; l++) target.keymaps[l] = norm.value.keymaps[l];
    const okFlashed = await flashModel(target, 'Restoring backup…');
    if (okFlashed) {
      for (let l = 0; l < norm.value.layerCount; l++) model.keymaps[l] = MK.clone(norm.value.keymaps[l]);
      renderAll();
    }
  }

  async function exportAllBackups() {
    const recs = await idb.all('backups');
    if (!recs.length) { toast('No backups to export'); return; }
    recs.sort((a, b) => a.createdAt - b.createdAt);
    download('multikey-backups.json', JSON.stringify({ kind: 'backup-bundle', backups: recs }, null, 2));
  }

  // ---------------------------------------------------------------------------
  // Wiring
  // ---------------------------------------------------------------------------

  function renderAll() {
    renderKeymapTab();
    renderLedTab();
    renderDeviceTab();
    renderPresets();
    renderBackups();
    refreshBle();
  }

  function wire() {
    $('#app-version').textContent = 'v' + APP_VERSION;

    if (!navigator.serial) $('#context-warning').classList.remove('hidden');

    $('#btn-connect').onclick = () => (connected ? disconnect() : connect());
    $('#log-toggle').onclick = () => $('#log-panel').classList.toggle('collapsed');
    $('#log-clear').onclick = () => { $('#log-lines').innerHTML = ''; logLines.length = 0; };
    $('#console-form').onsubmit = (e) => {
      e.preventDefault();
      const input = $('#console-input');
      const cmd = input.value.trim();
      if (!cmd) return;
      input.value = '';
      if (!connected) { toast('Not connected', 'error'); return; }
      client.sendRaw(cmd).catch((err) => log('Console: ' + err.message, 'err'));
    };

    for (const tab of $$('#tabs .tab')) {
      tab.onclick = () => {
        $$('#tabs .tab').forEach((t) => t.classList.toggle('active', t === tab));
        $$('.tab-page').forEach((p) => p.classList.toggle('active', p.id === 'tab-' + tab.dataset.tab));
        if (tab.dataset.tab === 'led' && connected && !model.led) readLed();
        if (tab.dataset.tab === 'device' && connected) refreshBle();
        if (tab.dataset.tab === 'presets') renderPresets();
        if (tab.dataset.tab === 'backups') renderBackups();
      };
    }

    // keymap tab
    $('#btn-add-layer').onclick = () => {
      if (model.layerCount >= (model.info ? model.info.maxLayers : MK.MAX_LAYERS)) return;
      model.layerCount++;
      if (!model.keymaps[model.layerCount - 1]) model.keymaps[model.layerCount - 1] = MK.emptyLayer(model.info.keys);
      if (!model.layerNames[model.layerCount - 1]) model.layerNames[model.layerCount - 1] = `layer ${model.layerCount}`;
      selLayer = model.layerCount - 1;
      markDirtyAndRender();
    };
    $('#btn-remove-layer').onclick = async () => {
      if (model.layerCount <= 1) return;
      const name = model.layerNames[model.layerCount - 1];
      if (!(await uiConfirm(
        `Disable layer ${model.layerCount} (“${name}”)? Its data stays in the keyboard storage but the layer becomes inactive.`, 'Remove', true))) return;
      model.layerCount--;
      if (selLayer >= model.layerCount) selLayer = model.layerCount - 1;
      if (model.baseLayer >= model.layerCount) model.baseLayer = 0;
      markDirtyAndRender();
    };
    $('#layer-name').onchange = () => {
      model.layerNames[selLayer] = MK.sanitizeLayerName($('#layer-name').value) || model.layerNames[selLayer];
      $('#layer-name').value = model.layerNames[selLayer];
      markDirtyAndRender();
    };
    $('#base-layer').onchange = () => {
      model.baseLayer = +$('#base-layer').value;
      markDirtyAndRender();
    };
    $('#hold-ms').onchange = () => {
      const v = Math.min(65535, Math.max(1, Math.round(+$('#hold-ms').value || 0)));
      if (v >= 1 && v <= 65535) { model.holdMs = v; $('#hold-ms').value = v; markDirtyAndRender(); }
      else $('#hold-ms').value = model.holdMs;
    };
    $('#btn-read').onclick = async () => {
      const d = localDiff();
      if (d && d.hasChanges && !(await uiConfirm('Discard local changes and read the keymap from the keyboard?', 'Read'))) return;
      await readFromKeyboard();
    };
    $('#btn-apply').onclick = applyToKeyboard;
    $('#btn-revert').onclick = async () => {
      const d = localDiff();
      if (!d || !d.hasChanges) { toast('No local changes to discard'); return; }
      if (!(await uiConfirm('Discard all local changes and return to the last-read keymap?', 'Discard', true))) return;
      model.layerCount = snapshot.layerCount;
      model.layerNames = snapshot.layerNames.slice();
      model.baseLayer = snapshot.baseLayer;
      model.holdMs = snapshot.holdMs;
      model.keymaps = MK.clone(snapshot.keymaps);
      if (selLayer >= model.layerCount) selLayer = 0;
      renderKeymapTab();
    };

    // LED tab
    $('#led-enabled').onchange = () => ledCommand(`LED ENABLE ${$('#led-enabled').checked ? 'ON' : 'OFF'}`, { enabled: $('#led-enabled').checked });
    $('#led-brightness').oninput = () => { $('#led-brightness-val').textContent = $('#led-brightness').value; };
    $('#led-brightness').onchange = () =>
      ledCommand(`LED BRIGHTNESS ${$('#led-brightness').value}`, { brightness: +$('#led-brightness').value });
    $('#led-effect').onchange = () => ledCommand(`LED EFFECT ${$('#led-effect').value}`, { effect: $('#led-effect').value });
    $('#led-color').onchange = () => {
      const hex = $('#led-color').value;
      const r = parseInt(hex.slice(1, 3), 16), g = parseInt(hex.slice(3, 5), 16), b = parseInt(hex.slice(5, 7), 16);
      ledCommand(`LED COLOR ${r} ${g} ${b}`, { color: hex });
    };
    $('#led-keypress').onchange = () => ledCommand(`LED KEYPRESS ${$('#led-keypress').value}`, { keypress: $('#led-keypress').value });
    $('#led-sleep').onchange = () => ledCommand(`LED SLEEP ${$('#led-sleep').value}`, { sleep: $('#led-sleep').value });
    $('#led-sleep-timeout').onchange = () => {
      const v = Math.max(0, Math.round(+$('#led-sleep-timeout').value || 0));
      $('#led-sleep-timeout').value = v;
      ledCommand(`LED SLEEP_TIMEOUT ${v}`, { sleepTimeoutMs: v });
    };
    $('#led-ble').onchange = () => ledCommand(`LED BLE ${$('#led-ble').checked ? 'ON' : 'OFF'}`, { bleIndicators: $('#led-ble').checked });
    $('#btn-led-read').onclick = readLed;
    $('#btn-led-save').onclick = async () => {
      if (!connected) { toast('Not connected', 'error'); return; }
      try {
        await client.send('LED SAVE', { timeoutMs: 5000 });
        toast('LED settings saved to keyboard', 'success');
      } catch (err) {
        toast('LED SAVE failed: ' + err.message, 'error');
      }
    };
    $('#btn-led-hello').onclick = () => ledCommand('HELLO', null);

    // device tab
    $('#btn-ble-refresh').onclick = refreshBle;
    $('#monitor-toggle').onchange = () => ($('#monitor-toggle').checked ? startMonitor() : (stopMonitor(), $('#monitor-toggle').checked = false));
    $('#btn-save-all').onclick = async () => {
      if (!ensureConnected()) return;
      try {
        await client.send('SAVE ALL', { timeoutMs: 8000 });
        await readAllFromDevice();
        renderAll();
        toast('Saved to flash', 'success');
      } catch (err) { toast('SAVE ALL failed: ' + err.message, 'error'); }
    };
    $('#btn-recall-all').onclick = async () => {
      if (!ensureConnected()) return;
      if (!(await uiConfirm('Reload the saved keymap from flash? Unsaved changes in RAM will be lost.', 'Reload', true))) return;
      try {
        await client.send('RECALL ALL');
        await readAllFromDevice();
        renderAll();
        toast('Reloaded from flash', 'success');
      } catch (err) { toast('RECALL failed: ' + err.message, 'error'); }
    };
    $('#btn-reset-all').onclick = async () => {
      if (!ensureConnected()) return;
      if (!(await uiConfirm('Reset all layers to compiled defaults in RAM? Nothing is written to flash yet.', 'Reset', true))) return;
      try {
        await client.send('RESET ALL');
        await readAllFromDevice();
        renderAll();
        toast('RAM reset to defaults — use “Apply” or “Save all” to persist', 'warn');
      } catch (err) { toast('RESET failed: ' + err.message, 'error'); }
    };

    // presets tab
    $('#btn-preset-layer').onclick = () => savePreset('layer');
    $('#btn-preset-keymap').onclick = () => savePreset('keymap');
    $('#btn-preset-import').onclick = () => $('#preset-file').click();
    $('#preset-file').onchange = () => {
      const f = $('#preset-file').files[0];
      if (f) importPresets(f);
      $('#preset-file').value = '';
    };

    // backups tab
    $('#btn-backups-export').onclick = exportAllBackups;
    $('#btn-backups-clear').onclick = async () => {
      if (!(await uiConfirm('Delete ALL backups? This cannot be undone.', 'Delete all', true))) return;
      await idb.clear('backups');
      renderBackups();
      toast('All backups deleted', 'warn');
    };

    navigator.serial && navigator.serial.addEventListener('disconnect', (e) => {
      if (e.port === client.port) onDisconnect();
    });

    renderDeviceTab();
    renderLedTab();

    // Bridge for js/globals.js (firmware 3.0 panels).
    window.MKUI = {
      client,
      toast,
      log,
      uiConfirm,
      layerCount: () => model.layerCount,
      layerName: (i) => model.layerNames[i],
    };
  }

  document.addEventListener('DOMContentLoaded', wire);
})();
