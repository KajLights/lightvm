// Optional browser acceptance check. Requires Playwright, Chromium/Chrome with
// local-network permission support, and access to the pinned compiler assets.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const { spawn, execFileSync } = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
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
    await cdp.send('Browser.grantPermissions', { origin: editorUrl, permissions: ['localNetwork', 'loopbackNetwork', 'localNetworkAccess'] });
    page = await browser.newPage({ viewport: { width: 1440, height: 1200 } });
    // Route only the bootstrap hostname to the actual simulator metadata.
    // DNS packet interoperability is tested separately; HTTP and UI are real.
    await page.route('http://lightvm.local/api/v1/devices', async route => {
      const response = await route.fetch({ url: url + '/api/v1/devices' });
      await route.fulfill({ response });
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
    await page.waitForFunction(() => document.querySelector('#discovery-state').textContent.includes('1 device found'));
    assert.equal(await page.locator('#http-address').inputValue(), url);
    assert.equal(await page.locator('#nearby-devices option').count(), 2);
    await page.locator('#connect-http').click();
    await page.waitForFunction(() => document.querySelector('#path-health').textContent.includes('HTTP: connected') && !document.querySelector('#config-fields').disabled);
    console.log('Automatic discovery selects the bridge IP and connects through the device picker: passed');
    const config = { schema: 'lightvm.config/1', devices: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 5 } }], targets: { A: { device: 'lights' } } };
    await page.locator('#config-json').fill(JSON.stringify(config)); await page.locator('#config-save').click();
    await page.waitForFunction(() => document.querySelector('#output-binding').textContent.includes('5 pixels') && !document.querySelector('#config-fields').disabled);
    await page.locator('#connect').click();
    await page.waitForFunction(() => document.querySelector('#path-health').textContent.includes('USB: connected') && !document.querySelector('#command-path').disabled);
    await page.locator('#command-path').selectOption('http');
    await page.waitForFunction(() => !document.querySelector('#command-path').disabled);
    await page.waitForFunction(() => document.querySelector('#check-state').textContent === 'All checks passed' || document.querySelector('#compiler-state').textContent === 'Compiler unavailable', null, { timeout: 60000 });
    const compiler = await page.locator('#compiler-state').innerText();
    assert.equal(compiler, 'Compiler ready', 'This check requires the pinned browser compiler assets');
    {
      await page.locator('#upload').click();
      await page.waitForFunction(() => document.querySelector('#parameter-state').textContent.includes('Running') && !document.querySelector('#parameter-fields').disabled);
      await page.locator('#procedure-name').fill('browser'); await page.locator('#procedure-save').click();
      await page.waitForFunction(() => document.querySelector('#procedure-select').value === 'browser' && !document.querySelector('#procedure-fields').disabled);
      await page.locator('#command-path').selectOption('usb');
      await page.waitForFunction(() => !document.querySelector('#parameter-fields').disabled && !document.querySelector('#command-path').disabled);
      await page.locator('#parameters input[data-name=speed]').fill('0.7'); await page.locator('#parameters-apply').click();
      await page.waitForFunction(() => document.querySelector('#parameters input[data-name=speed]').value === '0.7' && !document.querySelector('#parameter-fields').disabled);
    }
    // Provision through the UI while playback continues. The host has no radio.
    await page.locator('#wifi-add').click();
    const row = page.locator('.wifi-network').first();
    await row.locator('input').nth(0).fill('café🌈');
    const password = 'browser-test-password';
    await row.locator('input[type=password]').fill(password);
    // Observe real background polls while the user is editing a credential.
    // Neither disabling the form nor rebuilding a select is visually harmless.
    await page.evaluate(() => {
      const input = document.querySelector('.wifi-network input[type=password]');
      input.focus(); input.setSelectionRange(2, 6);
      const check = { input, options: [...document.querySelector('#command-path').options], mutations: 0, blurs: 0 };
      input.addEventListener('blur', () => check.blurs++);
      check.observer = new MutationObserver(records => { check.mutations += records.length; });
      check.observer.observe(document.querySelector('#command-path'), { childList: true });
      for (const fields of document.querySelectorAll('#management fieldset')) check.observer.observe(fields, { attributes: true, attributeFilter: ['disabled'] });
      window.pollingCheck = check;
    });
    for (let count = 0; count < 2; count++) await page.waitForResponse(response => {
      const body = response.request().postDataBuffer();
      return response.ok() && body?.subarray(16).toString().includes('"op":"network_status"');
    }, { timeout: 12000 });
    const stable = await page.evaluate(() => {
      const check = window.pollingCheck; check.observer.disconnect();
      const options = [...document.querySelector('#command-path').options];
      const result = { focused: document.activeElement === check.input, selection: [check.input.selectionStart, check.input.selectionEnd],
        mutations: check.mutations, blurs: check.blurs, sameOptions: options.length === check.options.length && options.every((option, index) => option === check.options[index]) };
      delete window.pollingCheck; return result;
    });
    assert.deepEqual(stable, { focused: true, selection: [2, 6], mutations: 0, blurs: 0, sameOptions: true });
    assert.equal(await row.locator('input[type=password]').inputValue(), password);
    assert.equal(await row.locator('input').first().inputValue(), 'café🌈');
    console.log('Background polling preserves focused credentials, selection, draft values and form/selector nodes: passed');
    await page.locator('#http-enabled').check();
    await page.locator('#http-port').fill('8080');
    await page.locator('#network-save').click();
    await page.waitForFunction(() => document.querySelector('#activity').textContent.includes('Network settings saved') && !document.querySelector('#network-fields').disabled);
    assert.equal(await row.locator('input[type=password]').inputValue(), '');
    assert.equal(await page.evaluate(secret => Object.values(localStorage).some(value => value.includes(secret)), password), false);
    assert.equal((await page.locator('#activity').innerText()).includes(password), false);
    assert.match(await page.locator('#parameter-state').innerText(), /Running/);
    await page.locator('#network-refresh').click();
    await page.waitForFunction(() => !document.querySelector('#network-fields').disabled);
    assert.equal(await page.locator('.wifi-network input').first().inputValue(), 'café🌈');

    // The other client invalidates the displayed parameter revision. The UI
    // refreshes after the conflict and leaves the winning value in place.
    execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--set-parameters', '{"speed":0.8}']);
    await page.locator('#parameters input[data-name=speed]').fill('0.9');
    await page.locator('#parameters-apply').click();
    await page.waitForFunction(() => document.querySelector('#notice').textContent.includes('Review it before trying again') && !document.querySelector('#parameter-fields').disabled);
    assert.equal(await page.locator('#parameters input[data-name=speed]').inputValue(), '0.8');

    await page.locator('#program-pause').click();
    await page.waitForFunction(() => document.querySelector('#parameter-state').textContent.includes('Paused') && !document.querySelector('#parameter-fields').disabled);
    await page.locator('#program-resume').click();
    await page.waitForFunction(() => document.querySelector('#parameter-state').textContent.includes('Running') && !document.querySelector('#parameter-fields').disabled);
    const downloadReady = page.waitForEvent('download');
    await page.locator('#procedure-download').click();
    const downloaded = await downloadReady;
    assert.equal(downloaded.suggestedFilename(), 'browser.wasm');
    assert.equal(fs.readFileSync(await downloaded.path()).subarray(0, 4).toString('hex'), '0061736d');
    await page.screenshot({ path: path.join(root, 'build/network-editor-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth);
    await page.screenshot({ path: path.join(root, 'build/network-editor-mobile.png'), fullPage: true });
    console.log(JSON.stringify({ compiler, errors, mobileOverflow: overflow, paths: await page.locator('#path-health').innerText(), output: await page.locator('#output-binding').innerText() }));
    if (errors.length || overflow) throw new Error('Browser layout or runtime verification failed');
    await page.setViewportSize({ width: 1440, height: 1200 });
    await page.locator('#startup-procedure').selectOption('browser');
    await page.locator('#startup-save').click();
    await page.waitForFunction(() => document.querySelector('#activity').textContent.includes('Startup selection saved') && !document.querySelector('#config-fields').disabled);
    await page.locator('#procedure-activate').click();
    await page.waitForFunction(() => document.querySelector('#parameter-state').textContent.includes('Running') && !document.querySelector('#parameter-fields').disabled);
    await page.locator('#startup-procedure').selectOption('');
    await page.locator('#startup-save').click();
    await page.waitForFunction(() => !document.querySelector('#config-fields').disabled && document.querySelector('#parameter-fields').disabled);
    await page.locator('#procedure-delete').click();
    await page.waitForFunction(() => !document.querySelector('#procedure-fields').disabled && !document.querySelector('#procedure-select').options.length);
    const saved = JSON.parse(execFileSync(python, [path.join(root, 'tools/client.py'), '--url', url, '--get-config', '--saved'], { encoding: 'utf8' }));
    assert.equal(saved.startup ?? null, null);
    assert.deepEqual(errors, []);
    console.log('Network form, credential handling, conflict refresh, pause/resume, download, activation, startup and deletion: passed');
  } catch (error) { if (page) { console.log(await page.locator("body").innerText()); await page.screenshot({ path: path.join(root, "build/network-editor-failure.png"), fullPage: true }); } throw error; } finally { await browser?.close(); receiver?.kill(); server?.kill(); fs.rmSync(directory, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
