import { parseRelease } from './release';
import { crc32 } from '../protocol.mjs';
import type { Compilation, InputBindings, Manifest, Setting, TransitionSpec } from './types';

export type SettingsValues = Record<string, number | boolean>;
export type Blend = 'over' | 'add' | 'screen' | 'multiply';
export interface SavedAnimation {
  id: string; name: string; version: number; filename: string; source: string;
  bytecode: string; digest: string; manifest: Manifest; settings: SettingsValues; inputs?: InputBindings;
}
export interface PresetLayer { id: string; animation: string; settings: SettingsValues; inputs?: InputBindings; opacity: number; blend: Blend }
export interface SavedPreset { id: string; name: string; layers: PresetLayer[]; entry_transition?: TransitionSpec }
export interface Library { schema: 'lightvm.library'; animations: SavedAnimation[]; presets: SavedPreset[] }
export interface CompositionLayer { id: string; program: string; crc32: number; settings: SettingsValues; inputs?: InputBindings; opacity: number; blend: Blend }
export const LIBRARY_KEY = 'lightvm.library.v1';
export const BLENDS: Blend[] = ['over', 'add', 'screen', 'multiply'];
export const emptyLibrary = (): Library => ({ schema: 'lightvm.library', animations: [], presets: [] });
export function bytesFor(animation: SavedAnimation) { return Uint8Array.from(atob(animation.bytecode), byte => byte.charCodeAt(0)); }
export async function digestFor(bytes: Uint8Array) {
  const digest = await crypto.subtle.digest('SHA-256', new Uint8Array(bytes));
  return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}
