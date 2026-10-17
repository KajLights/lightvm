import { describe, expect, it } from 'vitest';
import { supportsProgram } from './compiler';
import { DEFAULT_SOURCE, type Description, type Manifest } from './types';
import rainbow from '../../examples/animations/rainbow.py?raw';

describe('animation compiler', () => {
  const animation = { program_abis: [4], bytecode_formats: ['LBC1/4'], program_schemas: ['lightvm.program/11'], program_profiles: ['animation/1'] } as Description;
  const v4: Manifest = { abi: 4, schema: 'lightvm.program/11', profile: 'animation/1', pixels: { format: 'rgba8-premul' }, settings: [] };
  it('keeps the editor default identical to the supplied Rainbow example', () => {
    expect(DEFAULT_SOURCE).toBe(rainbow);
    expect(DEFAULT_SOURCE).toContain('profile("animation/1")');
  });
  it('requires the target to advertise the format and profile', () => {
    expect(supportsProgram(animation, v4)).toBe(true);
    expect(supportsProgram({ ...animation, program_profiles: [] }, v4)).toBe(false);
    expect(supportsProgram({ ...animation, program_abis: [] }, v4)).toBe(false);
    expect(supportsProgram({ ...animation, bytecode_formats: [] }, v4)).toBe(false);
    expect(supportsProgram({ ...animation, program_schemas: ['lightvm.program/5'] }, v4)).toBe(false);
    expect(supportsProgram(animation, { ...v4, profile: 'layer/1' })).toBe(false);
    expect(supportsProgram({ control_abi: 4, bytecode_format: 'LBC1/4', program_schemas: ['lightvm.program/11'] } as unknown as Description, v4)).toBe(false);
    expect(supportsProgram(animation, { ...v4, schema: undefined } as unknown as Manifest)).toBe(false);
  });
});
