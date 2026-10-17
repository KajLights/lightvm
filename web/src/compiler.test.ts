import { describe, expect, it } from 'vitest';
import { supportsProgram, programCompatibility } from './compiler';
import { DEFAULT_SOURCE, type Description, type Manifest } from './types';
import rainbow from '../../examples/animations/rainbow.py?raw';

const target = (version: string) => ({ version } as Description);
const manifest: Manifest = { compiler_version: '1.20.0', requires: '1', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [] };
describe('animation release compatibility', () => {
  it('keeps the editor default identical to the supplied Rainbow example', () => {
    expect(DEFAULT_SOURCE).toBe(rainbow);
    expect(DEFAULT_SOURCE).toContain('profile("animation")');
  });
  it('accepts earlier minimum releases across minor and patch releases regardless of compiler provenance', () => {
    for (const version of ['1.0.0', '1.2.0', '1.15.20+local']) expect(supportsProgram(target(version), manifest)).toBe(true);
    expect(supportsProgram(target('1.2.9'), { ...manifest, requires: '1.2' })).toBe(true);
    expect(supportsProgram(target('1.1.9'), { ...manifest, requires: '1.2' })).toBe(false);
    expect(programCompatibility(target('1.1.9'), { ...manifest, requires: '1.2' })).toContain('Requires LightVM 1.2');
  });
  it('requires an explicit valid release and never admits a different major', () => {
    for (const version of ['0.9.9', '2.0.0', '1.0.0-alpha', 'invalid', '1']) expect(supportsProgram(target(version), manifest)).toBe(false);
    expect(supportsProgram(target('1.0.0'), { ...manifest, requires: undefined } as unknown as Manifest)).toBe(false);
    expect(supportsProgram(target('1.0.0'), { ...manifest, schema: 'lightvm.program/11' })).toBe(false);
  });
});
