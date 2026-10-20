import type { LightVMClient } from '../protocol.mjs';
import { AUDIO_SIGNALS, type AudioFeatures } from './audio-features';

export const MUSIC_SOURCE_PREFIX = 'music.';
/** A controller-wide feature publisher. It never reads or modifies animation wiring. */
export class AudioSourceSession {
  private stopped = false;
  private ready?: Promise<void>;
  private publisher?: { generation: number; boot_id: string };
  private sequence = 0;
  private latest?: AudioFeatures;
  private timer?: ReturnType<typeof setTimeout>;
  private sending?: Promise<void>;
  private sentAt = -Infinity;
  constructor(private client: LightVMClient, private bootId: string, private failed: (error: Error) => void) {}
  start() { return this.ready ??= this.prepare(); }
  private async prepare() {
    const claim = await this.client.json('input_claim', { signals: AUDIO_SIGNALS.map(name => ({ name: MUSIC_SOURCE_PREFIX + name, type: 'float' })) });
    if (claim.boot_id !== this.bootId) throw new Error('The controller rebooted. Reconnect before publishing music.');
    if (this.stopped) return;
    this.publisher = claim; this.schedule();
  }
  publish(values: AudioFeatures) {
    if (this.stopped || AUDIO_SIGNALS.some(name => !Number.isFinite(values[name]) || values[name] < 0 || values[name] > 1)) return;
    this.latest = { ...values }; this.schedule();
  }
  private schedule() {
    if (this.stopped || !this.publisher || !this.latest || this.sending || this.timer !== undefined) return;
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
        values: Object.fromEntries(AUDIO_SIGNALS.map(name => [MUSIC_SOURCE_PREFIX + name, frame[name]])) });
    } catch (error) {
      if (!this.stopped) { this.stopped = true; this.latest = undefined; this.failed(error as Error); }
    }
  }
  async stop() {
    this.stopped = true; clearTimeout(this.timer); this.latest = undefined;
    await this.ready?.catch(() => {}); await this.sending;
  }
}