export function programName(animation: SavedAnimation) { return 'a' + animation.digest.slice(0, 31); }
export function settingsFor(manifest: Manifest, settings: SettingsValues): SettingsValues {
  return Object.fromEntries((manifest.settings ?? []).filter(setting => Object.hasOwn(settings, setting.name)).map(setting => {
    const value = settings[setting.name]; validateSetting(setting, value); return [setting.name, value];
  }));
}
export function validateSetting(setting: Setting, value: unknown) {
  const valid = setting.type === 'bool' ? typeof value === 'boolean' : typeof value === 'number' && Number.isFinite(value) &&
    (setting.type === 'float' || Number.isInteger(value)) &&
    (setting.type !== 'int' || (value >= -2147483648 && value <= 2147483647)) &&
    (setting.type !== 'rgb8' || (value >= 0 && value <= 0xffffff)) &&
    (setting.minimum === undefined || value >= setting.minimum) && (setting.maximum === undefined || value <= setting.maximum);
  if (!valid) throw new Error('Enter a valid ' + setting.type + ' value for ' + setting.name + '.');
}
export function inputsFor(manifest: Manifest, bindings: InputBindings): InputBindings {
  if (!bindings || typeof bindings !== 'object' || Array.isArray(bindings)) throw new Error('Input bindings must be an object.');
  for (const [name, binding] of Object.entries(bindings)) {
    const declaration = manifest.inputs?.find(input => input.name === name);
    if (!declaration) throw new Error('Unknown input: ' + name + '.');
    if (!binding || typeof binding !== 'object' || Array.isArray(binding)) throw new Error('Invalid binding for ' + name + '.');
    const keys = Object.keys(binding);
    if ('value' in binding) { if (keys.length !== 1) throw new Error('Use one driver for ' + name + '.'); validateSetting(declaration, binding.value); }
    else if ('default' in binding) { if (keys.length !== 1 || binding.default !== true) throw new Error('default must be true.'); }
    else if ('driver' in binding) {
      const driver = binding.driver;
      if (keys.length !== 1 || declaration.type !== 'float' || !driver || driver.kind !== 'ramp' || !Number.isFinite(driver.duration) || driver.duration <= 0 || driver.duration > 1e9 ||
        !['once', 'repeat', 'pingpong'].includes(driver.mode) || (driver.easing !== undefined && !['linear', 'smoothstep'].includes(driver.easing)) ||
        (driver.restart !== undefined && typeof driver.restart !== 'boolean') || Object.keys(driver).some(key => !['kind','duration','mode','easing','from','to','restart'].includes(key))) throw new Error('Invalid timer for ' + name + '.');
      validateSetting(declaration, driver.from ?? 0); validateSetting(declaration, driver.to ?? 1);
    } else if ('source' in binding) {
      if (!/^[A-Za-z0-9_.-]{1,32}$/.test(binding.source) || keys.some(key => !['source','timeout_ms','fallback','map'].includes(key)) ||
        (binding.timeout_ms !== undefined && (!Number.isInteger(binding.timeout_ms) || binding.timeout_ms < 0 || binding.timeout_ms > 0xffffffff)) ||
        (binding.fallback !== undefined && !['default','hold'].includes(binding.fallback))) throw new Error('Invalid source binding for ' + name + '.');
      if (binding.map !== undefined) {
        const map = binding.map;
        if (!['int','float'].includes(declaration.type) || !map || Object.keys(map).length !== 4 ||
          ![map.input_min,map.input_max,map.output_min,map.output_max].every(value => typeof value === 'number' && Number.isFinite(value)) || map.input_min >= map.input_max) throw new Error('Invalid mapping for ' + name + '.');
        const nearest = (value: number) => Math.sign(value) * Math.floor(Math.abs(value) + 0.5);
        validateSetting(declaration, declaration.type === 'int' ? nearest(map.output_min) : map.output_min);
        validateSetting(declaration, declaration.type === 'int' ? nearest(map.output_max) : map.output_max);
      }
    } else throw new Error('Choose a value, timer, source or default for ' + name + '.');
  }
  return structuredClone(bindings);
}
export function programManifest(bytes: Uint8Array): Manifest {
  const invalid = () => new Error('Unsupported compiled program. Recompile its source with the current compiler.');
  if (bytes.length < 96 || new TextDecoder().decode(bytes.subarray(0, 4)) !== 'LBC1') throw invalid();
  const size = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(76, true);
  if (!size || size > bytes.length - 96) throw invalid();
  let manifest: Manifest;
  try { manifest = JSON.parse(new TextDecoder().decode(bytes.subarray(bytes.length - size))); }
  catch { throw invalid(); }
  if (!manifest || manifest.schema !== 'lightvm.program' || !Array.isArray(manifest.settings) ||
    manifest.settings.some(setting => !setting || typeof setting.name !== 'string' || !['bool', 'int', 'float', 'rgb8'].includes(setting.type))) throw invalid();
  return manifest;
}
export function validateArtifact(bytes: Uint8Array, manifest: Manifest) {
  const invalid = () => new Error('Unsupported compiled animation. Recompile its source with the current compiler.');
  // Reading a library never requires the connected firmware (or this editor)
  // to support execution. Keep source and recipes available across releases.
  if (!manifest || typeof manifest.schema !== 'string' || typeof manifest.profile !== 'string' ||
      !Array.isArray(manifest.settings) || bytes.length < 96) throw invalid();
  if (manifest.requires !== undefined && !parseRelease(manifest.requires)) throw invalid();
  if (manifest.compiler_version !== undefined && !parseRelease(manifest.compiler_version, false)) throw invalid();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const size = view.getUint32(76, true), offset = bytes.length - size;
  if (new TextDecoder().decode(bytes.subarray(0,4)) !== 'LBC1' || !size || offset < 96) throw invalid();
  let embedded: Manifest;
  try { embedded = JSON.parse(new TextDecoder().decode(bytes.subarray(offset))); } catch { throw invalid(); }
  const canonical = (value: any): any => Array.isArray(value) ? value.map(canonical) : value && typeof value === 'object' ? Object.fromEntries(Object.keys(value).sort().map(key => [key, canonical(value[key])])) : value;
  if (JSON.stringify(canonical(embedded)) !== JSON.stringify(canonical(manifest))) throw invalid();
}
export async function saveAnimation(library: Library, compiled: Compilation, filename: string, name: string, settings: SettingsValues, inputs: InputBindings = {}) {
  validateArtifact(compiled.bytecode, compiled.manifest);
  if (!name.trim() || name.length > 80) throw new Error('Give the animation a name of 1–80 characters.');
  const version = Math.max(0, ...library.animations.filter(item => item.name === name.trim()).map(item => item.version)) + 1;
  const animation: SavedAnimation = {
    id: crypto.randomUUID(), name: name.trim(), version, filename, source: compiled.source,
    bytecode: btoa(Array.from(compiled.bytecode, byte => String.fromCharCode(byte)).join('')),
    digest: await digestFor(compiled.bytecode), manifest: compiled.manifest, settings: settingsFor(compiled.manifest, settings),
    ...(Object.keys(inputs).length ? { inputs: inputsFor(compiled.manifest, inputs) } : {}),
  };
  return { animation, library: { ...library, animations: [...library.animations, animation] } };
}
export function compositionLayers(library: Library, layers: PresetLayer[]): CompositionLayer[] {
  if (!layers.length || layers.length > 8) throw new Error('Choose between one and eight layers.');
  return layers.map(layer => {
    const animation = library.animations.find(item => item.id === layer.animation);
    if (!animation) throw new Error('The saved animation for layer ' + layer.id + ' is missing.');
    if (animation.manifest.profile === 'transition') throw new Error('A transition combines A and B; it cannot be a composition layer.');
    const inputs = inputsFor(animation.manifest, layer.inputs ?? animation.inputs ?? {});
    return { id: layer.id, program: programName(animation), crc32: crc32(bytesFor(animation)), settings: settingsFor(animation.manifest, layer.settings), opacity: layer.opacity, blend: layer.blend,
      ...(Object.keys(inputs).length ? { inputs } : {}) };
  });
}

