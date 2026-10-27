import { spawn } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { expect, it } from 'vitest';
import { LightVMClient } from '../protocol.mjs';
import { encodeAudioRecord, audioNow, audioHex } from './audio-wire';

// LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-source-native
it.skipIf(!process.env.LIGHTVM_AUDIO_RECEIVER || !process.env.LIGHTVM_AUDIO_EYE)('drives a scoped native Eye with the browser structured-audio codec', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'lightvm-audio-'));
  const receiver = spawn(process.env.LIGHTVM_AUDIO_RECEIVER!, ['--storage', join(directory, 'store.bin')], { stdio: ['pipe', 'pipe', 'pipe'] });
  const transport = {
    onData: (_bytes: Uint8Array) => {}, onDisconnect: (_error?: Error) => {},
    write: (bytes: Uint8Array) => new Promise<void>((resolve, reject) => receiver.stdin.write(bytes, error => error ? reject(error) : resolve())),
    close: async () => { receiver.stdin.end(); },
  };
  const client = new LightVMClient(transport);
  receiver.stdout.on('data', bytes => transport.onData(bytes));
  receiver.stdin.on('error', error => transport.onDisconnect(error)); receiver.on('error', error => transport.onDisconnect(error));
  try {
    const description = await client.describe(); expect(description.live_inputs).toBe(true);
    await client.configure({ schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 80 } }] });
    await client.saveProgram('eye', readFileSync(process.env.LIGHTVM_AUDIO_EYE!));
    const sources = { music: { type: 'gateway', name: 'music', analysis_profile: 1, allow_synthetic: false } };
    await client.manage('program_activate', { name: 'eye', action: 'play', output: 'lights', sources,
      inputs: { width: { source: 'music.band63' }, intensity: { source: 'music.level' } } });
    const before = await client.json('program_state', { output: 'lights' });
    const claim = await client.json('audio_claim', { name: 'music', source_id: '9000000000000007', session: 'f000000000000008', analysis_profile: 1 });
    expect(claim.boot_id).toBe(description.boot_id);
    await client.manage('audio_clock', { boot_id: claim.boot_id, generation: claim.generation, challenge: claim.challenge, publisher_us: audioHex(audioNow()) });
    const capture = audioNow();
    const bytes = encodeAudioRecord({ source: 0x9000000000000007n, session: 0xf000000000000008n, sequence: 1n,
      audioUs: capture, synthetic: false, values: [0.7, 0.3, 0, 0, 0, 0], bands: Array(64).fill(0.3),
      edges: Array.from({ length: 65 }, (_, i) => 2 + Math.floor(i * 510 / 64)),
      events: Array.from({ length: 4 }, (_, i) => ({ id: 0xf100000000000001n + BigInt(i), timeUs: capture, strength: 0.5 })) }, audioNow());
    await client.publishAudio(claim.generation, 1n, bytes);
    await expect.poll(async () => (await client.json('program_state', { output: 'lights' })).inputs.intensity.value, { timeout: 1000 }).toBeCloseTo(0.7);
    const active = await client.json('program_state', { output: 'lights' });
    expect(active.generation).toBe(before.generation); expect(active.inputs.width.value).toBeCloseTo(0.3); expect(active.sources).toEqual(sources);
    expect((await client.manage('program_snapshot', { output: 'lights' })).pixels.some((value: number) => value > 0)).toBe(true);
    await client.manage('audio_release', { boot_id: claim.boot_id, generation: claim.generation });
    await expect.poll(async () => (await client.json('program_state', { output: 'lights' })).inputs.intensity.fallback).toBe(true);
    const restored = await client.json('program_state', { output: 'lights' });
    expect(restored.generation).toBe(before.generation); expect(restored.inputs.intensity.binding.source).toBe('music.level');
  } finally {
    await client.close(); receiver.kill(); rmSync(directory, { recursive: true, force: true });
  }
});
