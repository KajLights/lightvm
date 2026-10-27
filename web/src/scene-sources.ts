import type { AudioProviderStatus, InputSource, SceneSource, SceneSources } from './types';

export const ANALYSIS_PROFILES = [{ id: 1, label: 'Adaptive percussion v1' }, { id: 2, label: 'Level rise baseline v1' }] as const;
export interface SourceEntry { key: number; alias: string; source: SceneSource }
let nextKey = 0;
export const sourceEntry = (alias: string, source: SceneSource): SourceEntry => ({ key: ++nextKey, alias, source: structuredClone(source) });
export const sourceEntries = (sources: SceneSources = {}) => Object.entries(sources).map(([alias, source]) => sourceEntry(alias, source));
const object = (value: unknown): value is Record<string, unknown> => Boolean(value && typeof value === 'object' && !Array.isArray(value));
const identity = (value: unknown) => typeof value === 'string' && /^[a-f\d]{16}$/i.test(value) && !/^0{16}$/.test(value);

export function normalizeAddress(value: string): string {
  if (typeof value !== 'string' || value.length > 127) throw Error('Use an IPv4 address or DNS name of at most 127 characters.');
  const text = value.toLowerCase().replace(/\.$/, '');
  if (text.endsWith('.local')) throw Error('.local lookup is not supported. Use a router DNS name or an IPv4 address.');
  if (/^[\d.]*$/.test(text)) {
    const octets = text.split('.');
    if (octets.length !== 4 || octets.some(n => !/^\d{1,3}$/.test(n) || Number(n) > 255)) throw Error('Enter a valid IPv4 address.');
    if (!Number(octets[0]) || Number(octets[0]) >= 224) throw Error('Use a unicast IPv4 address.');
    return octets.map(Number).join('.');
  }
  if (!text.split('.').every(label => /^[a-z\d](?:[a-z\d-]{0,61}[a-z\d])?$/.test(label))) throw Error('Enter an ordinary DNS hostname.');
  return text;
}
export function validateSources(sources: unknown): asserts sources is SceneSources {
  if (!object(sources) || Object.keys(sources).length > 4) throw Error('A scene can declare at most four audio sources.');
  for (const [alias, source] of Object.entries(sources)) {
    if (!/^[A-Za-z\d_-]{1,23}$/.test(alias) || alias === 'board' || alias === 'light') throw Error('Source aliases need 1–23 letters, digits, underscores or hyphens; board and light are reserved.');
    if (!object(source)) throw Error('Invalid source declaration for ' + alias + '.');
    const allowed = source.type === 'lvaf' ? ['type', 'address', 'port', 'source_id', 'analysis_profile', 'allow_synthetic'] : ['type', 'name', 'source_id', 'analysis_profile', 'allow_synthetic'];
    if (Object.keys(source).some(key => !allowed.includes(key))) throw Error('Unknown source field in ' + alias + '.');
    if (source.type === 'lvaf') {
      normalizeAddress(source.address as string);
      if (source.port !== undefined && (!Number.isInteger(source.port) || Number(source.port) < 1024 || Number(source.port) > 65535)) throw Error('Source port must be an integer from 1024 to 65535.');
    } else if (source.type !== 'gateway' || typeof source.name !== 'string' || !/^[A-Za-z\d_.-]{1,32}$/.test(source.name)) throw Error('Gateway names need 1–32 letters, digits, dots, underscores or hyphens.');
    if (source.source_id !== undefined && !identity(source.source_id)) throw Error('An identity pin needs 16 hexadecimal digits and cannot be zero.');
    if (source.analysis_profile !== undefined && source.analysis_profile !== 1 && source.analysis_profile !== 2) throw Error('Unsupported audio analysis profile.');
    if (source.allow_synthetic !== undefined && typeof source.allow_synthetic !== 'boolean') throw Error('Synthetic input policy must be true or false.');
  }
}
export function assembleSources(entries: SourceEntry[]): { sources: SceneSources; error?: string } {
  try {
    if (new Set(entries.map(row => row.alias)).size !== entries.length) throw Error('Each source alias must be unique.');
    const sources = Object.fromEntries(entries.map(row => [row.alias, row.source])); validateSources(sources);
    return { sources };
  } catch (error) { return { sources: {}, error: (error as Error).message }; }
}
export function providerMatches(source: SceneSource, provider: AudioProviderStatus, policy = true) {
  let address: string; try { address = source.type === 'lvaf' ? normalizeAddress(source.address) : source.name; } catch { return false; }
  return provider.type === source.type && provider.address === address && (source.type !== 'lvaf' || provider.port === (source.port ?? 49152)) && (!policy ||
    provider.expected_profile === (source.analysis_profile ?? 1) && provider.expected_source_id === (source.source_id?.toLowerCase() ?? '0000000000000000') && provider.allow_synthetic === (source.allow_synthetic ?? false));
}
export function scopedInputSources(global: InputSource[], declarations: SceneSources = {}): InputSource[] {
  const fields = ['level', 'bass', 'mid', 'treble', 'centroid', 'onset', 'onsets', ...Array.from({ length: 64 }, (_, n) => 'band' + n.toString().padStart(2, '0'))];
  const scoped = Object.keys(declarations).flatMap(alias => fields.map(field => ({ name: alias + '.' + field, label: alias + ' → ' + field,
    origin: 'scene' as const, type: field === 'onsets' ? 'int' as const : 'float' as const, valid: false, value: null, age_ms: null })));
  return [...global.filter(source => !Object.hasOwn(declarations, source.name.split('.')[0])), ...scoped];
}
