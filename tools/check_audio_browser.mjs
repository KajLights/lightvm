// Silent AudioWorklet conformance against the pinned shared analyser.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { gunzipSync } from 'node:zlib';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { chromium } from 'playwright';
import { AnalysisRunner, RECORD, PROFILES } from '../web/audio/pcm-adapter.mjs';

const root = fileURLToPath(new URL('../', import.meta.url));
const core = path.resolve(process.env.KAJ_AUDIO_CORE || path.join(root, '.deps/kaj-fi/core'));
const fixture = gunzipSync(fs.readFileSync(path.join(core, '../tests/fixtures/conformance.pcm.gz')));
const corpus = JSON.parse(fs.readFileSync(path.join(core, '../tests/fixtures/conformance.json')));
assert.equal(createHash('sha256').update(fixture).digest('hex'), corpus.sha256);
const revision = fs.readFileSync(path.join(root, 'kaj-audio.rev'), 'utf8').trim();
assert.equal(execFileSync(process.env.PYTHON || 'python3', [path.join(root, 'tools/audio_dependency.py'), '--verify', core], { encoding: 'utf8' }).trim(), revision);
assert.equal(fs.readFileSync(path.join(root, 'build/audio/revision.txt'), 'utf8').trim(), revision);
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'build/audio/profiles.json')));
const parameters = Object.fromEntries(Object.entries(manifest.parameters).map(([key, value]) => [key, value.default]));
const { default: create } = await import(pathToFileURL(path.join(root, 'build/audio/kaj-audio.mjs')));
const module = await create(), memory = module.HEAP16.buffer;
const runner = new AnalysisRunner(module, parameters);
const samples = Float32Array.from(new Int16Array(fixture.buffer, fixture.byteOffset, fixture.length / 2), n => n / 32768);
for (let frame = 0; frame < samples.length; frame += 128) runner.process([samples.subarray(frame, frame + 128)], frame, 1000000, 0);
const reference = Array.from({ length: runner.historyCount * PROFILES }, (_, i) => Array.from(runner.history.subarray(i * RECORD, (i + 1) * RECORD)));
runner.reset(0);
for (let frame = 0, turn = 0; frame < samples.length;) {
  const count = Math.min([1, 37, 511, 128, 251][turn++ % 5], samples.length - frame);
  runner.process([samples.subarray(frame, frame + count)], frame, 1000000, 0); frame += count;
}
for (let row = 0; row < reference.length; row++) {
  const actual = Array.from(runner.history.subarray(row * RECORD, (row + 1) * RECORD)); actual[154] = reference[row][154];
  assert.deepEqual(actual, reference[row], 'Irregular callback blocks must not change analysis or timestamps');
}
const beforeGap = runner.epoch, silence = new Float32Array(128);
for (let frame = samples.length + 128; frame < samples.length + 128 + 4096; frame += 128) runner.process([silence], frame, 1000000, 0);
assert.ok(runner.epoch > beforeGap); assert.equal(runner.hops, 3); assert.equal(runner.latest[3], 0); assert.equal(runner.latest[4], 0);
assert.equal(module.HEAP16.buffer, memory, 'The WASM heap must not grow during analysis/reset');
runner.destroy();
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'lightvm-audio-native-'));
let native;
try {
  const binary = path.join(directory, 'audio-fixture');
  execFileSync(process.env.CC || 'cc', ['-std=c11', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I' + core,
    path.join(root, 'tests/audio_host_fixture.c'), ...['host.c', 'features.c', 'onset.c', 'fft_portable.c'].map(name => path.join(core, name)), '-lm', '-o', binary]);
  const result = execFileSync(binary, { input: fixture, maxBuffer: 4 * 1024 * 1024 });
  native = new Float32Array(result.buffer.slice(result.byteOffset, result.byteOffset + result.length));
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
assert.equal(native.length, reference.length * 90);
let nativeError = 0;
for (let row = 0; row < reference.length; row++) {
  const actual = reference[row], n = native.subarray(row * 90, (row + 1) * 90);
  for (let i = 0; i < 70; i++) {
    const error = Math.abs(actual[i + 4] - n[i]); nativeError = Math.max(nativeError, error);
    assert.ok(error <= (i === 4 ? 1e-3 : 1e-4), `native row ${row}, metric ${i}`);
  }
  assert.deepEqual(actual.slice(74, 91), Array.from(n.subarray(73, 90)), 'Exact native/WASM band edges');
  const event = actual[3] > 0 && actual[142 + (actual[3] - 1) * 3] === actual[2];
  assert.equal(event, n[72] > 0, `native event occurrence on row ${row}`);
  if (event) assert.ok(Math.abs(actual[143 + (actual[3] - 1) * 3] - n[72]) <= 1e-4);
}
let server, browser;
try {
  server = spawn(process.execPath, [path.join(root, 'node_modules/vite/bin/vite.js'), '--host', '127.0.0.1', '--port', '0'], { cwd: root, stdio: ['ignore', 'pipe', 'pipe'] });
  const url = await new Promise((resolve, reject) => {
    let output = ''; const timer = setTimeout(() => reject(Error(`Vite startup timeout: ${output}`)), 10000);
    server.on('error', reject); server.on('exit', code => reject(Error(`Vite exited ${code}: ${output}`)));
    const read = bytes => { output += bytes; const match = output.match(/http:\/\/127\.0\.0\.1:\d+\//); if (match) { clearTimeout(timer); resolve(match[0]); } };
    server.stdout.on('data', read); server.stderr.on('data', read);
  });
  browser = await chromium.launch({ headless: true, ...(process.env.CHROME_EXECUTABLE ? { executablePath: process.env.CHROME_EXECUTABLE } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
  const page = await browser.newPage();
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  page.on('console', msg => { if (msg.type() === 'error') console.error(msg.text()); });
  await page.addInitScript(() => {
    window.audioSpeakerConnections = 0;
    const connect = AudioNode.prototype.connect;
    AudioNode.prototype.connect = function (destination, ...rest) {
      if (destination instanceof AudioDestinationNode) { window.audioSpeakerConnections++; throw Error('No speaker connection is permitted.'); }
      return connect.call(this, destination, ...rest);
    };
    navigator.mediaDevices.getUserMedia = async () => { throw Error('This check permits only silent fixture input.'); };
  });
  await page.route('**/fixture.pcm', route => route.fulfill({ body: fixture, contentType: 'application/octet-stream' }));
  await page.goto(url);
  const result = await page.evaluate(async parameters => {
    const { MicrophoneAudio, readAudioFixture } = await import('/src/audio-features.ts');
    let frames = 0, epochs = [], problem;
    const file = new File([await (await fetch('/fixture.pcm')).arrayBuffer()], 'conformance.pcm');
    const fixture = await readAudioFixture(file);
    let ended; const finished = new Promise(resolve => { ended = resolve; });
    const input = new MicrophoneAudio(snapshot => { frames++; const epoch = snapshot.live[0]?.epoch; if (epochs.at(-1) !== epoch) epochs.push(epoch); }, error => { problem = error.message; ended(); }, ended);
    await input.start('', parameters, -1, fixture);
    // Deliberately hold the sole transferable mailbox on the main thread. DSP
    // and every-hop export must continue independently; no stale queue replay.
    setTimeout(() => { const until = performance.now() + 500; while (performance.now() < until) {} }, 2500);
    await Promise.race([finished, new Promise((_, reject) => setTimeout(() => reject(Error('Fixture timeout')), 20000))]);
    const report = await input.export(); await input.stop();
    return { connectedToSpeakers: window.audioSpeakerConnections, frames, epochs, problem, report };
  }, parameters);
  assert.deepEqual(errors, []); assert.equal(result.problem, undefined); assert.equal(result.connectedToSpeakers, 0);
  fs.writeFileSync(path.join(root, 'build/audio-browser-observed.json'), JSON.stringify(result));
  console.log(JSON.stringify({ frames: result.frames, epochs: result.epochs, rows: result.report.records.length, reference_rows: reference.length, first: result.report.records[0]?.slice(0, 4) }));
  assert.equal(result.report.records.length, reference.length, 'Every ready analysis hop must survive fixture replay.');
  assert.equal(result.epochs.length, 1, 'Main-thread stalls must not reset analysis.');
  const captureBase = result.report.records[0][2] - reference[0][2];
  let browserError = 0;
  for (let row = 0; row < reference.length; row++) {
    const actual = result.report.records[row], expected = reference[row];
    assert.ok(Math.abs(actual[2] - expected[2] - captureBase) <= 1, 'Sample timestamps retain one fixed correlation');
    assert.equal(actual[3], expected[3], 'Event identity/count retained across main-thread stall');
    for (let e = 0; e < expected[3]; e++) {
      assert.equal(actual[141 + e * 3], expected[141 + e * 3]);
      assert.ok(Math.abs(actual[142 + e * 3] - expected[142 + e * 3] - captureBase) <= 1);
      assert.equal(actual[143 + e * 3], expected[143 + e * 3]);
    }
    assert.equal(actual[155], expected[155]); assert.equal(actual[156], expected[156]);
    for (let i = 4; i < 141; i++) {
      const error = Math.abs(actual[i] - expected[i]); browserError = Math.max(browserError, error);
      assert.ok(error <= (i === 8 ? 1e-3 : 1e-4), `row ${row}, column ${i}`);
    }
  }
  await page.goto(url + '#/audio');
  await page.getByLabel('PCM fixture', { exact: true }).setInputFiles({ name: 'conformance.pcm', mimeType: 'application/octet-stream', buffer: fixture });
  await page.getByText('conformance.pcm', { exact: false }).waitFor();
  await page.getByRole('checkbox', { name: 'Compare both algorithms' }).check();
  await page.getByRole('button', { name: 'Replay fixture silently' }).click();
  await page.getByText('Listening · shared local audio analysis', { exact: true }).waitFor();
  await page.waitForFunction(() => document.querySelector('#meter-1-level')?.value > 0.1);
  await page.setViewportSize({ width: 1440, height: 1200 });
  await page.screenshot({ path: path.join(root, 'build/audio-comparison-desktop.png'), fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true, 'Audio comparison must fit mobile');
  // One paint avoids tiled screenshots racing the live audio meter repaint.
  await page.setViewportSize({ width: 390, height: await page.evaluate(() => document.documentElement.scrollHeight) });
  await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  await page.screenshot({ path: path.join(root, 'build/audio-comparison-mobile.png') });
  await page.getByText('Fixture complete · comparison ready to export', { exact: true }).waitFor({ timeout: 20000 });
  const download = page.waitForEvent('download'); await page.getByRole('button', { name: 'Export comparison' }).click();
  const downloaded = await download, report = JSON.parse(fs.readFileSync(await downloaded.path(), 'utf8'));
  assert.equal(report.input.sha256, corpus.sha256); assert.equal(report.manifest.dependency, revision);
  assert.deepEqual(report.parameters, parameters); assert.equal(report.records.length, reference.length); assert.equal(report.streams.length, 2);
  assert.equal(await page.evaluate(() => window.audioSpeakerConnections), 0); assert.deepEqual(errors, []);
  const evidence = { fixture_sha256: corpus.sha256, core_revision: revision, browser: await browser.version(), profiles: [1, 2],
    ready_hops_per_profile: reference.length / 2, native_metric_max_error: nativeError, audio_worklet_max_error: browserError,
    event_ids_and_hops_equal: true, exact_band_edges: true, main_thread_stall_ms: 500, irregular_blocks_equal: true, gap_resets_history: true,
    wasm_heap_bytes: memory.byteLength, ui_fixture_export: true, desktop_and_mobile_layout: true, speaker_connections: result.connectedToSpeakers };
  fs.writeFileSync(path.join(root, 'build/audio-browser-validation.json'), JSON.stringify(evidence, null, 2) + '\n');
  console.log(JSON.stringify(evidence, null, 2));
} finally { await browser?.close(); server?.kill(); }
