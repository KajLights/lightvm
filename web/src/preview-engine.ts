import type { Compilation, DebugMap, Diagnostic } from './types';

export interface PreviewSnapshot {
  pixels: Uint8Array; phase: number; seconds: number; heap: number;
  location?: { line: number; column: number; blockId?: string };
  error?: Diagnostic; values: Record<string, number | boolean | string>;
}
// The narrow boundary is shared by the browser worker and native/Wasm parity tests.
export class PreviewEngine {
  private debug?: DebugMap;
  private points = new Map<number, DebugMap['checkpoints'][number]>();
  private count = 0;
  seconds = 0;
  constructor(private vm: any) {}
  open(artifact: Compilation, count: number, seed = 1, settings: Record<string, number | boolean> = {}) {
    if (artifact.diagnostics.length) throw new Error(artifact.diagnostics[0].message);
    const bytes = artifact.bytecode, ptr = this.vm._malloc(bytes.length), text = new TextEncoder().encode(JSON.stringify(settings) + '\0'), settingsPtr = this.vm._malloc(text.length);
    if (!ptr || !settingsPtr) { this.vm._free(ptr); this.vm._free(settingsPtr); throw new Error('There is not enough memory to start the preview.'); }
    try {
      this.vm.HEAPU8.set(bytes, ptr); this.vm.HEAPU8.set(text, settingsPtr);
      this.vm._preview_open(ptr, bytes.length, count, seed, settingsPtr);
    } finally { this.vm._free(ptr); this.vm._free(settingsPtr); }
    this.debug = artifact.debug; this.points = new Map(this.debug?.checkpoints.map(point => [point.pc, point]));
    this.count = Math.min(512, Math.max(0, count)); this.seconds = 0;
    return this.snapshot();
  }
  frame(delta: number) {
    if (!Number.isFinite(delta) || delta < 0 || delta > 1000) throw new Error('Choose a time step from 0 to 1,000 seconds.');
    const next = this.seconds + delta;
    if (this.vm._preview_frame(next, delta) < 0) throw new Error(this.message() || 'Finish the current frame before starting another.');
    this.seconds = next;
  }
  advance(step: boolean) {
    // Bound each worker turn even for a missing loop increment. The caller can
    // yield and resume, pause, or discard the worker immediately.
    const deadline = performance.now() + 8;
    for (let n = 0; n < 4096; n++) {
      const status = this.vm._preview_step(step ? 1 : 256);
      if (status !== 0 || step && this.points.has(this.vm._preview_pc()) || performance.now() >= deadline) break;
    }
    return this.snapshot();
  }
  input(index: number, value: number) {
    if (this.vm._preview_input(index, value) < 0) throw new Error('Use a value inside this input’s range, after the frame has finished.');
  }
  close() { this.vm._preview_close(); }
  private message() { return this.vm.UTF8ToString(this.vm._preview_error()); }
  snapshot(): PreviewSnapshot {
    const pc = this.vm._preview_pc(), phase = this.vm._preview_phase(), point = this.points.get(pc);
    const location = phase === 3 ? this.debug?.instructions[pc] : point;
    const message = this.message(), values: PreviewSnapshot['values'] = {};
    if (point && phase !== 3) for (const [name, reg] of Object.entries(point.variables).slice(0, 32)) {
      const bits = this.vm._preview_word(reg) >>> 0, type = this.vm._preview_type(reg);
      const buffer = new ArrayBuffer(4), view = new DataView(buffer); view.setUint32(0, bits, true);
      values[name] = type === 2 ? view.getFloat32(0, true) : type === 4 ? Boolean(bits) : type === 3 ? '#' + bits.toString(16).padStart(6, '0') : type === 7 ? (bits | 0) / 65536 : bits | 0;
    }
    const ptr = this.vm._preview_pixels();
    return { phase, seconds: this.seconds, pixels: this.vm.HEAPU8.slice(ptr, ptr + this.count * 4), heap: this.vm._preview_heap(),
      location, values, ...(message ? { error: { message, line: 1, column: 1, ...location } } : {}) };
  }
}
