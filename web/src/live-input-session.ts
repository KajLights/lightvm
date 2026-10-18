import type { LightVMClient } from '../protocol.mjs';
import type { InputBindings, ProgramState } from './types';

export type InputTarget = Pick<ProgramState, 'output' | 'slot' | 'layer' | 'generation' | 'target'>;

/** One command in flight, newest values coalesced, and optimistic revision checks. */
export class LiveInputSession {
  private values: InputBindings = {};
  private timer?: ReturnType<typeof setTimeout>;
  private sending = false;
  private stopped = false;
  private sentAt = -Infinity;
  private epoch = 0;
  revision: number;
  constructor(private client: LightVMClient, private target: InputTarget, revision: number,
    private changed: (state: ProgramState) => void, private failed: (error: Error) => void) { this.revision = revision; }
  get pending() { return this.sending || Object.keys(this.values).length > 0; }
  get version() { return this.epoch; }
  observe(state: ProgramState) { if (!this.pending) this.revision = Math.max(this.revision, state.inputs_revision ?? 0); }
  publish(values: InputBindings) {
    if (this.stopped) return;
    this.epoch++; Object.assign(this.values, values); this.schedule();
  }
  stop() { this.stopped = true; clearTimeout(this.timer); this.values = {}; this.epoch++; }
  private schedule() {
    if (this.stopped || this.sending || this.timer !== undefined || !Object.keys(this.values).length) return;
    this.timer = setTimeout(() => { this.timer = undefined; void this.send(); }, Math.max(0, 33 - (Date.now() - this.sentAt)));
  }
  private async send() {
    if (this.stopped || this.sending) return;
    const inputs = this.values; this.values = {}; this.sending = true; this.sentAt = Date.now();
    try {
      const { generation, ...target } = this.target;
      const next = await this.client.json('program_inputs', { ...target, inputs, expected_generation: generation, expected_inputs_revision: this.revision }) as ProgramState;
      if (this.stopped) return;
      this.revision = next.inputs_revision ?? 0; this.changed(next);
    } catch (error) { if (!this.stopped) { this.stop(); this.failed(error as Error); } }
    finally { this.sending = false; this.schedule(); }
  }
}
