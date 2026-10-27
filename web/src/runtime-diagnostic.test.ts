import { expect, it } from 'vitest';
import { runtimeDiagnostic } from './runtime-diagnostic';
import type { Compilation, ProgramState } from './types';

it('only links a device fault to source for the identical compiled package', () => {
  const artifact = { debug: { crc32: 19, instructions: [{ line: 8, column: 5, blockId: 'paint' }] } } as Compilation;
  const state = { faulted: true, fault_pc: 0, program_crc32: 19, fault_message: 'Position 25 is outside 0 to 24.' } as ProgramState;
  expect(runtimeDiagnostic(state, artifact)).toEqual({ line: 8, column: 5, blockId: 'paint', message: state.fault_message });
  expect(runtimeDiagnostic({ ...state, program_crc32: 20 }, artifact)).toBeUndefined();
  expect(runtimeDiagnostic({ ...state, fault_pc: 2 }, artifact)).toBeUndefined();
  expect(runtimeDiagnostic({ ...state, faulted: false }, artifact)).toBeUndefined();
});
