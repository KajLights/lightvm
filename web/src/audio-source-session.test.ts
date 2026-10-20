import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { AudioSourceSession } from './audio-source-session';
import { AUDIO_SIGNALS, SILENCE } from './audio-features';

afterEach(() => vi.useRealTimers());
function setup() {
  const failed = vi.fn();
  const client = { closed: false, json: vi.fn(async () => ({ boot_id: 'boot', generation: 9 })), manage: vi.fn(async () => {}) };
  const session = new AudioSourceSession(client as unknown as LightVMClient, 'boot', failed);
  return { session, client, failed };
}
it('publishes reusable features without touching any animation bindings', async () => {
  const { session, client } = setup(); await session.start(); await session.stop();
  expect(client.json).toHaveBeenCalledExactlyOnceWith('input_claim', { signals: AUDIO_SIGNALS.map(name => ({ name: 'music.' + name, type: 'float' })) });
  expect(client.manage).not.toHaveBeenCalled();
});
it('coalesces slow transport to the newest coherent feature batch', async () => {
  vi.useFakeTimers(); const { session, client } = setup(); await session.start();
  let finish!: () => void; client.manage.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve; }));
  session.publish({ ...SILENCE, level: 0.5 }); await vi.advanceTimersByTimeAsync(0);
  for (let i = 0; i < 100; i++) session.publish({ ...SILENCE, bass: i / 100 });
  await vi.advanceTimersByTimeAsync(200); expect(client.manage).toHaveBeenCalledTimes(1);
  finish(); await vi.advanceTimersByTimeAsync(0);
  expect(client.manage).toHaveBeenCalledTimes(2);
  expect(client.manage).toHaveBeenLastCalledWith('input_publish', { boot_id: 'boot', generation: 9, sequence: 2,
    values: Object.fromEntries(AUDIO_SIGNALS.map(name => ['music.' + name, name === 'bass' ? 0.99 : 0])) });
  await session.stop();
});
it('discards invalid and cancelled frames', async () => {
  vi.useFakeTimers(); const { session, client } = setup(); await session.start();
  session.publish({ ...SILENCE, level: NaN }); await vi.advanceTimersByTimeAsync(100); expect(client.manage).not.toHaveBeenCalled();
  session.publish(SILENCE); await session.stop(); await vi.advanceTimersByTimeAsync(100); expect(client.manage).not.toHaveBeenCalled();
});
it('does not publish after cancellation during claiming or across a boot change', async () => {
  const { session, client } = setup(); let grant!: (value: any) => void;
  client.json.mockImplementationOnce(() => new Promise(resolve => { grant = resolve; }));
  const starting = session.start(), stopping = session.stop(); grant({ boot_id: 'boot', generation: 9 }); await starting; await stopping;
  expect(client.json).toHaveBeenCalledTimes(1); expect(client.manage).not.toHaveBeenCalled();
  const second = setup(); second.client.json.mockResolvedValueOnce({ boot_id: 'new-boot', generation: 1 });
  await expect(second.session.start()).rejects.toThrow('rebooted');
});
it('stops on revoked ownership without reclaiming or changing consumers', async () => {
  vi.useFakeTimers(); const { session, client, failed } = setup(); await session.start();
  client.manage.mockRejectedValueOnce(new Error('stale publisher generation'));
  session.publish(SILENCE); await vi.advanceTimersByTimeAsync(0);
  session.publish(SILENCE); await vi.advanceTimersByTimeAsync(100);
  expect(failed).toHaveBeenCalledTimes(1);expect(client.manage).toHaveBeenCalledTimes(1);expect(client.json).toHaveBeenCalledTimes(1);
});
