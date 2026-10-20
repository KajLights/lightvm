import type { InputBindings, Manifest, Setting, TransitionSpec } from './types';
export type SettingsValues = Record<string, number | boolean>;
export type Blend = 'over' | 'add' | 'screen' | 'multiply';
export interface CompositionLayer { id: string; program: string; crc32: number; settings: SettingsValues; inputs?: InputBindings; opacity: number; blend: Blend }
export const BLENDS: Blend[] = ['over', 'add', 'screen', 'multiply'];
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
    else if ('control' in binding) {
      if (keys.length !== 1 || !((binding.control === 'color' && declaration.type === 'rgb8') ||
        (binding.control === 'brightness' && declaration.type === 'float' && (declaration.minimum ?? 0) <= 0 && (declaration.maximum ?? 1) >= 1))) throw new Error('Light control does not match ' + name + '.');
    }
    else if ('driver' in binding) {
      const driver = binding.driver;
      if (keys.length !== 1 || declaration.type !== 'float' || !driver || driver.kind !== 'ramp' || !Number.isFinite(driver.duration) || driver.duration <= 0 || driver.duration > 1e9 ||
        !['once', 'repeat', 'pingpong'].includes(driver.mode) || (driver.easing !== undefined && !['linear', 'smoothstep'].includes(driver.easing)) ||
        (driver.restart !== undefined && typeof driver.restart !== 'boolean') || Object.keys(driver).some(key => !['kind','duration','mode','easing','from','to','restart'].includes(key))) throw new Error('Invalid timer for ' + name + '.');
      validateSetting(declaration, driver.from ?? 0); validateSetting(declaration, driver.to ?? 1);
    } else if ('source' in binding) {
      if (!/^[A-Za-z0-9_.-]{1,32}$/.test(binding.source) || keys.some(key => !['source','timeout_ms','fallback','map','palette'].includes(key)) ||
        (binding.timeout_ms !== undefined && (!Number.isInteger(binding.timeout_ms) || binding.timeout_ms < 0 || binding.timeout_ms > 0xffffffff)) ||
        (binding.fallback !== undefined && !['default','hold'].includes(binding.fallback))) throw new Error('Invalid source binding for ' + name + '.');
      if (binding.palette !== undefined) {
        const palette = binding.palette;
        if (binding.map || declaration.type !== 'rgb8' || !palette || Object.keys(palette).length !== 4 ||
          ![palette.input_min,palette.input_max,palette.from,palette.to].every(value => typeof value === 'number' && Number.isFinite(value)) ||
          palette.input_min >= palette.input_max) throw new Error('Invalid colour palette for ' + name + '.');
        validateSetting(declaration, palette.from); validateSetting(declaration, palette.to);
      }
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
