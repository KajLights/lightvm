import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import { COMMAND, LightVMClient } from '../protocol.mjs';
import { AudioEyeSession, audioEyeTarget } from './audio-eye-session';
import type { ProgramState } from './types';

// Optional integration against the companion live-input core and its compiled Eye.
// LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-eye-native
it.skipIf(!process.env.LIGHTVM_AUDIO_RECEIVER || !process.env.LIGHTVM_AUDIO_EYE)('drives and releases a real native Eye without replacing its generation', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'lightvm-audio-'));
  const receiver = spawn(process.env.LIGHTVM_AUDIO_RECEIVER!, ['--storage', join(directory, 'store.bin')], { stdio: ['pipe', 'pipe', 'pipe'] });
  const transport = {
    onData: (_bytes: Uint8Array) => {}, onDisconnect: (_error?: Error) => {},
    write: (bytes: Uint8Array) => new Promise<void>((resolve, reject) => receiver.stdin.write(bytes, error => error ? reject(error) : resolve())),
    close: async () => { receiver.stdin.end(); },
  };
  const client = new LightVMClient(transport), failures: Error[] = [];
  receiver.stdout.on('data', bytes => transport.onData(bytes));
  receiver.stdin.on('error', error => transport.onDisconnect(error)); receiver.on('error', error => transport.onDisconnect(error));
  let session: AudioEyeSession | undefined;
  try {
    const description = await client.describe(); expect(description.live_inputs).toBe(true);
    await client.configure({ schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 80 } }] });
    await client.upload(readFileSync(process.env.LIGHTVM_AUDIO_EYE!), description.module_limit, () => {}, 'lights');
    await client.command(COMMAND.PLAY);
    const before = await client.json('program_state', { output: 'lights' }) as ProgramState;
    const target = audioEyeTarget(before); expect(target).toBeDefined();
    session = new AudioEyeSession(client, target!, description.boot_id, () => {}, error => failures.push(error));
    await session.start();
    session.publish({ position: 0.25, width: 0.3, hue: 0.6, brightness: 0.7 });
    await expect.poll(async () => {
      const state = await client.json('program_state', { output: 'lights' });
      return state.inputs.brightness.value;
    }, { timeout: 2000 }).toBeCloseTo(0.7);
    const active = await client.json('program_state', { output: 'lights' });
    expect(active.generation).toBe(before.generation); expect(active.inputs.width.value).toBeCloseTo(0.3);
    const snapshot = await client.manage('program_snapshot', { output: 'lights' });
    expect(snapshot.pixels.some((value: number) => value > 0)).toBe(true);
    await session.stop();
    const restored = await client.json('program_state', { output: 'lights' });
    expect(restored.generation).toBe(before.generation);
    for (const name of ['position', 'width', 'hue', 'brightness']) expect(restored.inputs[name].binding).toEqual(target!.bindings[name]);
    expect(failures).toEqual([]);
  } finally {
    await session?.stop().catch(() => {}); await client.close(); receiver.kill(); rmSync(directory, { recursive: true, force: true });
  }
});
