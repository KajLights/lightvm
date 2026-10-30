import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import test from 'node:test';
import { assessCandidate, decodeUf2, verifiedPayload } from '../shared/device-core/web/firmware.mjs';
const fixture = JSON.parse(readFileSync(new URL('../shared/device-core/contracts/candidate-cases.json', import.meta.url)));
function merge(base, patch) { for (const [key, value] of Object.entries(patch)) base[key] = value && typeof value === 'object' && !Array.isArray(value) ? merge(base[key] ?? {}, value) : value; return base; }
for (const entry of fixture.cases) test('candidate: ' + entry.name, () => {
  const device = merge(structuredClone(fixture.device), entry.device ?? {});
  assert.deepEqual(assessCandidate(device, fixture.artifact, entry.required_features ?? fixture.required_features,
    entry.method ?? fixture.method, entry.supported_tools ?? fixture.supported_tools), entry.expected);
});
function uf2(family) {
  const bytes = new Uint8Array(512), view = new DataView(bytes.buffer);
  [0x0a324655, 0x9e5d5157, 0x2000, 0x10000000, 256, 0, 1, family].forEach((word, i) => view.setUint32(i * 4, word, true));
  view.setUint32(508, 0x0ab16f30, true); return bytes;
}
test('UF2 validates family, exact order and reserved boundaries before decoding', () => {
  for (const family of [0xe48bff56, 0xe48bff59]) {
    const bytes = uf2(family); assert.equal(decodeUf2(bytes, 0x10100000, family).length, 256);
    assert.throws(() => decodeUf2(bytes, 0x10100000, family ^ 1));
    new DataView(bytes.buffer).setUint32(12, 0x10100000, true);
    assert.throws(() => decodeUf2(bytes, 0x10100000, family));
  }
});
test('downloads enforce byte count and SHA-256 before handing bytes to an installer', async () => {
  const data = new Uint8Array([1, 2, 3]), hash = Buffer.from(await crypto.subtle.digest('SHA-256', data)).toString('hex');
  const asset = { bytes: 3, sha256: hash };
  assert.deepEqual(await verifiedPayload('https://example.test/a', asset, async () => new Response(data)), data);
  await assert.rejects(verifiedPayload('https://example.test/a', asset, async () => new Response(new Uint8Array([1, 2]))));
  await assert.rejects(verifiedPayload('https://example.test/a', asset, async () => new Response(new Uint8Array([1, 2, 4]))));
  await assert.rejects(verifiedPayload('https://example.test/a', asset, async () => new Response(new Uint8Array(4))));
});
