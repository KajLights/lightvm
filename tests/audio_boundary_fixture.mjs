// Run committed decision-boundary cases with the same browser WASM module.
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
const [moduleFile, pcmFile, casesFile, resultFile] = process.argv.slice(2);
const { default: create } = await import(pathToFileURL(moduleFile));
const api = await create(), heap = api.HEAP16.buffer;
const bytes = fs.readFileSync(pcmFile), view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
const cases = JSON.parse(fs.readFileSync(casesFile)), results = [];
for (const test of cases) {
  const handle = api._kf_host_new(test.profile, 0, -50, 30, 220, 16, test.threshold, 200);
  if (!handle) throw Error('analysis allocation failed');
  const pcm = api._kf_host_pcm(handle) / 2, output = api._kf_host_output(handle) / 4;
  const rows = Buffer.alloc((test.target_hop + 1) * 74 * 4);
  for (let hop = 0; hop <= test.target_hop; hop++) {
    for (let i = 0; i < 1024; i++) api.HEAP16[pcm + i] = view.getInt16(hop * 2048 + i * 2, true);
    rows.writeFloatLE(api._kf_host_push(handle, Math.floor((hop + 1) * 1024 * 1e6 / 48000)), hop * 74 * 4);
    for (let i = 0; i < 73; i++) rows.writeFloatLE(api.HEAPF32[output + i], (hop * 74 + i + 1) * 4);
  }
  results.push(rows);
  api._kf_host_destroy(handle);
}
if (api.HEAP16.buffer !== heap) throw Error('WASM heap grew');
fs.writeFileSync(resultFile, Buffer.concat(results));
