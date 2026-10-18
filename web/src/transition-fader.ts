import type { LightVMClient } from '../protocol.mjs';
import type { TransitionState } from './types';

export const FADER_SOURCE = 'desktop.fader';
const UPDATE_INTERVAL_MS = 33;
interface Assignment {
  token: number;
  value?: number;
  timer?: ReturnType<typeof setTimeout>;
  sending: boolean;
  sentAt: number;
}

// The fader is a producer for the existing external transition source. Keep at
// most one update in flight and one newer value, independently of pointer rate.
export class TransitionFaderInput {
  private assignment: Assignment | null = null;
  revision = 0;
  constructor(private client: LightVMClient, private output: string,
    private changed: (progress: number) => void,
    private lost: (error?: Error) => void) {}

  get pending() { return Boolean(this.assignment?.sending || this.assignment?.value !== undefined); }
  attach(token: number) {
    this.stop();
    this.assignment = { token, sending: false, sentAt: -Infinity };
  }
  stop() {
    if (this.assignment) clearTimeout(this.assignment.timer);
    this.assignment = null; this.revision++;
  }
  observe(state: TransitionState) {
    if (this.assignment && (state.driver.kind !== 'source' || state.driver.source !== FADER_SOURCE ||
      state.driver.assignment !== this.assignment.token)) this.revoke();
  }
  publish(progress: number) {
    if (!this.assignment || !Number.isFinite(progress) || progress < 0 || progress > 100) return;
    this.revision++; this.assignment.value = progress; this.schedule(this.assignment);
  }
  private revoke(error?: Error) { this.stop(); this.lost(error); }
  private schedule(assignment: Assignment) {
    if (assignment.sending || assignment.timer !== undefined || assignment.value === undefined) return;
    assignment.timer = setTimeout(() => {
      assignment.timer = undefined; void this.send(assignment);
    }, Math.max(0, UPDATE_INTERVAL_MS - (Date.now() - assignment.sentAt)));
  }
  private async send(assignment: Assignment) {
    if (this.assignment !== assignment || assignment.value === undefined) return;
    const value = assignment.value;
    assignment.value = undefined; assignment.sending = true; assignment.sentAt = Date.now();
    try {
      const accepted = await this.client.updateTransitionSource(FADER_SOURCE, assignment.token, value, this.output);
      if (this.assignment !== assignment) return;
      if (!accepted) { this.revoke(); return; }
      this.changed(value);
    } catch (error) {
      if (this.assignment === assignment) this.revoke(error as Error);
    } finally {
      assignment.sending = false;
      if (this.assignment === assignment) this.schedule(assignment);
    }
  }
}