function checkSettings(value: unknown): value is SettingsValues {
  return Boolean(value && typeof value === 'object' && !Array.isArray(value) && Object.values(value).every(item => typeof item === 'boolean' || (typeof item === 'number' && Number.isFinite(item))));
}
export function validTransitionSpec(value: unknown): value is TransitionSpec {
  const spec = value as TransitionSpec | undefined;
  return Boolean(spec && typeof spec.program === 'string' && /^[A-Za-z0-9_.-]{1,32}$/.test(spec.program) && Number.isInteger(spec.crc32) && spec.crc32 >= 0 && spec.crc32 <= 0xffffffff &&
    typeof spec.duration_ms === 'number' && Number.isFinite(spec.duration_ms * 1000) && spec.duration_ms >= 0 &&
    (spec.seed === undefined || (Number.isInteger(spec.seed) && spec.seed >= 0 && spec.seed <= 0xffffffff)) &&
    (spec.settings === undefined || checkSettings(spec.settings)));
}
export function parseLibrary(text: string): Library {
  if (text.length > 5 * 1024 * 1024) throw new Error('Choose a library smaller than 5 MiB.');
  const library = JSON.parse(text) as Library;
  // Recognize the library namespace for source recovery, including old saved
  // envelopes. Structural checks below are independent of execution support.
  if (typeof library?.schema !== 'string' || library.schema.split('/')[0] !== 'lightvm.library' || !Array.isArray(library.animations) || !Array.isArray(library.presets)) throw new Error('Unsupported animation library.');
  const ids = new Set<string>();
  for (const animation of library.animations) {
    if (!animation || typeof animation.id !== 'string' || !animation.id || ids.has(animation.id) || typeof animation.name !== 'string' || !animation.name.trim() || animation.name.length > 80 ||
      !Number.isSafeInteger(animation.version) || animation.version < 1 || typeof animation.source !== 'string' || animation.source.length > 65536 ||
      typeof animation.filename !== 'string' || typeof animation.bytecode !== 'string' || animation.bytecode.length > 1500000 || !/^[a-f0-9]{64}$/.test(animation.digest) ||
      !animation.manifest || !Array.isArray(animation.manifest.settings) || !checkSettings(animation.settings)) throw new Error('Invalid saved animation.');
    ids.add(animation.id);
    for (const setting of animation.manifest.settings ?? []) if (!setting || typeof setting.name !== 'string' || !['bool', 'int', 'rgb8', 'float'].includes(setting.type)) throw new Error('Invalid animation setting.');
    validateArtifact(bytesFor(animation), animation.manifest);
    settingsFor(animation.manifest, animation.settings);
    inputsFor(animation.manifest, animation.inputs ?? {});
  }
  const presetIds = new Set<string>();
  for (const preset of library.presets) {
    if (!preset || typeof preset.id !== 'string' || !preset.id || presetIds.has(preset.id) || typeof preset.name !== 'string' || !/^[A-Za-z0-9_.-]{1,32}$/.test(preset.name) || !Array.isArray(preset.layers) || !preset.layers.length || preset.layers.length > 8) throw new Error('Invalid preset.');
    if (preset.entry_transition !== undefined && !validTransitionSpec(preset.entry_transition)) throw new Error('Invalid preset entry transition.');
    presetIds.add(preset.id); const layerIds = new Set<string>();
    for (const layer of preset.layers) {
      if (!layer || typeof layer.id !== 'string' || !/^[A-Za-z0-9_.-]{1,32}$/.test(layer.id) || layerIds.has(layer.id) || !ids.has(layer.animation) ||
        !checkSettings(layer.settings) || !Number.isInteger(layer.opacity) || layer.opacity < 0 || layer.opacity > 255 || !BLENDS.includes(layer.blend)) throw new Error('Invalid preset layer.');
      layerIds.add(layer.id);
    }
    compositionLayers(library, preset.layers);
  }
  return { ...library, schema: 'lightvm.library' };
}
export function readLibrary(): { library: Library; error: string; backup: string | null } {
  let saved: string | null = null;
  try {
    saved = localStorage.getItem(LIBRARY_KEY);
    return { library: saved ? parseLibrary(saved) : emptyLibrary(), error: '', backup: null };
  } catch {
    return { library: emptyLibrary(), backup: saved, error: 'The saved library could not be opened. Export its preserved backup to recover source, then recompile with the current compiler. Existing browser data remains until an explicit import.' };
  }
}
export function writeLibrary(library: Library) {
  // Commit storage before updating the UI. Quota errors must never look like a successful save.
  localStorage.setItem(LIBRARY_KEY, JSON.stringify(library));
}
export async function importLibrary(current: Library, text: string) {
  const incoming = parseLibrary(text);
  for (const animation of incoming.animations) if (await digestFor(bytesFor(animation)) !== animation.digest) throw new Error('Compiled animation checksum differs from its library version.');
  function merge<T extends { id: string }>(existing: T[], added: T[]) {
    const result = [...existing];
    for (const item of added) {
      const match = result.find(value => value.id === item.id);
      if (match && JSON.stringify(match) !== JSON.stringify(item)) throw new Error('A library item has a conflicting version.');
      if (!match) result.push(item);
    }
    return result;
  }
  return { ...current, animations: merge(current.animations, incoming.animations), presets: merge(current.presets, incoming.presets) };
}
