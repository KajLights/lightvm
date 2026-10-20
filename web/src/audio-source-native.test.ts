import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import { COMMAND, LightVMClient } from '../protocol.mjs';
import { AudioSourceSession } from './audio-source-session';
import { SILENCE } from './audio-features';
import type { ProgramState } from './types';

// Optional integration against the companion live-input core and its compiled Eye.
// LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-source-native
it.skipIf(!process.env.LIGHTVM_AUDIO_RECEIVER || !process.env.LIGHTVM_AUDIO_EYE)('drives a native Eye through generic music bindings without replacing its generation', async () => {
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
  let session: AudioSourceSession | undefined;
  try {
    const description = await client.describe(); expect(description.live_inputs).toBe(true);
    await client.configure({ schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 80 } }] });
    await client.upload(readFileSync(process.env.LIGHTVM_AUDIO_EYE!), description.module_limit, () => {}, 'lights');
    await client.command(COMMAND.PLAY);
    const before = await client.json('program_state', { output: 'lights' }) as ProgramState;
    await client.json('program_inputs', { output: 'lights', expected_generation: before.generation, expected_inputs_revision: before.inputs_revision,
      inputs: { width: { source: 'music.bass' }, intensity: { source: 'music.level' } } });
    session = new AudioSourceSession(client, description.boot_id, error => failures.push(error));
    await session.start();
    session.publish({ ...SILENCE, bass: 0.3, level: 0.7 });
    await expect.poll(async () => {
      const state = await client.json('program_state', { output: 'lights' });
      return state.inputs.intensity.value;
    }, { timeout: 2000 }).toBeCloseTo(0.7);
    const active = await client.json('program_state', { output: 'lights' });
    expect(active.generation).toBe(before.generation); expect(active.inputs.width.value).toBeCloseTo(0.3);
    const snapshot = await client.manage('program_snapshot', { output: 'lights' });
    expect(snapshot.pixels.some((value: number) => value > 0)).toBe(true);
    await session.stop();
    const restored = await client.json('program_state', { output: 'lights' });
    expect(restored.generation).toBe(before.generation);
    expect(restored.inputs.width.binding.source).toBe('music.bass');
    expect(restored.inputs.intensity.binding.source).toBe('music.level');
    expect(failures).toEqual([]);
  } finally {
    await session?.stop().catch(() => {}); await client.close(); receiver.kill(); rmSync(directory, { recursive: true, force: true });
  }
});
