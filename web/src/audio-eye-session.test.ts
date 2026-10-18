import { afterEach, describe, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import type { ProgramState } from './types';
import { AudioEyeSession, audioEyeTarget, EYE_SOURCE_PREFIX, type EyeTarget } from './audio-eye-session';
import { EYE_INPUTS } from './audio-features';

afterEach(() => vi.useRealTimers());
const target: EyeTarget = { output: 'lights', slot: 'B', layer: 'eye', generation: 8, revision: 2,
  bindings: Object.fromEntries(EYE_INPUTS.map(name => [name, { default: true }])) };
const frame = { position: 0.2, width: 0.3, hue: 0.4, brightness: 0.5 };
function setup() {
  const changed = vi.fn(), failed = vi.fn();
  const client = { closed: false, json: vi.fn(async (op: string) => op === 'input_claim' ? { boot_id: 'boot', generation: 9 } : { inputs_revision: 3 }), manage: vi.fn(async () => {}) };
  const session = new AudioEyeSession(client as unknown as LightVMClient, target, 'boot', changed, failed);
  return { session, client, changed, failed };
}
describe('audio source publications', () => {
  it('discovers a compatible live Eye and rejects incomplete contracts', () => {
    const state = { generation: 8, slot: 'B', faulted: false, inputs_revision: 2,
      manifest: { profile: 'animation', inputs: EYE_INPUTS.map(name => ({ name, type: 'float', minimum: 0, maximum: 1 })) },
      inputs: Object.fromEntries(EYE_INPUTS.map(name => [name, { binding: { default: true } }])) } as unknown as ProgramState;
    expect(audioEyeTarget(state)?.generation).toBe(8);
    expect(audioEyeTarget({ ...state, manifest: { ...state.manifest, profile: 'transition' } })).toBeUndefined();
    expect(audioEyeTarget({ ...state, faulted: true })).toBeUndefined();
    expect(audioEyeTarget({ ...state, manifest: { ...state.manifest, inputs: [] } } as ProgramState)).toBeUndefined();
  });
  it('claims named sources and binds the precise instance with generation and revision checks', async () => {
    const { session, client } = setup(); await session.start();
    expect(client.json).toHaveBeenNthCalledWith(1, 'input_claim', { signals: EYE_INPUTS.map(name => ({ name: EYE_SOURCE_PREFIX + name, type: 'float' })) });
    expect(client.json).toHaveBeenNthCalledWith(2, 'program_inputs', expect.objectContaining({ output: 'lights', slot: 'B', layer: 'eye', expected_generation: 8, expected_inputs_revision: 2 }));
    await session.stop();
    expect(client.json).toHaveBeenLastCalledWith('program_inputs', expect.objectContaining({ inputs: target.bindings, expected_generation: 8, expected_inputs_revision: 3 }));
  });
  it('publishes coherent batches and coalesces slow requests to the newest frame', async () => {
    vi.useFakeTimers(); const { session, client } = setup(); await session.start();
    let finish!: () => void; client.manage.mockImplementationOnce(() => new Promise<void>(resolve => { finish = resolve; }));
    session.publish(frame); await vi.advanceTimersByTimeAsync(0);
    for (let i = 0; i < 100; i++) session.publish({ ...frame, position: i / 100 });
    await vi.advanceTimersByTimeAsync(200); expect(client.manage).toHaveBeenCalledTimes(1);
    finish(); await vi.advanceTimersByTimeAsync(0);
    expect(client.manage).toHaveBeenCalledTimes(2);
    expect(client.manage).toHaveBeenLastCalledWith('input_publish', { boot_id: 'boot', generation: 9, sequence: 2,
      values: { 'audio.eye.position': 0.99, 'audio.eye.width': 0.3, 'audio.eye.hue': 0.4, 'audio.eye.brightness': 0.5 } });
    await session.stop();
  });
  it('discards unsent values on stop and refuses nonfinite values', async () => {
    vi.useFakeTimers(); const { session, client } = setup(); await session.start();
    session.publish({ ...frame, width: NaN }); await vi.advanceTimersByTimeAsync(100); expect(client.manage).not.toHaveBeenCalled();
    session.publish(frame); await session.stop(); await vi.advanceTimersByTimeAsync(100);
    expect(client.manage).not.toHaveBeenCalled();
  });
  it('does not bind after cancellation during source claiming', async () => {
    const { session, client } = setup(); let grant!: (value: any) => void;
    client.json.mockImplementationOnce(() => new Promise(resolve => { grant = resolve; }));
    const starting = session.start(), stopping = session.stop(); grant({ boot_id: 'boot', generation: 9 }); await starting; await stopping;
    expect(client.json).toHaveBeenCalledTimes(1);
  });
  it('releases ownership when another control rewires the Eye without restoring over it', async () => {
    const { session, client, failed } = setup(); await session.start();
    const state = { generation: 8, slot: 'B', faulted: false, inputs_revision: 4,
      manifest: { profile: 'animation', inputs: EYE_INPUTS.map(name => ({ name, type: 'float' })) },
      inputs: Object.fromEntries(EYE_INPUTS.map(name => [name, { binding: { value: 0.5 } }])) } as unknown as ProgramState;
    session.observe(state); await session.stop();
    expect(failed).toHaveBeenCalledWith(expect.objectContaining({ message: expect.stringContaining('changed elsewhere') }));
    expect(client.json).toHaveBeenCalledTimes(2);
  });
  it('rejects a previous boot and leaves newer wiring alone when restoration conflicts', async () => {
    const first = setup(); first.client.json.mockResolvedValueOnce({ boot_id: 'new-boot', generation: 1 });
    await expect(first.session.start()).rejects.toThrow('rebooted'); expect(first.client.json).toHaveBeenCalledTimes(1);
    const second = setup(); await second.session.start();
    second.client.json.mockRejectedValueOnce(new Error('stale input revision'));
    await expect(second.session.stop()).rejects.toThrow('stale input revision');
    expect(second.client.json).toHaveBeenCalledTimes(3);
  });
});
