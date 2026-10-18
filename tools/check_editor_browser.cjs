// Optional browser acceptance check. Requires Playwright, Chromium/Chrome with
// local-network permission support, and access to the pinned compiler assets.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const { spawn, execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const root = path.resolve(__dirname, '..');
const python = process.env.LIGHTVM_PYTHON || path.join(root, 'build/venv/bin/python');
const receiverPath = process.env.LIGHTVM_RECEIVER || path.join(root, 'build/network-host/lightvm');
function announcedUrl(child, output) {
  return new Promise((resolve, reject) => {
    let text = '';
    const timer = setTimeout(() => finish(new Error('Local test server did not start within ten seconds')), 10000);
    const failed = error => finish(error);
    const exited = code => finish(new Error(`Local test server exited (${code}): ${text}`));
    const received = bytes => { text += bytes; const match = /http:\/\/\S+/.exec(text); if (match) finish(null, match[0]); };
    function finish(error, url) {
      clearTimeout(timer); output.off('data', received); child.off('error', failed); child.off('exit', exited);
      if (error) reject(error); else resolve(url);
    }
    output.on('data', received); child.once('error', failed); child.once('exit', exited);
  });
}
async function grantLocalNetwork(cdp, page, origin) {
  const session = await page.context().newCDPSession(page);
  const { targetInfo } = await session.send('Target.getTargetInfo');
  await session.detach();
  await cdp.send('Browser.grantPermissions', { origin, browserContextId: targetInfo.browserContextId, permissions: ['localNetwork', 'loopbackNetwork', 'localNetworkAccess'] });
}
(async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'lightvm-network-ui-'));
  let receiver, server, browser, page;
  try {
    receiver = spawn(receiverPath, ['--http', '0', '--storage', path.join(directory, 'store.bin')]);
    const url = await announcedUrl(receiver, receiver.stderr);
    server = spawn(python, [path.join(root, 'tools/serve_editor.py'), '--port', '0']);
    const editorUrl = await announcedUrl(server, server.stdout);
    browser = await chromium.launch({ headless: true, ...(process.env.CHROME_EXECUTABLE ? { executablePath: process.env.CHROME_EXECUTABLE } : {}) });
    const cdp = await browser.newBrowserCDPSession();
    page = await browser.newPage({ viewport: { width: 1440, height: 1200 } });
    await grantLocalNetwork(cdp, page, editorUrl);
    // Route only the bootstrap hostname to the actual simulator metadata.
    // DNS packet interoperability is tested separately; HTTP and UI are real.
    await page.route('http://lightvm.local/api/v1/devices', async route => {
      const response = await route.fetch({ url: url + '/api/v1/devices' });
      await route.fulfill({ response });
    });
    const { FrameDecoder } = await import('../web/protocol.mjs');
    const replies = new EventEmitter(), counts = { sockets: 0, socketFader: 0, httpFader: 0 };
    const waitForCommand = (op, timeout = 12000) => new Promise((resolve, reject) => {
      const done = () => { clearTimeout(timer); resolve(); };
      const timer = setTimeout(() => { replies.off(op, done); reject(new Error('No command reply: ' + op)); }, timeout);
      replies.once(op, done);
    });
    page.on('response', response => {
      const bytes = response.request().postDataBuffer();
      if (!response.ok() || !response.url().endsWith('/api/v1/command') || !bytes) return;
      try {
        const { op } = JSON.parse(bytes.subarray(16).toString());
        if (op === 'transition_update') counts.httpFader++;
        replies.emit(op);
      } catch { /* Non-JSON upload commands. */ }
    });
    page.on('websocket', socket => {
      if (!socket.url().endsWith('/api/v1/ws')) return;
      counts.sockets++;
      const pending = new Map();
      const sent = new FrameDecoder(({ kind, id, payload }) => {
        if (kind !== 11) return;
        const { op } = JSON.parse(new TextDecoder().decode(payload)); pending.set(id, op);
        if (op === 'transition_update') counts.socketFader++;
      });
      const received = new FrameDecoder(({ kind, id }) => {
        if (kind !== 129) return;
        const op = pending.get(id); pending.delete(id); if (op) replies.emit(op);
      });
      socket.on('framesent', ({ payload }) => sent.feed(new Uint8Array(payload)));
      socket.on('framereceived', ({ payload }) => received.feed(new Uint8Array(payload)));
    });
    const errors = []; page.on('pageerror', error => errors.push(error.message)); page.on("console", m => { if (m.type()==="error") console.log(m.text()); }); page.on("requestfailed", r => console.log("REQUEST FAILED", r.url(), r.failure()));
    // The serial API is simulated over a second logical HTTP session. Native
    // pipe/HTTP concurrency is covered independently by tests/test_http.py.
    await page.addInitScript(device => {
      class Port {
        async open() {
          this.session = (await (await fetch(device + '/api/v1/sessions', { method: 'POST' })).json()).session_id;
          this.readable = new ReadableStream({ start: controller => { this.reader = controller; } });
          this.writable = new WritableStream({ write: async bytes => {
            const response = await fetch(device + '/api/v1/command', { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'X-LightVM-Session': this.session }, body: bytes });
            this.reader.enqueue(new Uint8Array(await response.arrayBuffer()));
          } });
        }
        getInfo() { return { usbVendorId: 0x303a, usbProductId: 0x1001 }; }
        async setSignals() {}
        async close() { await fetch(device + '/api/v1/sessions/' + this.session, { method: 'DELETE' }); }
      }
      Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => new Port() } });
    }, url);
    await page.goto(editorUrl);
    await page.waitForFunction(() => document.querySelector('#discovery-state')?.textContent.includes('1 device found'));
    assert.equal(await page.locator('#http-address').inputValue(), url);
    assert.equal(await page.locator('#nearby-devices option').count(), 2);
    await page.locator('#connect-http').click();
    await page.waitForFunction(() => location.hash.endsWith('/configure') && document.querySelector('#output-add'));
    const deviceInfo = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--describe'], { encoding: 'utf8' }));
    assert.match(await page.locator('#path-health').innerText(), deviceInfo.websocket_supported ? /WebSocket: connected/ : /HTTP: connected/);
    assert.equal(counts.sockets, deviceInfo.websocket_supported ? 1 : 0);
    console.log('Device picker selected ' + (deviceInfo.websocket_supported ? 'WebSocket' : 'HTTP') + ' and opened configuration: passed');

    await page.locator('#output-add').click();
    await page.locator('#config-count-0').fill('5');
    await page.locator('#output-add').click();
    await page.locator('#config-pin-1').selectOption('GP17');
    await page.locator('#config-count-1').fill('7');
    await page.locator('#config-save').click();
    await page.waitForFunction(() => document.querySelector('#output-binding')?.textContent.includes('5 pixels') && !document.querySelector('#config-fields')?.disabled);
    const activeConfig = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--get-config'], { encoding: 'utf8' }));
    assert.equal(activeConfig.devices[0].config.count, 5);
    console.log('Output form saved hardware configuration: passed');

    await page.getByRole('link', { name: 'Connections & devices' }).click();
    await page.locator('#connect').click();
    await page.waitForFunction(() => location.hash.endsWith('/develop') && document.querySelector('#path-health')?.textContent.includes('USB: connected'));
    await page.locator('#command-path').selectOption('http');
    await page.waitForFunction(() => !document.querySelector('#command-path')?.disabled);
    await page.waitForFunction(() => ['Compiler ready', 'Compiler unavailable'].includes(document.querySelector('#compiler-state')?.textContent), null, { timeout: 70000 });
    const compiler = await page.locator('#compiler-state').innerText();
    assert.equal(compiler, 'Compiler ready', 'This check requires the pinned browser compiler assets');

    await page.locator('#preview-output').selectOption('lights2');
    await page.waitForFunction(() => !document.querySelector('#preview-output')?.disabled);
    await page.locator('#upload').click();
    await page.waitForFunction(() => document.querySelector('#playback-state')?.textContent.includes('Running') && !document.querySelector('#upload')?.disabled);
    await page.locator('#draft-duration').fill('30');
    await page.locator('#upload').click();
    await page.waitForFunction(() => !document.querySelector('#upload')?.disabled);
    await page.locator('#animation-name').fill('Browser rainbow');
    await page.getByRole('button', { name: 'Save animation', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#saved-animation')?.value && document.querySelector('#activity')?.textContent.includes('Browser rainbow v1'));
    await page.getByRole('button', { name: 'Add as layer', exact: true }).click();
    await page.locator('#layer2-duration').fill('5');
    await page.locator('.layer-card').nth(1).locator('.layer-controls select').selectOption('screen');
    await page.getByRole('button', { name: 'Move layer 2 down', exact: true }).click();
    await page.getByRole('button', { name: 'Preview preset →', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#activity')?.textContent.includes('Preset preview running'));
    await page.locator('#preset-name').fill('browser');
    await page.getByRole('button', { name: 'Save preset', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#saved-preset')?.value);
    await page.getByRole('button', { name: 'Save preset to device', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#device-preset')?.value === 'browser');
    const savedLibrary = await page.evaluate(() => JSON.parse(localStorage.getItem('lightvm.library.v1')));
    assert.equal(savedLibrary.animations.length, 1);
    assert.equal(savedLibrary.presets[0].layers.length, 2);
    assert.equal(savedLibrary.presets[0].layers[0].settings.duration, 5);
    assert.equal(savedLibrary.presets[0].layers[0].blend, 'screen');
    assert.equal(savedLibrary.presets[0].layers[1].settings.duration, 30);
    const programName = (await page.locator('#stored-program option').nth(1).getAttribute('value')).slice('animation:'.length);
    await page.locator('#command-path').selectOption('usb');
    await page.waitForFunction(() => !document.querySelector('#command-path')?.disabled);
    await page.getByRole('button', { name: 'Run on output', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#activity')?.textContent.includes('Preset running on'));
    console.log('Selected second output, previewed settings, saved source, composed two layers, saved and ran device preset: passed');
    await page.locator('#transition-slot-B').selectOption('browser');
    await page.getByRole('button', { name: 'Load B', exact: true }).click();
    await page.waitForFunction(() => !document.querySelector('#transition-progress')?.disabled);
    const transitionApplied = waitForCommand('transition_set');
    await page.locator('#transition-progress').evaluate(input => {
      Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, '35');
      input.dispatchEvent(new Event('input', { bubbles: true }));
    });
    await transitionApplied;
    await page.waitForFunction(() => !document.querySelector('#transition-progress')?.disabled);
    const transition = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(transition.progress, 35); assert.equal(transition.A.layers.length, 2); assert.equal(transition.B.layers.length, 2);
    await page.locator('#transition-duration').fill('100');
    await page.getByRole('button', { name: 'Run to A', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('label[for="transition-progress"]')?.textContent.includes('0.0%'));
    console.log('Prepared B, set absolute slider progress, and completed a native timed transition: passed');

    for (const name of ['crossfade', 'wipe']) {
      await page.getByLabel('Load example').selectOption('transition_' + name);
      await page.getByRole('button', { name: 'Save and select transition →', exact: true }).waitFor();
      await page.waitForFunction(() => !document.querySelector('#upload')?.disabled);
      await page.getByLabel('Stored transition name').fill(name);
      await page.getByRole('button', { name: 'Save and select transition →', exact: true }).click();
      await page.waitForFunction(name => document.querySelector('#transition-program')?.value === name, name);
    }
    await page.getByLabel('Transition program').selectOption('crossfade');
    await page.getByRole('button', { name: 'Apply transition', exact: true }).click();
    await page.waitForFunction(() => !document.querySelector('#transition-progress')?.disabled);
    const pythonTransition = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(pythonTransition.procedure, 'crossfade');
    const programs = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--procedures'], { encoding: 'utf8' }));
    assert.deepEqual(programs.filter(program => ['crossfade', 'wipe'].includes(program.name)).map(program => program.profile), ['transition', 'transition']);
    console.log('Compiled both Python transition examples in the browser, stored them by name, and recalled crossfade: passed');

    await page.getByLabel('Load example').selectOption('color');
    await page.waitForFunction(() => document.querySelector('#check-state')?.textContent === 'All checks passed' && !document.querySelector('#upload')?.disabled);
    await page.locator('#animation-name').fill('Browser color');
    await page.getByRole('button', { name: 'Save animation', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#activity')?.textContent.includes('Browser color v1'));
    const faderLibrary = await page.evaluate(() => JSON.parse(localStorage.getItem('lightvm.library.v1')));
    assert.equal(faderLibrary.animations.length, 2);

    await page.locator('#command-path').selectOption('http');
    await page.waitForFunction(() => !document.querySelector('#command-path')?.disabled);
    await page.getByRole('link', { name: 'Fader', exact: true }).click();
    await page.getByRole('button', { name: 'Use fader', exact: true }).waitFor();
    const transitionPicker = page.getByRole('region', { name: 'Deployed transitions' });
    await transitionPicker.getByLabel('Transition program').selectOption('wipe');
    await transitionPicker.getByLabel('Override separator', { exact: true }).check();
    await transitionPicker.getByLabel('separator', { exact: true }).selectOption('true');
    await transitionPicker.getByLabel('Override color', { exact: true }).check();
    await transitionPicker.getByLabel('color', { exact: true }).fill('#ff6600');
    await page.getByRole('button', { name: 'Apply transition', exact: true }).click();
    await transitionPicker.getByText('Active transition: wipe', { exact: true }).waitFor();
    await page.screenshot({ path: path.join(root, 'build/fader-settings-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, 'Fader mobile layout overflows');
    await page.screenshot({ path: path.join(root, 'build/fader-settings-mobile.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1200 });
    await page.waitForFunction(() => !document.querySelector('#fader-duration')?.disabled && document.querySelector('#transition-fader')?.disabled);
    await page.locator('#fader-duration').fill('50');
    await page.locator('#fader-interval').fill('100');
    await page.getByRole('button', { name: 'Start timed demo' }).click();
    await page.getByText('Timed transition running', { exact: true }).waitFor();
    const timed = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(timed.driver.kind, 'timed'); assert.equal(timed.driver.interval_ms, 100);
    await page.getByRole('button', { name: 'Use fader', exact: true }).click();
    await page.getByText('Fader controls the transition', { exact: true }).waitFor();
    const identity = scene => scene.layers.map(layer => ({ id: layer.id, procedure: layer.procedure, generation: layer.generation }));
    const beforeSelect = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    await transitionPicker.getByLabel('Transition program').selectOption('crossfade');
    await page.getByRole('button', { name: 'Apply transition', exact: true }).click();
    await transitionPicker.getByText('Active transition: crossfade', { exact: true }).waitFor();
    await page.waitForFunction(() => !document.querySelector('#transition-fader')?.disabled);
    const afterSelect = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(afterSelect.procedure, 'crossfade'); assert.deepEqual(afterSelect.driver, beforeSelect.driver);
    assert.equal(afterSelect.progress, beforeSelect.progress);
    assert.deepEqual(identity(afterSelect.A), identity(beforeSelect.A)); assert.deepEqual(identity(afterSelect.B), identity(beforeSelect.B));
    console.log('Selected deployed wipe with colour settings and switched programs while retaining fader control: passed');
    const beforeFader = { ...counts };
    for (const progress of [0, 35, 100]) {
      if (Number(await page.locator('#transition-fader').inputValue()) !== progress) {
        const applied = waitForCommand('transition_update');
        await page.locator('#transition-fader').evaluate((input, progress) => {
          Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(input, String(progress));
          input.dispatchEvent(new Event('input', { bubbles: true }));
        }, progress);
        await applied;
      }
      const manual = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
      assert.equal(manual.driver.kind, 'source'); assert.equal(manual.driver.source, 'desktop.fader');
      assert.equal(manual.progress, progress); assert.equal(manual.A.layers.length, 2); assert.equal(manual.B.layers.length, 2);
    }
    await page.locator('#transition-fader').focus();
    const keyboard = waitForCommand('transition_update');
    await page.keyboard.press('Home'); await keyboard;
    if (deviceInfo.websocket_supported) {
      assert.ok(counts.socketFader > beforeFader.socketFader);
      assert.equal(counts.httpFader, beforeFader.httpFader);
      console.log('Manual fader updates used WebSocket with no HTTP fader requests: passed');
    }

    const beforeDrops = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    await page.getByRole('article', { name: 'Browser color v1' }).dragTo(page.getByRole('region', { name: 'Scene B' }));
    await page.getByRole('region', { name: 'Scene B' }).getByText('Browser color v1', { exact: true }).waitFor();
    await page.waitForFunction(() => !document.querySelector('#transition-fader')?.disabled);
    const loadedB = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    const colorAnimation = faderLibrary.animations.find(animation => animation.name === 'Browser color');
    assert.equal(loadedB.B.layers.length, 1); assert.equal(loadedB.B.layers[0].procedure, 'a' + colorAnimation.digest.slice(0, 31));
    assert.deepEqual(identity(loadedB.A), identity(beforeDrops.A));
    assert.deepEqual(loadedB.driver, beforeDrops.driver); assert.equal(loadedB.progress, 0);
    await page.getByRole('article', { name: 'Browser rainbow v1' }).dragTo(page.getByRole('region', { name: 'Scene A' }));
    await page.getByRole('region', { name: 'Scene A' }).getByText('Browser rainbow v1', { exact: true }).waitFor();
    await page.waitForFunction(() => !document.querySelector('#transition-fader')?.disabled);
    const loadedA = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(loadedA.A.layers.length, 1); assert.equal(loadedA.A.layers[0].procedure, programName);
    assert.deepEqual(identity(loadedA.B), identity(loadedB.B));
    assert.deepEqual(loadedA.driver, beforeDrops.driver); assert.equal(loadedA.progress, 0);
    console.log('Dragged different saved browser animations into A and B while preserving the other scene, progress, and external-source assignment: passed');

    await page.screenshot({ path: path.join(root, 'build/fader-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false, 'Fader mobile layout overflows');
    await page.screenshot({ path: path.join(root, 'build/fader-mobile.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1200 });
    await page.getByRole('button', { name: 'Hold', exact: true }).click();
    await page.getByText('Holding current position', { exact: true }).waitFor();
    const held = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--device', 'lights2', '--get-transition'], { encoding: 'utf8' }));
    assert.equal(held.driver.kind, 'manual'); assert.equal(held.progress, 0);
    await page.getByRole('link', { name: 'Develop', exact: true }).click();
    await page.locator('#saved-animation').selectOption(savedLibrary.animations[0].id);
    console.log('Fader tab took over the repeating timed demo, supplied external input at 0/35/100%, accepted keyboard input, and held without replacing scenes: passed');

    await page.evaluate(() => {
      const editor = document.querySelector('.CodeMirror')?.CodeMirror;
      editor.replaceRange('\n# draft survives tab changes', { line: editor.lineCount() });
    });
    await page.getByRole('link', { name: 'Configure', exact: true }).click();
    await page.getByRole('button', { name: 'Wi-Fi & HTTP' }).click();
    // Host simulator has HTTP but no Wi-Fi. Check the available network form
    // while background polls run; radio-specific credential checks need hardware.
    const portInput = page.locator('#http-port');
    await portInput.fill('80');
    await portInput.focus();
    await page.evaluate(() => {
      const input = document.querySelector('#http-port');
      input.focus();
      const check = { input, mutations: 0, blurs: 0 };
      input.addEventListener('blur', () => check.blurs++);
      check.observer = new MutationObserver(records => { check.mutations += records.length; });
      check.observer.observe(document.querySelector('#network-fields'), { attributes: true, attributeFilter: ['disabled'] });
      window.pollingCheck = check;
    });
    for (let count = 0; count < 2; count++) await waitForCommand('network_status');
    const stable = await page.evaluate(() => {
      const check = window.pollingCheck; check.observer.disconnect();
      const result = { focused: document.activeElement === check.input, value: check.input.value, mutations: check.mutations, blurs: check.blurs };
      delete window.pollingCheck; return result;
    });
    assert.deepEqual(stable, { focused: true, value: '80', mutations: 0, blurs: 0 });
    console.log('Background polling preserved network draft and focus: passed');

    await page.locator('#http-enabled').check();
    await page.locator('#http-port').fill('8080');
    await page.locator('#network-save').click();
    await page.waitForFunction(() => document.querySelector('#activity')?.textContent.includes('Network settings saved') && !document.querySelector('#network-fields')?.disabled);
    await page.getByRole('button', { name: 'Output & startup' }).click();
    await page.locator('#startup-procedure-0').selectOption(programName);
    await page.locator('#config-save').click();
    await page.waitForFunction(() => document.querySelector('#activity')?.textContent.includes('Output configuration applied') && !document.querySelector('#config-fields')?.disabled);
    await page.getByRole('link', { name: 'Develop', exact: true }).click();
    assert.equal(await page.evaluate(() => document.querySelector('.CodeMirror').CodeMirror.getValue().includes('# draft survives tab changes')), true);
    await page.getByRole('button', { name: 'Run on output', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#playback-state')?.textContent.includes('Running') && !document.querySelector('#upload')?.disabled);
    await page.waitForFunction(() => document.querySelector('#check-state')?.textContent === 'All checks passed');
    await page.screenshot({ path: path.join(root, 'build/network-editor-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    await page.screenshot({ path: path.join(root, 'build/network-editor-mobile.png'), fullPage: true });
    assert.equal(overflow, false, 'Mobile layout overflows');
    await page.getByRole('button', { name: 'Delete animation version', exact: true }).click();
    await page.getByRole('alert').filter({ hasText: 'A saved browser preset uses this version' }).waitFor();
    assert.equal(await page.locator('#saved-animation option').count(), 3);
    await page.getByRole('button', { name: 'Delete browser preset', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#saved-preset')?.options.length === 1);
    await page.getByRole('button', { name: 'Delete animation version', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#saved-animation')?.options.length === 2);
    assert.equal(await page.locator('#device-preset option').count(), 2);
    console.log('Browser deletion protects referenced versions and leaves device presets intact: passed');
    await page.getByRole('navigation', { name: 'Device workspace' }).getByRole('link', { name: /Firmware/ }).click();
    await page.getByRole('heading', { name: 'Firmware installation' }).waitFor();
    assert.equal(new URL(page.url()).hash.endsWith('/firmware'), true);
    if (deviceInfo.websocket_supported) {
      // Exercise the hosted editor's secure-context rules with a plain local
      // socket. Fulfil only the editor's HTTPS assets from the local server;
      // its HTTP bootstrap and ws:// connection still reach the real device.
      const secureOrigin = 'https://lightvm-editor.test';
      const secure = await browser.newPage();
      await grantLocalNetwork(cdp, secure, secureOrigin);
      secure.on('pageerror', error => errors.push(error.message));
      await secure.route(secureOrigin + '/**', async route => {
        const requested = new URL(route.request().url());
        const response = await route.fetch({ url: editorUrl + requested.pathname + requested.search });
        await route.fulfill({ response });
      });
      await secure.route('http://lightvm.local/api/v1/devices', async route => {
        const response = await route.fetch({ url: url + '/api/v1/devices' });
        await route.fulfill({ response });
      });
      await secure.goto(secureOrigin + '/#/devices');
      await secure.locator('#http-address').fill(url);
      await secure.locator('#connect-http').click();
      await secure.waitForFunction(() => document.querySelector('#path-health')?.textContent.includes('WebSocket: connected'));
      assert.equal(await secure.evaluate(() => isSecureContext), true);
      await secure.close();
      console.log('HTTPS editor connected to the plain local WebSocket: passed');
      receiver.kill();
      await page.waitForFunction(() => document.querySelector('#command-path')?.value === '' && document.querySelector('#notice')?.textContent.includes('WebSocket'));
      assert.equal(counts.sockets, 1, 'Socket loss must not reconnect automatically');
      console.log('Socket loss detached the selected path without automatic failover: passed');
    }
    assert.deepEqual(errors, []);
    console.log('Program, configuration and network tabs persisted; firmware route and mobile layout: passed');
  } catch (error) { if (page) { console.log(await page.locator("body").innerText()); await page.screenshot({ path: path.join(root, "build/network-editor-failure.png"), fullPage: true }); } throw error; } finally { await browser?.close(); receiver?.kill(); server?.kill(); fs.rmSync(directory, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
