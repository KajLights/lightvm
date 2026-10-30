// Real browser + native engine: editing a stored recipe cannot acquire sources.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { stripVTControlCharacters } from 'node:util';
import { chromium } from 'playwright';
import { LightVMClient, crc32 } from '../web/protocol.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'lightvm-source-editor-'));
const source = `from lightvm import profile,render,Input,rgb
profile("animation")
LEVEL=Input("level",default=0.25,minimum=0.0,maximum=1.0)
EVENTS=Input("events",default=0)
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(runtime.input(LEVEL)*100.0),0,runtime.input(EVENTS)%256))
`;
const sources = {
  room: { type: 'lvaf', address: '127.0.0.1', port: 59999, analysis_profile: 2, source_id: '8000000000000001', allow_synthetic: true },
  desk: { type: 'gateway', name: 'desk', analysis_profile: 1, allow_synthetic: false },
};
function announced(child, output) {
  return new Promise((resolve, reject) => {
    let text = '';
    const timer = setTimeout(() => finish(Error('Server startup timeout: ' + text)), 10000);
    const read = bytes => { text += bytes; const match = stripVTControlCharacters(text).match(/http:\/\/[^\s]+/); if (match) finish(null, match[0].replace(/\/$/, '')); };
    const failed = error => finish(error), exited = code => finish(Error(`Server exited ${code}: ${text}`));
    function finish(error, url) {
      clearTimeout(timer); output.off('data', read); child.off('error', failed); child.off('exit', exited);
      if (error) reject(error); else resolve(url);
    }
    output.on('data', read); child.once('error', failed); child.once('exit', exited);
  });
}
async function eventually(read, expected) {
  const until = Date.now() + 10000;
  do { const value = await read(); if (value === expected) return; await new Promise(resolve => setTimeout(resolve, 50)); } while (Date.now() < until);
  assert.equal(await read(), expected);
}
let receiver, server, browser, client;
try {
  const bytecode = execFileSync(process.env.LIGHTVM_PYTHON || 'python3', ['-c',
    'import sys; from lightvm.compiler import compile_source; r=compile_source(sys.stdin.read()); assert r.success,r.diagnostics; sys.stdout.buffer.write(r.bytecode)'], { input: source, cwd: root });
  receiver = spawn(process.env.LIGHTVM_RECEIVER || path.join(root, 'build/host/lightvm'), ['--http', '0', '--storage', path.join(directory, 'store.bin')]);
  const controllerUrl = await announced(receiver, receiver.stderr);
  const transport = { onData() {}, onDisconnect() {},
    write: bytes => new Promise((resolve, reject) => receiver.stdin.write(bytes, error => error ? reject(error) : resolve())),
    close: async () => { receiver.stdin.end(); } };
  client = new LightVMClient(transport);
  receiver.stdout.on('data', bytes => transport.onData(bytes));
  receiver.stdin.on('error', error => transport.onDisconnect(error));
  await client.configure({ schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 5 } }] });
  await client.saveProgram('reactive', bytecode);
  await client.savePreset('mixed', [{ id: 'main', program: 'reactive', crc32: crc32(bytecode), opacity: 255, blend: 'over',
    inputs: { level: { source: 'room.band3' }, events: { source: 'desk.onsets' } } }], 'lights', undefined, undefined, sources);
  const providers = async () => (await client.json('audio_status')).providers;
  assert.deepEqual(await providers(), []);
  console.log('Seeded stored UDP/gateway recipe without acquiring providers.');
  server = spawn(process.execPath, [path.join(root, 'node_modules/vite/bin/vite.js'), '--host', '127.0.0.1', '--port', '0'], { cwd: root });
  const editorUrl = await announced(server, server.stdout);
  browser = await chromium.launch({ headless: true, ...(process.env.CHROME_EXECUTABLE ? { executablePath: process.env.CHROME_EXECUTABLE } : {}) });
  const page = await browser.newPage({ viewport: { width: 1440, height: 1200 } });
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  const session = await page.context().newCDPSession(page), { targetInfo } = await session.send('Target.getTargetInfo');
  await session.detach();
  const cdp = await browser.newBrowserCDPSession();
  await cdp.send('Browser.grantPermissions', { origin: editorUrl, browserContextId: targetInfo.browserContextId, permissions: ['localNetwork', 'loopbackNetwork', 'localNetworkAccess'] });
  await page.route('http://lightvm.local/api/v1/controllers', async route => {
    const response = await route.fetch({ url: controllerUrl + '/api/v1/controllers' }); await route.fulfill({ response });
  });
  await page.goto(editorUrl + '/#/controllers');
  await page.locator('#http-address').fill(controllerUrl); await page.locator('#connect-http').click();
  await page.getByRole('navigation', { name: 'Workspace', exact: true }).getByRole('link', { name: 'Configuration', exact: true }).click();
  await page.getByRole('button', { name: 'Presets', exact: true }).click();
  await page.locator('#controller-preset').selectOption('mixed');
  await page.getByRole('button', { name: 'Open preset', exact: true }).click();
  await page.getByLabel('Source 1 address', { exact: true }).waitFor();
  assert.equal(await page.getByLabel('Source 1 address', { exact: true }).inputValue(), sources.room.address);
  assert.equal(await page.getByLabel('Source 1 profile', { exact: true }).inputValue(), '2');
  assert.equal(await page.getByLabel('Source 1 identity pin', { exact: true }).inputValue(), sources.room.source_id);
  assert.equal(await page.getByLabel('Source 1 allow synthetic', { exact: true }).isChecked(), true);
  const opacity = page.getByLabel('Layer 1 opacity', { exact: true }); await opacity.press('End'); await opacity.press('ArrowLeft');
  await page.getByText('Settings & inputs', { exact: true }).click();
  await page.getByText('What controls this animation?', { exact: true }).click();
  await page.getByLabel('events driver', { exact: true }).selectOption('desk.onsets');
  await page.getByLabel('level driver', { exact: true }).selectOption('room.band63');
  await page.getByRole('button', { name: 'Save preset to device', exact: true }).click();
  await eventually(async () => (await client.json('preset_get', { name: 'mixed' })).layers[0].opacity, 254);
  let recipe = await client.json('preset_get', { name: 'mixed' });
  assert.deepEqual(recipe.sources, sources);
  assert.deepEqual(recipe.layers[0].inputs.events, { source: 'desk.onsets' });
  assert.equal(recipe.layers[0].inputs.level.source, 'room.band63');
  assert.deepEqual(await providers(), [], 'Saving must not acquire even the otherwise-unused UDP provider');
  console.log('Browser edit preserves both declarations, counter binding and inactive subscriptions.');
  await client.activatePreset('mixed', 'lights');
  assert.equal((await providers()).length, 2);
  await page.evaluate(async controller => {
    const { LightVMClient } = await import('/protocol.mjs'), { HttpTransport } = await import('/transports.mjs');
    const { audioNow, audioHex, encodeAudioRecord } = await import('/src/audio-wire.ts');
    window.sourceTestPublisher = new LightVMClient(await new HttpTransport(controller).open());
    const claim = await window.sourceTestPublisher.json('audio_claim', { name: 'desk', source_id: '9000000000000007', session: 'f000000000000008', analysis_profile: 1 });
    await window.sourceTestPublisher.manage('audio_clock', { boot_id: claim.boot_id, generation: claim.generation, challenge: claim.challenge, publisher_us: audioHex(audioNow()) });
    const now = audioNow();
    await window.sourceTestPublisher.publishAudio(claim.generation, 1n, encodeAudioRecord({ source: 0x9000000000000007n, session: 0xf000000000000008n, sequence: 1n,
      audioUs: now, synthetic: false, values: [0.7, 0.3, 0, 0, 0, 0], bands: Array(16).fill(0.3),
      edges: Array.from({ length: 17 }, (_, i) => 2 + Math.floor(i * 510 / 16)), events: [] }, audioNow()));
  }, controllerUrl);
  const pin = page.getByRole('button', { name: 'Pin observed identity 9000000000000007', exact: true }); await pin.waitFor();
  assert.equal((await providers()).find(provider => provider.type === 'gateway').bands, 16);
  await page.screenshot({ path: path.join(root, 'build/audio-sources-desktop.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 2400 });
  assert.ok(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1), 'Source editor must fit mobile width');
  await page.screenshot({ path: path.join(root, 'build/audio-sources-mobile.png'), fullPage: true });
  await pin.click(); await page.getByRole('button', { name: 'Save preset to device', exact: true }).click();
  await eventually(async () => (await client.json('preset_get', { name: 'mixed' })).sources.desk.source_id, '9000000000000007');
  assert.equal((await providers()).find(provider => provider.type === 'gateway').expected_source_id, '0000000000000000', 'Editing a recipe must not change the running scene policy');
  await client.activatePreset('mixed', 'lights');
  await eventually(async () => (await providers()).find(provider => provider.type === 'gateway' && provider.references > 0)?.expected_source_id, '9000000000000007');
  recipe = await client.json('preset_get', { name: 'mixed' });
  assert.deepEqual(recipe.sources.room, sources.room); assert.deepEqual(errors, []);
  await page.evaluate(() => window.sourceTestPublisher.close());
  const report = { result: 'passed', browser: await browser.version(), transports: ['lvaf', 'gateway'],
    checks: ['stored declarations preserved', 'spectrum and integer onset bindings', 'save acquires no providers', 'live provider status',
      'explicit identity pin adoption', 'save preserves active policy', 'activation applies edited policy', 'desktop and mobile layout'], speakerPlayback: false };
  fs.writeFileSync(path.join(root, 'build/audio-sources-browser-validation.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify(report));
} finally {
  await browser?.close(); await client?.close(); server?.kill(); receiver?.kill(); fs.rmSync(directory, { recursive: true, force: true });
}
