import { parseBlocks } from './languages/blocks';
import type { InputBindings } from './types';
import type { SettingsValues } from './program';

export interface ProgramFile {
  schema: 'lightvm.source';
  filename: string;
  name: string;
  source: string;
  settings: SettingsValues;
  inputs: InputBindings;
}
export function sourceName(filename: string) {
  return filename.replace(/(?:\.blocks\.json|\.py|\.js)$/i, '').replace(/[^A-Za-z0-9_.-]/g, '_').slice(0, 32) || 'animation';
}
export function parseProgramFile(text: string, filename: string): ProgramFile {
  if (/(?:\.py|\.js|\.blocks\.json)$/i.test(filename)) {
    if (text.length > 65536) throw new Error('Choose source smaller than 64 KiB.');
    if (/\.blocks\.json$/i.test(filename)) parseBlocks(text);
    return { schema: 'lightvm.source', filename, name: sourceName(filename), source: text, settings: {}, inputs: {} };
  }
  const data = JSON.parse(text) as ProgramFile;
  const object = (value: unknown) => value !== null && typeof value === 'object' && !Array.isArray(value);
  if (data?.schema !== 'lightvm.source' || typeof data.source !== 'string' || data.source.length > 65536 ||
    typeof data.filename !== 'string' || !/^[^/\\]{1,128}(?:\.py|\.js|\.blocks\.json)$/i.test(data.filename) ||
    typeof data.name !== 'string' || !/^[A-Za-z0-9_.-]{1,32}$/.test(data.name) ||
    !object(data.settings) || !object(data.inputs) || Object.values(data.settings).some(value =>
      typeof value !== 'boolean' && (typeof value !== 'number' || !Number.isFinite(value)))) {
    throw new Error('Choose a LightVM source file, Python (.py), JavaScript (.js) or Blocks (.blocks.json).');
  }
  if (/\.blocks\.json$/i.test(data.filename)) parseBlocks(data.source);
  return data;
}
