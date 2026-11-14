/* TEST-ONLY: drives the real UI end-to-end against mock-serial.js. */
(function () {
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
  const results = [];

  function report(ok, label) {
    results.push((ok ? 'PASS' : 'FAIL') + ' ' + label);
    const el = document.getElementById('test-results');
    if (el) el.textContent = results.join('\n');
    document.title = results.some((r) => r.startsWith('FAIL')) ? 'FAIL' : `DONE ${results.length}`;
  }

  async function waitFor(fn, timeoutMs, label) {
    const t0 = Date.now();
    for (;;) {
      try { if (await fn()) return; } catch (_) { /* retry */ }
      if (Date.now() - t0 > timeoutMs) throw new Error('waitFor timeout: ' + label);
      await sleep(50);
    }
  }

  function busyHidden() { return document.getElementById('busy').classList.contains('hidden'); }

  async function waitBusy() {
    await waitFor(() => !busyHidden(), 5000, 'busy to appear');
    await waitFor(busyHidden, 30000, 'busy to clear');
  }

  function idbCount(store) {
    return new Promise((resolve, reject) => {
      const req = indexedDB.open('multikey-configurator');
      req.onsuccess = () => {
        const r = req.result.transaction(store, 'readonly').objectStore(store).count();
        r.onsuccess = () => resolve(r.result);
        r.onerror = () => reject(r.error);
      };
      req.onerror = () => reject(req.error);
    });
  }

  function modalButton(text) {
    return [...document.querySelectorAll('#modal-root button')].find((b) => b.textContent.trim() === text);
  }

  function switchTab(name) {
    document.querySelector(`.tab[data-tab="${name}"]`).click();
  }

  async function runScenario() {
    const S = () => window.__mockState;
    try {
      document.title = 'RUNNING';

      // Fresh IndexedDB so counts are deterministic across reruns.
      await new Promise((resolve, reject) => {
        const req = indexedDB.open('multikey-configurator');
        req.onsuccess = () => {
          const db = req.result;
          const tx = db.transaction(['backups', 'presets'], 'readwrite');
          tx.objectStore('backups').clear();
          tx.objectStore('presets').clear();
          tx.oncomplete = () => { db.close(); resolve(); };
          tx.onerror = () => reject(tx.error);
        };
        req.onerror = () => reject(req.error);
      });

      // --- T1: connect (board boots ~1.6 s after port open, like real hardware) -
      document.getElementById('btn-connect').click();
      await waitFor(() => !document.body.classList.contains('offline'), 15000, 'connect');
      report(true, 'T1 connect (through boot window)');

      // --- T2: initial render ------------------------------------------------
      await waitFor(() => document.querySelectorAll('#matrix .kb-key').length === 35, 3000, 'matrix render');
      report(document.querySelectorAll('#matrix .kb-key').length === 35, 'T2a matrix renders 35 keys');
      report(document.querySelectorAll('#layer-tabs .layer-tab').length === 4, 'T2b 4 layer tabs');
      report(document.getElementById('device-summary').textContent.includes('2.0.0'), 'T2c device summary shows firmware');
      const firstCap = document.querySelector('#matrix .kb-key .k-main').textContent;
      report(firstCap === 'Del', 'T2d factory keymap shown (key 1 = Del), got "' + firstCap + '"');
      report(document.querySelector('#led-brightness-val').textContent === '50', 'T2e LED status read (brightness 50)');

      // --- T3: edit key 0 -> LAYER 3 MODAL -----------------------------------
      document.querySelectorAll('#matrix .kb-key')[0].click();
      await sleep(150);
      modalButton('Layer').click();
      await sleep(100);
      const targetSel = document.querySelector('#modal-root select');
      targetSel.value = '2';
      targetSel.dispatchEvent(new Event('change'));
      await sleep(50);
      [...document.querySelectorAll('#modal-root .km-mode')].find((d) => d.textContent.includes('Modal')).click();
      await sleep(80);
      modalButton('Save').click();
      await sleep(100);
      report(document.getElementById('dirty-status').textContent.includes('1 key'), 'T3 key edit marks 1 changed key');

      // --- T4: apply (auto-backup, flash, verify, save) ----------------------
      const savesBefore = S().saveCount;
      document.getElementById('btn-apply').click();
      await waitBusy();
      const a = S().keymaps[0][0];
      report(a.type === 'LAYER' && a.target === 2 && a.mode === 'MODAL', 'T4a device received layer action');
      report(S().saveCount === savesBefore + 1, 'T4b SAVE ALL executed');
      report((await idbCount('backups')) === 1, 'T4c auto-backup stored');

      // --- T5: add a layer + rename it, then apply ----------------------------
      document.getElementById('btn-add-layer').click();
      await sleep(80);
      report(document.querySelectorAll('#layer-tabs .layer-tab').length === 5, 'T5a layer tab added');
      document.getElementById('layer-name').value = 'games';
      document.getElementById('layer-name').dispatchEvent(new Event('change'));
      await sleep(80);
      document.getElementById('btn-apply').click();
      await waitBusy();
      report(S().layerCount === 5, 'T5b device layer count = 5');
      report(S().names[4] === 'games', 'T5c device layer 5 named "games"');

      // --- T6: LED settings ---------------------------------------------------
      const eff = document.getElementById('led-effect');
      eff.value = 'RAINBOW';
      eff.dispatchEvent(new Event('change'));
      await sleep(150);
      report(S().led.effect === 2, 'T6 LED base effect = RAINBOW on device');

      // --- T7: hold threshold -------------------------------------------------
      const hold = document.getElementById('hold-ms');
      hold.value = '250';
      hold.dispatchEvent(new Event('change'));
      await sleep(80);
      document.getElementById('btn-apply').click();
      await waitBusy();
      report(S().holdMs === 250, 'T7 hold threshold = 250 ms on device');

      // --- T8: restore the OLDEST backup (the T4 one, sorted last) -------------
      switchTab('backups');
      await sleep(200);
      report((await idbCount('backups')) === 3, 'T8a three backups exist before restore');
      const restoreButtons = document.querySelectorAll('#backup-list .list-item button.primary');
      restoreButtons[restoreButtons.length - 1].click();
      await sleep(150);
      modalButton('Restore').click();
      await waitBusy();
      const a8 = S().keymaps[0][0];
      report(a8.type === 'HID' && a8.usage === 0x4C, 'T8b key 1 restored to Del');
      report(S().layerCount === 4, 'T8c layer count restored to 4');
      report(S().holdMs === 500, 'T8d hold threshold restored to 500');
      report((await idbCount('backups')) === 4, 'T8e restore created its own backup first');

      // --- T9: presets ---------------------------------------------------------
      switchTab('presets');
      await sleep(200);
      document.getElementById('btn-preset-keymap').click();
      await sleep(150);
      modalButton('Save').click();
      await sleep(200);
      report((await idbCount('presets')) === 1, 'T9 preset saved');

      // --- T10: log sanity ------------------------------------------------------
      const logText = document.getElementById('log-lines').textContent;
      report(!logText.includes('Timeout') && !logText.includes('Flash failed'), 'T10 no timeouts/errors in log');

      document.title = `DONE ${results.filter((r) => r.startsWith('PASS')).length}/${results.length}` +
        (results.some((r) => r.startsWith('FAIL')) ? ' FAIL' : '');
      results.push('SCENARIO COMPLETE');
      document.getElementById('test-results').textContent = results.join('\n');
      // Mirror results into the log panel so they are easy to read from the DOM.
      document.getElementById('log-lines').textContent = results.join('\n');
    } catch (err) {
      report(false, 'EXCEPTION: ' + err.message);
      document.title = 'FAIL';
    }
  }

  window.addEventListener('load', () => setTimeout(runScenario, 400));
})();
