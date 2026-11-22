/* Firmware 3.0 panels: device settings, extended LED, per-layer LED,
 * palettes, multikey. Talks to the keyboard through the shared serial
 * client exposed by app.js as window.MKUI. */
(function () {
  const $ = (sel) => document.querySelector(sel);
  const ui = () => window.MKUI;

  function toast(msg, kind) { if (ui()) ui().toast(msg, kind || 'info'); }
  function client() { return ui() ? ui().client : null; }

  async function send(cmd, opts) {
    const c = client();
    if (!c || !c.port) { toast('Not connected', 'error'); throw new Error('not connected'); }
    return c.send(cmd, opts || {});
  }

  function num(el, fallback) {
    const v = parseInt(el.value, 10);
    return Number.isFinite(v) ? v : fallback;
  }

  // Nonlinear (gamma) fader: slider position -> perceptual value.
  const gamma = (s) => Math.round((s * s) / 255);
  const invGamma = (v) => Math.round(Math.sqrt(Math.max(0, v) * 255));

  function wireFader(input, label, initial) {
    const update = () => { label.textContent = gamma(num(input, 0)); };
    input.value = invGamma(initial);
    input.addEventListener('input', update);
    update();
  }

  function hexToRgb(hex) {
    const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex || '');
    return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : [0, 0, 0];
  }
  const rgbToHex = (r, g, b) =>
    '#' + [r, g, b].map((v) => Math.max(0, Math.min(255, v)).toString(16).padStart(2, '0')).join('');

  // ------------------------------------------------------------------
  // Device settings
  const NAV_NAMES = ['UP', 'LEFT', 'RIGHT', 'DOWN', 'ENTER'];

  function parseGlobals(line) {
    const out = {};
    const m = /^OK GLOBAL /.exec(line);
    if (!m) return null;
    for (const pair of line.slice(m[0].length).trim().split(/\s+/)) {
      const eq = pair.indexOf('=');
      if (eq > 0) out[pair.slice(0, eq)] = pair.slice(eq + 1);
    }
    return out;
  }

  function fillGlobals(g) {
    if (!g) return;
    $('#g-rollover').value = g.rollover || '0';
    $('#g-usb').value = g.usb || '1';
    $('#g-bt').value = g.bt || '0';
    $('#g-btpower').value = g.bt_power || '3';
    $('#g-lang').value = g.lang || '0';
    $('#g-oledsleep').value = Math.round((parseInt(g.oled_sleep, 10) || 0) / 1000);
    $('#g-bootmenu').value = g.boot || '1';
    $('#g-boottime').value = g.boot_ms || '2000';
    $('#g-navbright').value = g.nav_bright || '30';
    const mk = g.mk === '1';
    const id = parseInt(g.mk_id, 10) || 0;
    $('#g-mk').value = mk ? (id === 0 ? 'master' : String(id)) : 'off';
    $('#g-mklayout').value = g.mk_layout || '0';
    $('#g-savechanges').checked = g.save_slave === '1';
    $('#g-showall').checked = g.show_all === '1';
    const nav = (g.nav || '').split(',').map((s) => parseInt(s, 10) || 0);
    NAV_NAMES.forEach((name, i) => {
      const input = document.querySelector(`#g-nav-${i}`);
      if (input) input.value = nav[i] || 0;
    });
  }

  async function readGlobals() {
    try {
      const res = await send('GET GLOBAL', { timeoutMs: 5000 });
      fillGlobals(parseGlobals(res.lines[0]));
      toast('Settings read', 'success');
    } catch (e) { toast('GET GLOBAL failed: ' + e.message, 'error'); }
  }

  async function writeGlobals() {
    try {
      const cmds = [
        `SET GLOBAL ROLLOVER ${$('#g-rollover').value}`,
        `SET GLOBAL USB ${$('#g-usb').value === '0' ? 'HID' : 'CFG'}`,
        `SET GLOBAL BT ${$('#g-bt').value === '0' ? 'HID' : 'COM'}`,
        `SET GLOBAL BTPOWER ${num($('#g-btpower'), 3)}`,
        `SET GLOBAL LANG ${$('#g-lang').value}`,
        `SET GLOBAL OLEDSLEEP ${num($('#g-oledsleep'), 0)}`,
        `SET GLOBAL BOOTMENU ${$('#g-bootmenu').value}`,
        `SET GLOBAL BOOTTIME ${num($('#g-boottime'), 2000)}`,
        `SET GLOBAL NAVBRIGHT ${num($('#g-navbright'), 30)}`,
      ];
      const mkSel = $('#g-mk').value;
      cmds.push(`SET GLOBAL MK ${mkSel === 'off' ? 'OFF' : 'ON'}`);
      cmds.push(`SET GLOBAL MKID ${mkSel === 'off' || mkSel === 'master' ? 0 : mkSel}`);
      cmds.push(`SET GLOBAL MKLAYOUT ${num($('#g-mklayout'), 0)}`);
      cmds.push(`SET GLOBAL SAVECHANGES ${$('#g-savechanges').checked ? 'ON' : 'OFF'}`);
      cmds.push(`SET GLOBAL SHOWALL ${$('#g-showall').checked ? 'ON' : 'OFF'}`);
      const nav = NAV_NAMES.map((_, i) => num(document.querySelector(`#g-nav-${i}`), 0)).join(' ');
      cmds.push(`SET GLOBAL NAVKEYS ${nav}`);
      for (const cmd of cmds) await send(cmd);
      toast('Settings written', 'success');
    } catch (e) { toast('Write failed: ' + e.message, 'error'); }
  }

  // ------------------------------------------------------------------
  // Extended LED
  function parseLed(line) {
    const out = {};
    const m = /^OK LED /.exec(line);
    if (!m) return null;
    for (const pair of line.slice(m[0].length).trim().split(/\s+/)) {
      const eq = pair.indexOf('=');
      if (eq > 0) out[pair.slice(0, eq)] = pair.slice(eq + 1);
    }
    return out;
  }

  function fillLed3(s) {
    if (!s) return;
    wireFader($('#l3-brightness'), $('#l3-brightness-val'), parseInt(s.brightness, 10) || 0);
    wireFader($('#l3-backlight'), $('#l3-backlight-val'), parseInt(s.backlight, 10) || 0);
    $('#l3-mix').value = s.mix || '0';
    $('#l3-fxspeed').value = s.fx_speed || '128';
    $('#l3-kpspeed').value = s.kp_speed || '128';
    $('#l3-slspeed').value = s.sleep_speed || '128';
    const bl = (s.backlight || '0#000000').split('#')[1] || '000000';
    $('#l3-backlight-color').value = '#' + bl;
    $('#l3-flash').value = s.flash || '1';
    $('#l3-flash-next').value = '#' + (s.next || '00A000');
    $('#l3-flash-prev').value = '#' + (s.prev || 'A03C00');
    $('#l3-flash-fade').value = s.fade || '700';
  }

  async function readLed3() {
    try {
      const res = await send('LED STATUS', { timeoutMs: 5000 });
      fillLed3(parseLed(res.lines[0]));
    } catch (e) { toast('LED STATUS failed: ' + e.message, 'error'); }
  }

  async function writeLed3() {
    try {
      const bl = hexToRgb($('#l3-backlight-color').value);
      const next = hexToRgb($('#l3-flash-next').value).join(' ');
      const prev = hexToRgb($('#l3-flash-prev').value).join(' ');
      const cmds = [
        `LED BRIGHTNESS ${gamma(num($('#l3-brightness'), 0))}`,
        `LED MIX ${$('#l3-mix').value}`,
        `LED SPEED EFFECT ${num($('#l3-fxspeed'), 128)}`,
        `LED SPEED KEYPRESS ${num($('#l3-kpspeed'), 128)}`,
        `LED SPEED SLEEP ${num($('#l3-slspeed'), 128)}`,
        `LED BACKLIGHT ${gamma(num($('#l3-backlight'), 0))} ${bl.join(' ')}`,
        `LED LAYERFLASH ${$('#l3-flash').value === '1' ? 'ON' : 'OFF'} ${next} ${prev} ${num($('#l3-flash-fade'), 700)}`,
      ];
      for (const cmd of cmds) await send(cmd);
      toast('LED settings written', 'success');
    } catch (e) { toast('LED write failed: ' + e.message, 'error'); }
  }

  // Per-layer LED
  function parseLedLayer(line) {
    const m = /^OK LEDLAYER layer=(\d+) brightness=(\d+) effect=(\d+) color=([0-9A-Fa-f]{6})/.exec(line);
    return m ? { layer: +m[1], brightness: +m[2], effect: +m[3], color: m[4] } : null;
  }

  async function readLedLayer() {
    try {
      const layer = num($('#l3-layer'), 0);
      const res = await send(`GET LEDLAYER ${layer}`);
      const s = parseLedLayer(res.lines[0]);
      if (!s) return;
      wireFader($('#l3-layer-bright'), $('#l3-layer-bright-val'), s.brightness);
      $('#l3-layer-effect').value = String(s.effect);
      $('#l3-layer-color').value = '#' + s.color;
    } catch (e) { toast('GET LEDLAYER failed: ' + e.message, 'error'); }
  }

  async function writeLedLayer() {
    try {
      const layer = num($('#l3-layer'), 0);
      const c = hexToRgb($('#l3-layer-color').value);
      await send(`LED LAYER ${layer} BRIGHT ${gamma(num($('#l3-layer-bright'), 255))}`);
      await send(`LED LAYER ${layer} EFFECT ${$('#l3-layer-effect').value}`);
      await send(`LED LAYER ${layer} COLOR ${c.join(' ')}`);
      toast('Layer LED written', 'success');
    } catch (e) { toast('Layer LED write failed: ' + e.message, 'error'); }
  }

  // ------------------------------------------------------------------
  // Palettes
  const PAL_NAMES = ['RAINBOW', 'HEAT', 'OCEAN', 'FOREST', 'SUNSET', 'CANDY', 'GRAPE',
    'MINT', 'GOLD', 'ICE', 'CORAL', 'LAGOON', 'MAGMA', 'VIOLET', 'NEON', 'RUST'];

  function buildPaletteUi() {
    const sel = $('#pal-index');
    sel.innerHTML = '';
    for (let i = 0; i < 16; ++i) {
      const opt = document.createElement('option');
      opt.value = String(i);
      opt.textContent = `${i + 1} ${PAL_NAMES[i]}`;
      sel.appendChild(opt);
    }
    const stops = $('#pal-stops');
    stops.innerHTML = '';
    for (let i = 0; i < 8; ++i) {
      const row = document.createElement('label');
      row.className = 'pal-stop field';
      row.innerHTML = `Stop ${i + 1} <input type="color" data-stop="${i}" disabled> <span class="mono stop-pos">${Math.round(i * 255 / 7)}</span>`;
      stops.appendChild(row);
    }
  }

  function fillPalette(lines) {
    const stops = {};
    for (const line of lines) {
      const m = /^STOP (\d+) (\d+) (\d+) (\d+)$/.exec(line);
      if (m) stops[+m[1]] = [+m[2], +m[3], +m[4]];
    }
    document.querySelectorAll('#pal-stops input[data-stop]').forEach((input) => {
      const i = +input.dataset.stop;
      if (stops[i]) {
        input.value = rgbToHex(stops[i][0], stops[i][1], stops[i][2]);
        input.disabled = false;
      } else {
        input.disabled = true;
      }
    });
  }

  async function readPalette() {
    try {
      const res = await send(`GET PALETTE ${$('#pal-index').value}`, { ends: 1, timeoutMs: 5000 });
      fillPalette(res.lines);
    } catch (e) { toast('GET PALETTE failed: ' + e.message, 'error'); }
  }

  async function writePalette() {
    try {
      const index = $('#pal-index').value;
      for (const input of document.querySelectorAll('#pal-stops input[data-stop]')) {
        if (input.disabled) continue;
        const c = hexToRgb(input.value);
        await send(`SET PALETTE ${index} ${input.dataset.stop} ${c.join(' ')}`);
      }
      toast('Palette written', 'success');
    } catch (e) { toast('Palette write failed: ' + e.message, 'error'); }
  }

  // ------------------------------------------------------------------
  // Multikey slots
  function buildSlots() {
    const root = $('#mk-slots');
    if (!root) return;
    root.innerHTML = '';
    for (let i = 0; i < 6; ++i) {
      const save = document.createElement('button');
      save.className = 'btn small needs-conn';
      save.textContent = `SAVE ${i + 1}`;
      save.onclick = async () => {
        try { await send(`MULTIKEY SLOT SAVE ${i + 1}`); toast(`Multikey slot ${i + 1} saved`, 'success'); }
        catch (e) { toast(e.message, 'error'); }
      };
      const load = document.createElement('button');
      load.className = 'btn small needs-conn';
      load.textContent = `LOAD ${i + 1}`;
      load.onclick = async () => {
        try { await send(`MULTIKEY SLOT LOAD ${i + 1}`); toast(`Multikey slot ${i + 1} loaded`, 'success'); }
        catch (e) { toast(e.message, 'error'); }
      };
      root.appendChild(save);
      root.appendChild(load);
    }
  }

  // ------------------------------------------------------------------
  function wire() {
    if (!ui()) return; // app.js failed to load; nothing to attach to.

    buildPaletteUi();
    buildSlots();

    // Nav key inputs
    const navRoot = $('#g-navkeys');
    NAV_NAMES.forEach((name, i) => {
      const label = document.createElement('label');
      label.className = 'field';
      label.innerHTML = `${name} key <input id="g-nav-${i}" type="number" min="0" max="34" step="1" style="width:5em">`;
      navRoot.appendChild(label);
    });

    // Layer select for per-layer LED
    const layerSel = $('#l3-layer');
    layerSel.innerHTML = '';
    const layers = Math.max(1, ui().layerCount ? ui().layerCount() : 8);
    for (let i = 0; i < layers; ++i) {
      const opt = document.createElement('option');
      opt.value = String(i);
      opt.textContent = `${i + 1} ${ui().layerName ? ui().layerName(i) || '' : ''}`;
      layerSel.appendChild(opt);
    }

    $('#btn-globals-read').onclick = readGlobals;
    $('#btn-globals-write').onclick = writeGlobals;
    $('#btn-led3-read').onclick = readLed3;
    $('#btn-led3-write').onclick = writeLed3;
    $('#btn-led3-layer-write').onclick = writeLedLayer;
    layerSel.onchange = readLedLayer;
    $('#btn-pal-read').onclick = readPalette;
    $('#btn-pal-write').onclick = writePalette;
    $('#pal-index').onchange = () => { };
    $('#btn-mk-push').onclick = async () => {
      try { await send('MULTIKEY PUSH'); toast('Config pushed to slaves', 'success'); }
      catch (e) { toast(e.message, 'error'); }
    };

    wireFader($('#l3-brightness'), $('#l3-brightness-val'), 50);
    wireFader($('#l3-backlight'), $('#l3-backlight-val'), 0);
    wireFader($('#l3-layer-bright'), $('#l3-layer-bright-val'), 255);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', wire);
  } else {
    wire();
  }
})();
