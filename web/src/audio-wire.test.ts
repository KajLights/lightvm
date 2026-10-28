import { expect, it, vi } from 'vitest';
import { encodeAudioRecord, audioHex, type AudioRecord } from './audio-wire';
import { LightVMClient, COMMAND } from '../protocol.mjs';

function record(): AudioRecord {
  return { source: 0x9000000000000007n, session: 0xf000000000000008n, sequence: 0x1000000000000001n,
    audioUs: 4000000n, synthetic: false, values: [0.25, 0.5, 0.75, 0, 1, 0.125],
    bands: Array(64).fill(0.5), edges: Array.from({ length: 65 }, (_, i) => 2 + Math.floor(i * 510 / 64)),
    events: Array.from({ length: 4 }, (_, i) => ({ id: 0xf100000000000001n + BigInt(i), timeUs: 3900000n + BigInt(i), strength: 0.5 })) };
}
it('encodes the 64-band/four-event boundary and preserves uint64 fields exactly', () => {
  const f = record(), bytes = encodeAudioRecord(f, 4001000n), view = new DataView(bytes.buffer);
  expect(bytes.length).toBe(700); expect(Array.from(bytes.subarray(0, 8))).toEqual([76, 86, 65, 70, 5, 6, 4, 0]);
  expect(view.getBigUint64(8)).toBe(f.source); expect(view.getBigUint64(16)).toBe(f.session);
  expect(view.getBigUint64(24)).toBe(f.sequence); expect(view.getBigUint64(48)).toBe(1n);
  expect(view.getFloat32(64)).toBe(0.25); expect(view.getUint16(92)).toBe(2); expect(view.getUint16(598)).toBe(512);
  expect(view.getBigUint64(676)).toBe(f.events[3].id); expect(view.getFloat32(696)).toBe(0.5);
  expect(audioHex(f.source)).toBe('9000000000000007');
});
it('rejects stale events, invalid layouts, nonfinite metrics and imprecise identities', () => {
  const f = record();
  expect(() => encodeAudioRecord(f, 4300001n)).toThrow('occurrence');
  expect(() => encodeAudioRecord({ ...f, values: [NaN, 0, 0, 0, 0, 0] }, 4000000n)).toThrow();
  expect(() => encodeAudioRecord({ ...f, edges: [0, 512] }, 4000000n)).toThrow();
  expect(() => encodeAudioRecord({ ...f, source: 0n }, 4000000n)).toThrow();
  expect(() => audioHex(1n << 64n)).toThrow();
});
it('fragments into ordered 512-byte commands with an exact length and frame identity', async () => {
  const client = new LightVMClient({}), command = vi.spyOn(client, 'command').mockResolvedValue({} as never);
  const recordBytes = encodeAudioRecord(record(), 4001000n);
  await client.publishAudio(0xf0000001, 0xf100000000000001n, recordBytes);
  expect(command).toHaveBeenCalledTimes(2);
  const first = command.mock.calls[0], second = command.mock.calls[1];
  expect(first[0]).toBe(COMMAND.AUDIO_FRAME); expect(first[1]!.length).toBe(512); expect(second[1]!.length).toBe(220);
  const a = new DataView(first[1]!.buffer), b = new DataView(second[1]!.buffer);
  expect(a.getUint32(0, true)).toBe(0xf0000001); expect(a.getBigUint64(4, true)).toBe(0xf100000000000001n);
  expect(a.getUint16(12, true)).toBe(700); expect(a.getUint16(14, true)).toBe(0); expect(b.getUint16(14, true)).toBe(496);
  expect([...first[1]!.subarray(16), ...second[1]!.subarray(16)]).toEqual([...recordBytes]);
});
