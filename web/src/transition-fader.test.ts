import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import type { TransitionState } from './types';
import { FADER_SOURCE, TransitionFaderInput } from './transition-fader';

afterEach(() => vi.useRealTimers());

function setup(send = vi.fn(async () => true)) {
  vi.useFakeTimers();
  const changed = vi.fn(), lost = vi.fn();
  const input = new TransitionFaderInput({ updateTransitionSource: send } as unknown as LightVMClient, 'lights', changed, lost);
  input.attach(7);
  return { input, send, changed, lost };
}

it('streams during a drag at a bounded rate and delivers the final value', async () => {
  const { input, send } = setup();
  input.publish(10); await vi.advanceTimersByTimeAsync(0);
  input.publish(20); input.publish(30); await vi.advanceTimersByTimeAsync(32);
  expect(send).toHaveBeenCalledTimes(1);
  await vi.advanceTimersByTimeAsync(1);
  expect(send.mock.calls).toEqual([[FADER_SOURCE, 7, 10, 'lights'], [FADER_SOURCE, 7, 30, 'lights']]);
  input.publish(100); await vi.advanceTimersByTimeAsync(33);
  expect(send).toHaveBeenLastCalledWith(FADER_SOURCE, 7, 100, 'lights');
  expect(input.pending).toBe(false);
});

it('retains only the latest value while the connection is slow', async () => {
  let complete!: (accepted: boolean) => void;
  const send = vi.fn().mockImplementationOnce(() => new Promise<boolean>(resolve => { complete = resolve; })).mockResolvedValue(true);
  const { input } = setup(send);
  input.publish(1); await vi.advanceTimersByTimeAsync(0);
  for (let value = 2; value <= 100; value++) input.publish(value);
  await vi.advanceTimersByTimeAsync(1000);
  expect(send).toHaveBeenCalledTimes(1);
  complete(true); await vi.advanceTimersByTimeAsync(0);
  expect(send.mock.calls).toEqual([[FADER_SOURCE, 7, 1, 'lights'], [FADER_SOURCE, 7, 100, 'lights']]);
});

it('ignores invalid values without replacing the pending valid position', async () => {
  const { input, send } = setup();
  input.publish(35);
  for (const value of [NaN, Infinity, -1, 101]) input.publish(value);
  await vi.advanceTimersByTimeAsync(0);
  expect(send).toHaveBeenCalledExactlyOnceWith(FADER_SOURCE, 7, 35, 'lights');
});

it('drops pending values when a timed or replacement driver takes control', async () => {
  const { input, send, lost } = setup();
  input.publish(50);
  input.observe({ driver: { kind: 'timed', assignment: 8 } } as TransitionState);
  await vi.advanceTimersByTimeAsync(100);
  expect(send).not.toHaveBeenCalled(); expect(lost).toHaveBeenCalledOnce();
  input.attach(9); input.publish(60);
  input.observe({ driver: { kind: 'source', source: FADER_SOURCE, assignment: 10 } } as TransitionState);
  await vi.advanceTimersByTimeAsync(100);
  expect(send).not.toHaveBeenCalled();
});

it('does not reclaim a rejected assignment or continue after a transport error', async () => {
  const send = vi.fn().mockResolvedValueOnce(false).mockRejectedValueOnce(new Error('Disconnected'));
  const { input, lost } = setup(send);
  input.publish(10); await vi.advanceTimersByTimeAsync(0);
  input.publish(20); await vi.advanceTimersByTimeAsync(100);
  expect(send).toHaveBeenCalledTimes(1); expect(lost).toHaveBeenCalledWith(undefined);
  input.attach(8); input.publish(30); await vi.advanceTimersByTimeAsync(0);
  expect(lost).toHaveBeenLastCalledWith(expect.objectContaining({ message: 'Disconnected' }));
  input.publish(40); await vi.advanceTimersByTimeAsync(100);
  expect(send).toHaveBeenCalledTimes(2);
});

it('stops on disposal and ignores an old in-flight response after reassignment', async () => {
  let complete!: (accepted: boolean) => void;
  const send = vi.fn().mockImplementationOnce(() => new Promise<boolean>(resolve => { complete = resolve; })).mockResolvedValue(true);
  const { input, changed, lost } = setup(send);
  input.publish(10); await vi.advanceTimersByTimeAsync(0);
  input.attach(8); complete(false); await vi.advanceTimersByTimeAsync(0);
  expect(lost).not.toHaveBeenCalled(); expect(changed).not.toHaveBeenCalled();
  input.publish(20); input.stop(); await vi.advanceTimersByTimeAsync(100);
  expect(send).toHaveBeenCalledTimes(1);
});
