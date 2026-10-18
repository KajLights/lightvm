import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { LiveInputSession } from './live-input-session';
import type { ProgramState } from './types';

afterEach(() => vi.useRealTimers());
function setup(send = vi.fn(async () => ({ inputs_revision: 1 }))) {
  vi.useFakeTimers();
  const changed = vi.fn(), failed = vi.fn();
  const session = new LiveInputSession({ json: send } as unknown as LightVMClient,
    { output: 'lights', slot: 'B', layer: 'eye', generation: 7 }, 0, changed, failed);
  return { session, send, changed, failed };
}

it('coalesces the newest per-input values and carries routing and revision', async () => {
  const { session, send } = setup();
  session.publish({ hue: { value: 0.1 } });session.publish({ hue: { value: 0.5 }, width: { value: 0.2 } });
  await vi.advanceTimersByTimeAsync(0);
  expect(send).toHaveBeenCalledWith('program_inputs', { output: 'lights', slot: 'B', layer: 'eye', expected_generation: 7,
    expected_inputs_revision: 0, inputs: { hue: { value: 0.5 }, width: { value: 0.2 } } });
  session.publish({ brightness: { value: 0.6 } });await vi.advanceTimersByTimeAsync(33);
  expect(send).toHaveBeenLastCalledWith('program_inputs',expect.objectContaining({ expected_inputs_revision: 1 }));
});

it('keeps only one command in flight and preserves a later driver takeover', async () => {
  let finish!: (value: { inputs_revision: number }) => void;
  const send = vi.fn(() => new Promise<{ inputs_revision: number }>(resolve => { finish = resolve; }));
  const { session } = setup(send);
  session.publish({ position: { value: 0.2 } });await vi.advanceTimersByTimeAsync(0);
  for (let value = 0; value < 100; value++) session.publish({ position: { value: value / 100 } });
  session.publish({ position: { driver: { kind: 'ramp', duration: 3, mode: 'pingpong' } } });
  await vi.advanceTimersByTimeAsync(100);expect(send).toHaveBeenCalledTimes(1);
  finish({ inputs_revision: 1 });await vi.advanceTimersByTimeAsync(0);
  expect(send).toHaveBeenCalledTimes(2);
  expect(send).toHaveBeenLastCalledWith('program_inputs',expect.objectContaining({ inputs: { position: { driver: { kind: 'ramp', duration: 3, mode: 'pingpong' } } } }));
  session.stop();finish({ inputs_revision: 2 });
});

it('drops queued work and late replies when the target is left', async () => {
  let finish!: (value: { inputs_revision: number }) => void;
  const send = vi.fn(() => new Promise<{ inputs_revision: number }>(resolve => { finish = resolve; }));
  const { session, changed } = setup(send);
  session.publish({ hue: { value: 0.2 } });await vi.advanceTimersByTimeAsync(0);
  session.publish({ hue: { value: 0.8 } });session.stop();finish({ inputs_revision: 1 });
  await vi.advanceTimersByTimeAsync(100);
  expect(send).toHaveBeenCalledTimes(1);expect(changed).not.toHaveBeenCalled();
});

it('stops after a conflict and never regresses its revision from polling', async () => {
  const problem = new Error('stale input revision');
  const { session, send, failed } = setup(vi.fn(async () => { throw problem; }));
  session.observe({ inputs_revision: 4 } as ProgramState);session.observe({ inputs_revision: 2 } as ProgramState);
  session.publish({ hue: { value: 0.2 } });await vi.advanceTimersByTimeAsync(0);
  expect(send).toHaveBeenLastCalledWith('program_inputs',expect.objectContaining({ expected_inputs_revision: 4 }));expect(failed).toHaveBeenCalledWith(problem);
  session.publish({ hue: { value: 0.8 } });await vi.advanceTimersByTimeAsync(100);expect(send).toHaveBeenCalledTimes(1);
});
