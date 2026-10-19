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
    await page.route('http://lightvm.local/api/v1/controllers', async route => {
      const response = await route.fetch({ url: url + '/api/v1/controllers' });
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
    const errors = []; page.on('pageerror', error => { errors.push(error.message); console.error('BROWSER ERROR', error.stack); }); page.on("console", m => { if (m.type()==="error") console.log(m.text()); }); page.on("requestfailed", r => console.log("REQUEST FAILED", r.url(), r.failure()));
    // The serial API is simulated over a second logical HTTP session. Native
    // pipe/HTTP concurrency is covered independently by tests/test_http.py.
    await page.addInitScript(controller => {
      class Port {
        async open() {
          this.session = (await (await fetch(controller + '/api/v1/sessions', { method: 'POST' })).json()).session_id;
          this.readable = new ReadableStream({ start: controller => { this.reader = controller; } });
          this.writable = new WritableStream({ write: async bytes => {
            const response = await fetch(controller + '/api/v1/command', { method: 'POST', headers: { 'Content-Type': 'application/octet-stream', 'X-LightVM-Session': this.session }, body: bytes });
            this.reader.enqueue(new Uint8Array(await response.arrayBuffer()));
          } });
        }
        getInfo() { return { usbVendorId: 0x303a, usbProductId: 0x1001 }; }
        async setSignals() {}
        async close() { await fetch(controller + '/api/v1/sessions/' + this.session, { method: 'DELETE' }); }
      }
      Object.defineProperty(navigator, 'serial', { value: { requestPort: async () => new Port() } });
    }, url);

    await page.goto(editorUrl + '/#/controllers');
    await page.locator('#http-address').fill(url);
    await page.locator('#connect-http').click();
    await page.waitForURL('**/configuration');
    await page.getByRole('button', { name: 'Output & startup', exact: true }).click();
    await page.locator('#output-add').click();
    await page.locator('#config-count-0').fill('24');
    await page.locator('#config-save').click();
    await page.waitForFunction(() => document.querySelector('#output-binding')?.textContent.includes('24 pixels') && !document.querySelector('#config-fields')?.disabled);
    const controllerInfo = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--describe'], { encoding: 'utf8' }));
    assert.match(await page.locator('#path-health').innerText(), controllerInfo.websocket_supported ? /WebSocket: connected/ : /HTTP: connected/);
    console.log('Connected current simulator and configured output: passed');
    const tab = name => page.getByRole('navigation', { name: 'Workspace', exact: true }).getByRole('link', { name, exact: true });
    const ready = () => page.waitForFunction(() => document.querySelector('#check-state')?.textContent === 'All checks passed' && !document.querySelector('#deploy')?.disabled, null, { timeout: 90000 });
    await page.getByRole('link', { name: 'Connections & controllers' }).click();
    await page.locator('#connect').click();
    await page.waitForFunction(() => document.querySelector('#path-health')?.textContent.includes('USB: connected'));
    await page.locator('#command-path').selectOption('http');
    await tab('Editor').click(); await ready();
    // Both new frontends use the same real worker, host admission and exports.
    await page.getByLabel('Language', { exact: true }).selectOption('javascript'); await ready();
    for (const example of ['js_rainbow', 'js_pulse', 'js_trail', 'js_wipe']) {
      await page.getByLabel('Load example').selectOption(example); await ready();
    }
    await page.getByLabel('Load example').selectOption('js_trail'); await ready();
    await page.locator('#upload').click(); await page.getByText('Preview sent to lights.').waitFor();
    await page.getByLabel('Language', { exact: true }).selectOption('blocks'); await ready();
    await page.locator('.blocklySvg').first().waitFor();
    await page.getByLabel('Load example').selectOption('blocks_pulse'); await ready();
    await page.getByText('0.08', { exact: true }).click();
    await page.locator('.blocklyHtmlInput').fill('0.25');
    await page.locator('.blocklyHtmlInput').press('Enter');
    await page.getByText('0.25', { exact: true }).waitFor(); await ready();
    await page.locator('#upload').click(); await page.getByText('Preview sent to lights.').waitFor();
    const blocksDownload = page.waitForEvent('download');
    await page.getByRole('button', { name: /Save locally/ }).click();
    const blockFile = await blocksDownload, blockPath = path.join(directory, 'pulse.lightvm.json');
    await blockFile.saveAs(blockPath);
    const blockData = JSON.parse(fs.readFileSync(blockPath, 'utf8'));
    assert.equal(blockData.filename, 'pulse.blocks.json'); assert.ok(JSON.parse(blockData.source).blocks); assert.ok(blockData.source.includes('0.25'));
    await page.screenshot({ path: path.join(root, 'build/editor-blocks-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await page.getByRole('button', { name: 'Show toolbox', exact: true }).waitFor();
    assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'Blocks must fit the mobile viewport');
    await page.screenshot({ path: path.join(root, 'build/editor-blocks-mobile.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1200 });
    await page.getByLabel('Load example').selectOption('blocks_wipe'); await ready();
    await page.locator('#source-file').setInputFiles(blockPath); await ready();
    assert.equal(await page.locator('#filename').innerText(), 'pulse.blocks.json');
    await page.getByLabel('Language', { exact: true }).selectOption('javascript'); await ready();
    assert.equal(await page.locator('#filename').innerText(), 'trail.js');
    await page.getByLabel('Language', { exact: true }).selectOption('python'); await ready();
    assert.equal(await page.locator('#filename').innerText(), 'rainbow.py');
    console.log('JavaScript and independent blocks: worker compilation, device preview, editable file round trips and draft switching passed');

    await page.locator('#upload').click();
    await page.getByText('Preview sent to lights.').waitFor();
    const downloading = page.waitForEvent('download');
    await page.getByRole('button', { name: /Save locally/ }).click();
    const downloaded = await downloading, file = path.join(directory, downloaded.suggestedFilename());
    await downloaded.saveAs(file);
    assert.equal(JSON.parse(fs.readFileSync(file, 'utf8')).schema, 'lightvm.source');
    await page.locator('#deploy').click(); await page.getByText('rainbow deployed.').waitFor();
    for (const example of ['stars', 'transition_crossfade']) {
      await page.getByLabel('Load example').selectOption(example); await ready();
      await page.locator('#deploy').click(); await page.getByText(example.replace('transition_', '') + ' deployed.').waitFor();
    }
    await page.locator('#source-file').setInputFiles(file); await ready();
    assert.equal(await page.locator('#filename').innerText(), 'rainbow.py');
    assert.equal(await page.evaluate(() => localStorage.getItem('lightvm.library.v1')), null);
    await page.screenshot({ path: path.join(root, 'build/editor-workflow-desktop.png'), fullPage: true });
    console.log('Editor preview, external file round trip, and separate animation/transition deployment: passed');

    await tab('Configuration').click(); await page.getByRole('button', { name: 'Presets', exact: true }).click();
    for (const [name, program] of [['Rainbow', 'rainbow'], ['Stars', 'stars']]) {
      await page.getByRole('button', { name: 'New preset', exact: true }).click();
      await page.getByLabel('Preset name', { exact: true }).fill(name);
      await page.getByRole('button', { name: '+ Add layer', exact: true }).click();
      await page.locator('#preset-animation-layer1').selectOption(program);
      await page.getByLabel('Preset transition', { exact: true }).selectOption('crossfade');
      await page.getByRole('button', { name: 'Save preset to device', exact: true }).click();
      await page.waitForFunction(name => document.querySelector('#activity')?.textContent.includes('Saved preset ' + name) && !document.querySelector('#preset-name')?.disabled, name);
    }
    await page.getByRole('button', { name: 'Output & startup', exact: true }).click();
    await page.locator('#startup-mode-0').selectOption('preset');
    await page.locator('#startup-preset-0').selectOption('Rainbow');
    await page.locator('#config-save').click();
    await page.waitForFunction(() => !document.querySelector('#config-fields')?.disabled);
    await page.screenshot({ path: path.join(root, 'build/configuration-desktop.png'), fullPage: true });
    console.log('Device preset creation and startup configuration: passed');

    await tab('Mixer').click();
    await page.getByRole('button', { name: 'Load Rainbow into A', exact: true }).click();
    await page.getByRole('region', { name: 'Composition A', exact: true }).getByText('Rainbow', { exact: true }).first().waitFor();
    await page.getByRole('button', { name: 'Load Stars into B', exact: true }).click();
    await page.getByLabel('Transition program', { exact: true }).selectOption('crossfade');
    await page.getByRole('button', { name: 'Apply transition', exact: true }).click();
    await page.getByRole('button', { name: 'Use fader', exact: true }).click();
    await page.getByText('Fader controls the transition', { exact: true }).waitFor();
    const update = waitForCommand('transition_update');
    await page.locator('#transition-fader').fill('65'); await update;
    if (controllerInfo.websocket_supported) { assert.ok(counts.socketFader > 0); assert.equal(counts.httpFader, 0); }
    const keyboard = waitForCommand('transition_update'); await page.locator('#transition-fader').focus(); await page.keyboard.press('Home'); await keyboard;
    const move = waitForCommand('transition_update'); await page.locator('#transition-fader').fill('65'); await move;
    await page.screenshot({ path: path.join(root, 'build/mixer-desktop.png'), fullPage: true });
    assert.equal(await page.getByRole('button', { name: 'Swap A and B', exact: true }).isEnabled(), false);
    await page.getByRole('button', { name: 'Cut to B', exact: true }).click();
    await page.getByRole('button', { name: 'Swap A and B', exact: true }).click();
    await page.getByLabel('Fade duration (ms)').fill('100');
    await page.getByRole('button', { name: 'Run to B', exact: true }).click();
    await page.waitForFunction(() => document.querySelector('#transition-fader')?.value === '100');
    console.log('Mixer preset buses, transition selection, live fader, cut, swap and timed movement: passed');
    await tab('Editor').click(); await page.getByLabel('Load example').selectOption('transition_crossfade'); await ready();
    await page.locator('#upload').click(); await page.getByText('Preview sent to lights.').waitFor();
    await tab('Mixer').click(); await page.getByText('Active transition: _preview_transition', { exact: true }).waitFor();
    console.log('Transition scratch preview runs against the prepared A/B buses: passed');

    for (const name of ['Editor', 'Configuration', 'Mixer', 'Audio Playground']) {
      await tab(name).click();
      if (name === 'Audio Playground') {
        await page.getByLabel('brightness input minimum', { exact: true }).fill('40');
        await page.getByLabel('brightness input maximum', { exact: true }).fill('60');
        await page.screenshot({ path: path.join(root, 'build/audio-playground-desktop.png'), fullPage: true });
      }
      await page.setViewportSize({ width: 390, height: 844 });
      assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, name + ' overflows on mobile');
      // Paint the whole mobile document in one viewport; this avoids tiled
      // full-page captures racing the Mixer's live status repaint.
      const height = await page.evaluate(() => document.documentElement.scrollHeight);
      await page.setViewportSize({ width: 390, height });
      await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
      await page.screenshot({ path: path.join(root, 'build/' + name.toLowerCase().replace(/ /g, '-') + '-mobile.png') });
      await page.setViewportSize({ width: 1440, height: 1200 });
    }
    await tab('Configuration').click();
    await page.getByRole('button', { name: /Wi-Fi.*HTTP/ }).click();
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
    await tab('Mixer').click(); await tab('Audio Playground').click();
    assert.equal(await page.getByLabel('brightness input minimum', { exact: true }).inputValue(), '40');
    assert.equal(await page.getByLabel('brightness input maximum', { exact: true }).inputValue(), '60');
    await page.getByRole('link', { name: 'Firmware', exact: true }).click();
    await page.getByRole('heading', { name: 'Firmware installation' }).waitFor();
    await page.goBack();
    if (controllerInfo.websocket_supported) {
      // Exercise the hosted editor's secure-context rules with a plain local
      // socket. Fulfil only the editor's HTTPS assets from the local server;
      // its HTTP bootstrap and ws:// connection still reach the real controller.
      const secureOrigin = 'https://lightvm-editor.test';
      const secure = await browser.newPage();
      await grantLocalNetwork(cdp, secure, secureOrigin);
      secure.on('pageerror', error => errors.push(error.message));
      await secure.route(secureOrigin + '/**', async route => {
        const requested = new URL(route.request().url());
        const response = await route.fetch({ url: editorUrl + requested.pathname + requested.search });
        await route.fulfill({ response });
      });
      await secure.route('http://lightvm.local/api/v1/controllers', async route => {
        const response = await route.fetch({ url: url + '/api/v1/controllers' });
        await route.fulfill({ response });
      });
      await secure.goto(secureOrigin + '/#/controllers');
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
    console.log('All four workspaces: desktop/mobile layout, audio tuning retained across navigation, no browser errors: passed');
  } catch (error) { if (page) { console.log(await page.locator('body').innerText()); await page.screenshot({ path: path.join(root, 'build/network-editor-failure.png'), fullPage: true }); } throw error; }
  finally { await browser?.close(); receiver?.kill(); server?.kill(); fs.rmSync(directory, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
