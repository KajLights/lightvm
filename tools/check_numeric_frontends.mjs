#!/usr/bin/env node
// Production worker acceptance: real browser + Pyodide + native VM execution.
// Run npm run build first; requires Playwright Chromium or CHROME_EXECUTABLE.
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createServer } from 'node:http';
import { readFileSync, readdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, extname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';

const root = fileURLToPath(new URL('..', import.meta.url));
const assets = join(root, 'build/web');
const worker = readdirSync(join(assets, 'assets')).find(name => /^compiler-worker-.*\.js$/.test(name));
assert(worker, 'Build the editor before testing its compiler worker');
assert(process.env.LIGHTVM_NUMERIC_RUNTIME, 'Set LIGHTVM_NUMERIC_RUNTIME to the native test_platform');
const server = createServer((request, response) => {
  const file = resolve(assets, '.' + new URL(request.url, 'http://localhost').pathname);
  if (!file.startsWith(assets + '/')) { response.writeHead(403).end(); return; }
  try {
    response.setHeader('Content-Type', ({ '.js': 'text/javascript', '.mjs': 'text/javascript', '.html': 'text/html', '.wasm': 'application/wasm' })[extname(file)] || 'text/plain');
    response.end(readFileSync(file));
  } catch { response.writeHead(404).end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const url = `http://127.0.0.1:${server.address().port}/`;
const directory = mkdtempSync(join(tmpdir(), 'lightvm-browser-numeric-'));
let browser;
try {
  browser = await chromium.launch({ headless: true, ...(process.env.CHROME_EXECUTABLE ? { executablePath: process.env.CHROME_EXECUTABLE } : {}) });
  const page = await browser.newPage();
  await page.goto(url + 'index.html');
  const fixtures = ['python', 'javascript', 'blocks'].map((language, i) => ({ language,
    source: readFileSync(join(root, 'tests/fixtures/frontend-numeric/gather.' + ['py', 'js', 'blocks.json'][i]), 'utf8') }));
  const replies = await page.evaluate(async ({ url, worker, fixtures }) => {
    const compiler = new Worker(`${url}assets/${worker}`, { type: 'module' });
    const request = message => new Promise((resolve, reject) => {
      const timer = setTimeout(() => { compiler.terminate(); reject(new Error('compiler worker timed out')); }, 90000);
      const listener = ({ data }) => {
        if (data.type === 'fatal' || data.type === 'failed') { clearTimeout(timer); compiler.removeEventListener('message', listener); reject(new Error(data.message)); }
        if ((message.type === 'initialize' && data.type === 'ready') || (data.type === 'compiled' && data.id === message.id)) {
          clearTimeout(timer); compiler.removeEventListener('message', listener);
          resolve({ ...data, bytecode: Array.from(data.bytecode || []) });
        }
      };
      compiler.addEventListener('message', listener); compiler.postMessage(message);
    });
    try {
      await request({ type: 'initialize', baseUrl: url });
      const results = [];
      for (const [i, fixture] of fixtures.entries()) {
        results.push(await request({ type: 'compile', id: i, ...fixture }));
        results.push(await request({ type: 'compile', id: i+10, ...fixture, target: { version: '0.0.9' } }));
      }
      return results;
    } finally { compiler.terminate(); }
  }, { url, worker, fixtures });
  const report = [];
  for (let i = 0; i < fixtures.length; i++) {
    const [result, rejected] = replies.slice(i*2, i*2+2);
    assert.deepEqual(result.diagnostics, []);
    assert.equal(result.manifest.requires, '0.1.0');
    assert(rejected.diagnostics.length > 0 && rejected.bytecode.length === 0);
    const path = join(directory, 'fixture.lbc'); writeFileSync(path, Buffer.from(result.bytecode));
    const native = JSON.parse(execFileSync(process.env.LIGHTVM_NUMERIC_RUNTIME, ['--numeric-package', path, '200', '1000', '2'], { encoding: 'utf8' }));
    assert.equal(native.status, 1); assert.deepEqual(native.state, [196608]);
    assert(native.plan_bytes > 0); assert.deepEqual(native.arrays, [Array(200).fill(98304)]);
    const expected = Array.from({ length: 200 }, (_, j) => Buffer.from([j < 16 ? 4 : 1, 2, 7, 255]).toString('hex')).join('');
    assert.equal(native.pixels, expected);
    report.push({ language: fixtures[i].language, package_bytes: result.bytecode.length,
      package_sha256: createHash('sha256').update(Buffer.from(result.bytecode)).digest('hex'),
      bulk: true, target_rejected: true, passed: true });
  }
  console.log(JSON.stringify({ browser: await browser.version(), production_worker: worker,
    worker_sha256: createHash('sha256').update(readFileSync(join(assets, 'assets', worker))).digest('hex'),
    cases: report, passed: true }, null, 2));
} finally {
  await browser?.close(); server.close(); rmSync(directory, { recursive: true, force: true });
}
