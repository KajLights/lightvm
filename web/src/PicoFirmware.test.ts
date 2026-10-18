import { describe, expect, it } from 'vitest';
import { decodePicoUf2 } from './PicoFirmware';
function uf2(count: number) {
  const bytes = new Uint8Array(count * 512), view = new DataView(bytes.buffer);
  for (let n = 0; n < count; n++) {
    [0x0a324655, 0x9e5d5157, 0x2000, 0x10000000 + n * 256, 256, n, count, 0xe48bff56]
      .forEach((value, index) => view.setUint32(n * 512 + index * 4, value, true));
    view.setUint32(n * 512 + 508, 0x0ab16f30, true);
    bytes.fill(n + 1, n * 512 + 32, n * 512 + 288);
  }
  return bytes;
}
describe('Pico installer storage geometry', () => {
  it('decodes firmware inside the boundary supplied by its build', () => {
    const image = decodePicoUf2(uf2(2), 0x10100000);
    expect(image).toHaveLength(512);
    expect(image[0]).toBe(1); expect(image[256]).toBe(2);
  });
  it('rejects a UF2 that would reach the configured storage region', () => {
    expect(() => decodePicoUf2(uf2(17), 0x10001000)).toThrow(/block 16/);
  });
  it('requires an explicit valid storage boundary', () => {
    for (const boundary of [NaN, 0, 0x10100001, 0x12000000])
      expect(() => decodePicoUf2(uf2(1), boundary)).toThrow(/storage boundary/);
  });
});
