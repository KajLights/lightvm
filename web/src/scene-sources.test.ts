import { expect, it } from 'vitest';
import { assembleSources, normalizeAddress, providerMatches, scopedInputSources, sourceEntries, validateSources } from './scene-sources';
import type { AudioProviderStatus } from './types';

it('preserves both transports and explicit policies when editing a recipe', () => {
  const sources = { room: { type: 'lvaf' as const, address: 'Sense.EXAMPLE.', port: 51000, source_id: '9000000000000007', analysis_profile: 2 as const, allow_synthetic: true },
    desk: { type: 'gateway' as const, name: 'comparison_1' } };
  expect(assembleSources(sourceEntries(sources))).toEqual({ sources });
  expect(normalizeAddress('Sense.EXAMPLE.')).toBe('sense.example');
  expect(normalizeAddress('010.077.010.245')).toBe('10.77.10.245');
});
it('rejects unsupported addresses, aliases, profiles, fields and identity pins', () => {
  for (const address of ['', 'sense.local', 'SENSE.LOCAL.', '0.0.0.0', '224.1.1.1', '255.255.255.255', '192.168.1.256', 'https://sense', '-bad.example'])
    expect(() => validateSources({ room: { type: 'lvaf', address } }), address).toThrow();
  for (const alias of ['board', 'light', 'audio.level', 'x'.repeat(24)]) expect(() => validateSources({ [alias]: { type: 'gateway', name: 'desk' } })).toThrow();
  for (const extra of [{ port: 1023 }, { port: 65536 }, { port: 49152.5 }, { source_id: '0000000000000000' }, { source_id: '123' }, { analysis_profile: 3 }, { allow_synthetic: 1 }, { group: '239.1.1.1' }])
    expect(() => validateSources({ room: { type: 'lvaf', address: 'sense.example', ...extra } })).toThrow();
  expect(() => validateSources({ room: { type: 'gateway', name: 'desk', address: 'sense.example' } })).toThrow();
  const rows = sourceEntries({ room: { type: 'gateway', name: 'desk' } });
  expect(assembleSources([...rows, ...rows]).error).toMatch('unique');
  expect(() => validateSources(Object.fromEntries(['a', 'b', 'c', 'd', 'e'].map(alias => [alias, { type: 'gateway', name: alias }])))).toThrow('four');
});
it('matches live status by the entire canonical policy, keeping identity adoption explicit', () => {
  const source = { type: 'lvaf' as const, address: 'Sense.EXAMPLE.' };
  const provider = { type: 'lvaf', address: 'sense.example', port: 49152, expected_profile: 1, expected_source_id: '0000000000000000', allow_synthetic: false } as AudioProviderStatus;
  expect(providerMatches(source, provider)).toBe(true);
  for (const other of [{ port: 50000 }, { expected_profile: 2 }, { expected_source_id: '0000000000000007' }, { allow_synthetic: true }]) expect(providerMatches(source, { ...provider, ...other })).toBe(false);
  expect(providerMatches({ ...source, source_id: '0000000000000007' }, provider, false)).toBe(true);
});
it('suggests typed scoped counters/bands while shadowing a global alias', () => {
  const generic = [{ name: 'room.level', origin: 'publisher' as const, type: 'bool' as const, valid: true, value: true, age_ms: 0 },
    { name: 'control.level', type: 'float' as const, valid: true, value: 0.5, age_ms: 0 }];
  const sources = scopedInputSources(generic, { room: { type: 'lvaf', address: 'sense.example' }, desk: { type: 'gateway', name: 'music' } });
  expect(sources.filter(s => s.name === 'room.level')).toEqual([expect.objectContaining({ type: 'float', origin: 'scene', valid: false })]);
  expect(sources.find(s => s.name === 'room.onsets')?.type).toBe('int');
  expect(sources.find(s => s.name === 'desk.band63')?.type).toBe('float');
  expect(sources.find(s => s.name === 'control.level')).toEqual(generic[1]);
});
