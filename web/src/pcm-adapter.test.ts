import { expect, it } from 'vitest';
import { PcmAdapter, quantize } from '../audio/pcm-adapter.mjs';

it('uses the shared float-to-int16 rounding, clipping and channel contract', () => {
  expect([-2, -1, -0.5, -0.5 / 32768, 0.5 / 32768, 0.5, 1, NaN, Infinity].map(quantize)).toEqual([-32768, -32768, -16384, 0, 1, 16384, 32767, 0, 0]);
  const hops: Int16Array[] = [], p = new PcmAdapter(48000, -1, (pcm: Int16Array) => hops.push(pcm.slice()));
  p.push([new Float32Array(1024).fill(0.25), new Float32Array(1024).fill(0.75)], 1000000, 0);
  expect(hops).toHaveLength(1); expect(hops[0][0]).toBe(16384);
});
for (const rate of [44100, 48000, 96000]) it(`preserves calibration and sample timestamps at ${rate} Hz across irregular blocks`, () => {
  const values: number[] = [], times: number[] = [];
  const p = new PcmAdapter(rate, 0, (pcm: Int16Array, time: number) => { values.push(...pcm); times.push(time); });
  const total = rate / 2, sizes = [37, 128, 511, 1, 251];
  let offset = 0, turn = 0;
  while (offset < total) {
    const count = Math.min(sizes[turn++ % sizes.length], total - offset);
    const block = Float32Array.from({ length: count }, (_, i) => 0.25 * Math.sin(2 * Math.PI * 1000 * (offset + i) / rate));
    p.push([block], 1000000, 10000); offset += count;
  }
  const rms = Math.sqrt(values.slice(2048).reduce((sum, value) => sum + (value / 32768) ** 2, 0) / (values.length - 2048));
  expect(rms).toBeCloseTo(0.25 / Math.sqrt(2), 3);
  expect(times.length).toBe(23);
  expect(Math.abs(times[0] - (1000000 - 10000 + (1023 * rate / 48000 + 1) * 1e6 / rate))).toBeLessThan(1);
  for (let i = 1; i < times.length; i++) expect(Math.abs(times[i] - times[i - 1] - 1024e6 / 48000)).toBeLessThan(1);
});
it('attenuates frequencies that would alias when downsampling and clears history on gaps', () => {
  let squares = 0, count = 0;
  const p = new PcmAdapter(96000, 0, (pcm: Int16Array) => { for (const x of pcm) { squares += (x / 32768) ** 2; count++; } });
  p.push([Float32Array.from({ length: 96000 }, (_, i) => 0.5 * Math.sin(2 * Math.PI * 30000 * i / 96000))], 1000000, 0);
  expect(Math.sqrt(squares / count)).toBeLessThan(0.002);
  p.reset(100000); squares = count = 0;
  p.push([new Float32Array(4096)], 1000000, 0);
  expect(squares).toBe(0);
});
