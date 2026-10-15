import { describe, expect, it } from 'vitest';
import { supportsProgram } from './compiler';
import { DEFAULT_SOURCE, type Description, type Manifest } from './types';
import rainbow from '../../samples/python/rainbow.py?raw';

describe('animation compiler compatibility', () => {
  const legacy = { control_abi: 2, bytecode_format: 'LBC1/2', program_schemas: ['lightvm.program/3'] } as Description;
  const animation = { ...legacy, program_abis: [2, 3], bytecode_formats: ['LBC1/2', 'LBC1/3'], program_schemas: ['lightvm.program/3', 'lightvm.program/4'], program_profiles: ['animation/1'] };
  const v2: Manifest = { abi: 2, schema: 'lightvm.program/3', parameters: [] };
  const v3: Manifest = { abi: 3, schema: 'lightvm.program/4', profile: 'animation/1', parameters: [], settings: [] };
  it('keeps the editor default identical to the supplied Rainbow example', () => {
    expect(DEFAULT_SOURCE).toBe(rainbow);
    expect(DEFAULT_SOURCE).toContain('profile("animation/1")');
  });
  it('requires explicit support for the new format and profile', () => {
    expect(supportsProgram(legacy, v2)).toBe(true);
    expect(supportsProgram(legacy, v3)).toBe(false);
    expect(supportsProgram(animation, v2)).toBe(true);
    expect(supportsProgram(animation, v3)).toBe(true);
    expect(supportsProgram({ ...animation, program_profiles: [] }, v3)).toBe(false);
    expect(supportsProgram({ ...animation, program_abis: [2] }, v3)).toBe(false);
    expect(supportsProgram({ ...animation, bytecode_formats: ['LBC1/2'] }, v3)).toBe(false);
  });
});
