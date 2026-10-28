import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { AudioSourceSession } from './audio-source-session';
import { analysisFrame } from './audio-test-data';

afterEach(() => vi.useRealTimers());
function setup() {
  const failed = vi.fn();
  const client = { closed: false, json: vi.fn(async () => ({ boot_id: 'boot', generation: 9, challenge: '0000000000000001' })), manage: vi.fn(async () => {}), publishAudio: vi.fn(async (_generation: number, _id: bigint, _bytes: Uint8Array) => {}) };
  const session = new AudioSourceSession(client as unknown as LightVMClient, 'boot', failed, 'desk');
  return { session, client, failed };
}
it('claims a structured stream, echoes its clock challenge and explicitly releases ownership', async () => {
  const { session, client } = setup(); await session.start(analysisFrame()); await session.stop();
  expect(client.json).toHaveBeenCalledExactlyOnceWith('audio_claim', { name: 'desk', source_id: '0000000000000008', session: '0000000000000009', analysis_profile: 1, synthetic: false });
  expect(client.manage).toHaveBeenCalledWith('audio_clock', expect.objectContaining({ challenge: '0000000000000001', boot_id: 'boot', generation: 9 }));
  expect(client.manage).toHaveBeenLastCalledWith('audio_release', { boot_id: 'boot', generation: 9 });
});
it('coalesces slow delivery to one newest pending snapshot and keeps its capture time', async () => {
  vi.useFakeTimers(); const { session, client } = setup(); await session.start(analysisFrame());
  let finish!: () => void; client.publishAudio.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve; }));
  session.publish(analysisFrame(2n)); await vi.advanceTimersByTimeAsync(0);
  for (let i = 3; i <= 100; i++) session.publish(analysisFrame(BigInt(i), i / 100));
  await vi.advanceTimersByTimeAsync(200); expect(client.publishAudio).toHaveBeenCalledTimes(2);
  finish(); await vi.advanceTimersByTimeAsync(0); expect(client.publishAudio).toHaveBeenCalledTimes(3);
  const record = client.publishAudio.mock.calls[2][2], view = new DataView(record.buffer);
  expect(view.getBigUint64(24)).toBe(100n); expect(view.getFloat32(64)).toBe(1);
  expect(view.getBigUint64(32)).toBeLessThan(view.getBigUint64(40));
  await session.stop();
});
it('drops stale snapshots, renews clock ownership without data, and stops on revocation', async () => {
  vi.useFakeTimers(); const { session, client, failed } = setup(); await session.start(analysisFrame());
  const old = analysisFrame(2n); await vi.advanceTimersByTimeAsync(400); session.publish(old); await vi.advanceTimersByTimeAsync(0);
  expect(client.publishAudio).toHaveBeenCalledTimes(1);
  await vi.advanceTimersByTimeAsync(3200); expect(client.json).toHaveBeenCalledWith('audio_keep', { boot_id: 'boot', generation: 9 });
  client.publishAudio.mockRejectedValueOnce(new Error('expired or unowned audio generation'));
  session.publish(analysisFrame(3n)); await vi.advanceTimersByTimeAsync(0);
  expect(failed).toHaveBeenCalledTimes(1); const count = client.json.mock.calls.length;
  session.publish(analysisFrame(4n)); await vi.advanceTimersByTimeAsync(5000); expect(client.json).toHaveBeenCalledTimes(count);
  await session.stop();
});
it('changes session/profile by claiming a fresh generation and discarding old pending data', async () => {
  vi.useFakeTimers(); const { session, client } = setup(); await session.start(analysisFrame());
  client.json.mockResolvedValueOnce({ boot_id: 'boot', generation: 10, challenge: '0000000000000002' });
  const next = { ...analysisFrame(1n, 0.4, 2), session: 10n }; session.publish(next); await vi.advanceTimersByTimeAsync(0);
  expect(client.json).toHaveBeenLastCalledWith('audio_claim', expect.objectContaining({ session: '000000000000000a', analysis_profile: 2 }));
  expect(client.publishAudio.mock.calls.at(-1)![0]).toBe(10); await session.stop();
});
it('releases a claim granted after cancellation and rejects a controller reboot', async () => {
  const { session, client } = setup(); let grant!: (value: any) => void;
  client.json.mockImplementationOnce(() => new Promise(resolve => { grant = resolve; }));
  const starting = session.start(analysisFrame()), stopping = session.stop(); grant({ boot_id: 'boot', generation: 9, challenge: '0000000000000001' }); await starting; await stopping;
  expect(client.publishAudio).not.toHaveBeenCalled(); expect(client.manage).toHaveBeenCalledExactlyOnceWith('audio_release', { boot_id: 'boot', generation: 9 });
  const second = setup(); second.client.json.mockResolvedValueOnce({ boot_id: 'new-boot', generation: 1, challenge: '0000000000000001' });
  await expect(second.session.start(analysisFrame())).rejects.toThrow('rebooted');
});
