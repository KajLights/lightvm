import type { Compilation, Diagnostic, ProgramState } from './types';

export function runtimeDiagnostic(state: ProgramState | null, artifact: Compilation | null): Diagnostic | undefined {
  if (!state?.faulted || state.fault_pc === undefined || !artifact?.debug || state.program_crc32 !== artifact.debug.crc32) return;
  const location = artifact.debug.instructions[state.fault_pc];
  if (!location) return;
  return { ...location, message: state.fault_message ?? 'The program stopped here. Try the local preview to check the values.' };
}
