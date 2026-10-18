import type { LightVMClient } from '../protocol.mjs';
import type { ProgramState } from './types';
import { EYE_INPUTS, type EyeValues } from './audio-features';

export const EYE_SOURCE_PREFIX = 'audio.eye.';
export interface EyeTarget {
  output?: string; slot: string; layer?: string; generation: number; revision: number;
  bindings: Record<string, unknown>;
}

/** Discover the actual running contract; edited source does not identify the live target. */
export function audioEyeTarget(state: ProgramState | null): EyeTarget | undefined {
  if (!state || state.faulted || state.manifest.profile !== 'animation') return;
  const declarations = 'inputs' in state.manifest ? state.manifest.inputs : undefined;
  const revision = 'inputs_revision' in state ? state.inputs_revision : undefined;
  const inputs = 'inputs' in state ? state.inputs : undefined;
  if (!Array.isArray(declarations) || !Number.isInteger(revision) || !inputs || typeof inputs !== 'object') return;
  const bindings: Record<string, unknown> = {};
  for (const name of EYE_INPUTS) {
    if (!declarations.some(port => port.name === name && port.type === 'float' &&
      (port.minimum === undefined || port.minimum <= 0) && (port.maximum === undefined || port.maximum >= 1))) return;
    const port = (inputs as Record<string, { binding?: unknown }>)[name];
    if (!port?.binding) return;
    bindings[name] = port.binding;
  }
  return { output: state.output, slot: state.slot, layer: state.layer, generation: state.generation,
    revision: revision as number, bindings };
}

/** One atomic batch at most every 33 ms, one in flight, and only the newest pending frame. */
export class AudioEyeSession {
  private stopped = false;
  private bound = false;
  private ready?: Promise<void>;
  private stopping?: Promise<void>;
  private publisher?: { generation: number; boot_id: string };
  private sequence = 0;
  private revision = 0;
  private latest?: EyeValues;
  private timer?: ReturnType<typeof setTimeout>;
  private sending?: Promise<void>;
  private sentAt = -Infinity;
  constructor(private client: LightVMClient, readonly target: EyeTarget, private bootId: string,
    private changed: (state: ProgramState) => void, private failed: (error: Error) => void) {}
  start() { return this.ready ??= this.prepare(); }
  private address() {
    const { output, slot, layer, generation } = this.target;
    return { output, slot, layer, expected_generation: generation };
  }
  private async prepare() {
    const claim = await this.client.json('input_claim', { signals: EYE_INPUTS.map(name => ({ name: EYE_SOURCE_PREFIX + name, type: 'float' })) });
    if (claim.boot_id !== this.bootId) throw new Error('The controller rebooted. Reconnect before driving the Eye.');
    this.publisher = claim;
    if (this.stopped) return;
    const inputs = Object.fromEntries(EYE_INPUTS.map(name => [name, {
      source: EYE_SOURCE_PREFIX + name, timeout_ms: 1000, fallback: 'default',
    }]));
    const next = await this.client.json('program_inputs', { ...this.address(), expected_inputs_revision: this.target.revision, inputs });
    this.revision = next.inputs_revision; this.bound = true;
    if (!this.stopped) { this.changed(next); this.schedule(); }
  }
  publish(values: EyeValues) {
    if (this.stopped || EYE_INPUTS.some(name => !Number.isFinite(values[name]) || values[name] < 0 || values[name] > 1)) return;
    this.latest = { ...values }; this.schedule();
  }
  observe(state: ProgramState | null) {
    if (!this.bound || this.stopped) return;
    const current = audioEyeTarget(state);
    if (!current || current.revision < this.revision) return; // Ignore a poll begun before binding.
    if (current.revision !== this.revision || EYE_INPUTS.some(name => {
      const binding = current.bindings[name] as { source?: string };
      return binding.source !== EYE_SOURCE_PREFIX + name;
    })) {
      this.bound = false;
      this.failed(new Error('The Eye inputs changed elsewhere. Review the wiring before taking control again.'));
      void this.stop();
    }
  }
  private schedule() {
    if (this.stopped || !this.bound || !this.latest || this.sending || this.timer !== undefined) return;
    this.timer = setTimeout(() => {
      this.timer = undefined;
      this.sending = this.send().finally(() => { this.sending = undefined; this.schedule(); });
    }, Math.max(0, 33 - (Date.now() - this.sentAt)));
  }
  private async send() {
    if (this.stopped || !this.latest) return;
    const frame = this.latest; this.latest = undefined; this.sentAt = Date.now();
    try {
      await this.client.manage('input_publish', { ...this.publisher, sequence: ++this.sequence,
        values: Object.fromEntries(EYE_INPUTS.map(name => [EYE_SOURCE_PREFIX + name, frame[name]])) });
    } catch (error) {
      if (!this.stopped) { this.failed(error as Error); void this.stop().catch(this.failed); }
    }
  }
  stop(): Promise<void> {
    if (this.stopping) return this.stopping;
    this.stopped = true; clearTimeout(this.timer); this.latest = undefined;
    return this.stopping = this.release();
  }
  private async release() {
    await this.ready?.catch(() => {}); await this.sending;
    if (!this.bound || this.client.closed) return;
    this.bound = false;
    // Restore only if nobody has rewired this instance since we took control.
    const next = await this.client.json('program_inputs', { ...this.address(), expected_inputs_revision: this.revision, inputs: this.target.bindings });
    this.changed(next);
  }
}
