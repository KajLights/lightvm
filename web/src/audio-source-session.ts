import type { LightVMClient } from '../protocol.mjs';
import type { AnalysisFrame } from './audio-features';
import { audioHex, audioNow, encodeAudioRecord, AUDIO_EVENT_TTL_US } from './audio-wire';

type Claim = { boot_id: string; generation: number; challenge: string };
const key = (f: AnalysisFrame) => `${f.source}/${f.session}/${f.profile}/${f.synthetic}`;
/** One independently owned named stream. No animation wiring or Sense tuning is
 * changed here. One operation is in flight; pending metrics are latest-only. */
export class AudioSourceSession {
  private stopped = false;
  private publisher?: Claim;
  private ownedStream = '';
  private wanted?: AnalysisFrame;
  private latest?: AnalysisFrame;
  private frameId = 0n;
  private lastSequence = 0n;
  private renewAt = 0n;
  private sentEvents = new Map<bigint, number>();
  private timer?: ReturnType<typeof setTimeout>;
  private running?: Promise<void>;
  constructor(private client: LightVMClient, private bootId: string, private failed: (error: Error) => void, private name = 'music') {}
  start(frame: AnalysisFrame) { this.publish(frame); clearTimeout(this.timer); this.timer = undefined; return this.launch(); }
  publish(frame: AnalysisFrame) {
    if (this.stopped || !frame.sequence) return;
    if (this.wanted && key(this.wanted) === key(frame) && frame.sequence <= this.wanted.sequence) return;
    this.wanted = this.latest = frame;
    // A pending keepalive must not hold fresh analysis until the next renewal.
    clearTimeout(this.timer); this.timer = undefined; this.schedule();
  }
  private schedule() {
    if (this.stopped || this.running || this.timer !== undefined || !this.wanted) return;
    const delay = this.latest ? 0 : Math.max(0, Number(this.renewAt - audioNow()) / 1000);
    this.timer = setTimeout(() => { this.timer = undefined; void this.launch().catch(() => {}); }, delay);
  }
  private launch() {
    if (this.running) return this.running;
    this.running = this.send().catch(error => {
      if (!this.stopped) { this.stopped = true; this.latest = undefined; this.failed(error as Error); }
      throw error;
    }).finally(() => { this.running = undefined; this.schedule(); });
    return this.running;
  }
  private async clock(claim: Claim) {
    if (claim.boot_id !== this.bootId) throw new Error('The controller rebooted. Reconnect before publishing audio.');
    await this.client.manage('audio_clock', { boot_id: claim.boot_id, generation: claim.generation,
      challenge: claim.challenge, publisher_us: audioHex(audioNow()) });
    this.renewAt = audioNow() + 3500000n;
  }
  private async send() {
    if (this.stopped || !this.wanted) return;
    const target = this.wanted, stream = key(target);
    if (!this.publisher || this.ownedStream !== stream) {
      const claim = await this.client.json('audio_claim', { name: this.name, source_id: audioHex(target.source), session: audioHex(target.session), analysis_profile: target.profile, synthetic: target.synthetic }) as Claim;
      if (claim.boot_id !== this.bootId) throw new Error('The controller rebooted. Reconnect before publishing audio.');
      this.publisher = claim; this.ownedStream = stream; this.frameId = this.lastSequence = 0n; this.sentEvents.clear();
      if (this.stopped) return;
      await this.clock(claim);
    } else if (audioNow() >= this.renewAt) {
      const claim = await this.client.json('audio_keep', { boot_id: this.bootId, generation: this.publisher.generation }) as Claim;
      if (this.stopped) return;
      await this.clock(claim);
    }
    if (this.stopped || !this.latest || key(this.latest) !== this.ownedStream) return;
    const frame = this.latest; this.latest = undefined;
    const now = audioNow();
    if (frame.sequence <= this.lastSequence || now < frame.audioUs || now - frame.audioUs > AUDIO_EVENT_TTL_US) return;
    const events = frame.events.filter(e => e.timeUs <= now && now - e.timeUs <= AUDIO_EVENT_TTL_US && (this.sentEvents.get(e.id) ?? 0) < 3);
    const record = encodeAudioRecord({ ...frame, events }, now);
    try { await this.client.publishAudio(this.publisher!.generation, ++this.frameId, record); }
    catch (error) {
      // A slow reliable transport can age an otherwise valid frame. Drop it;
      // ownership errors still stop forwarding and never auto-reclaim a name.
      if ((error as Error).message.includes('unavailable, mismatched or stale audio frame') && audioNow() - frame.audioUs > AUDIO_EVENT_TTL_US) return;
      throw error;
    }
    this.lastSequence = frame.sequence;
    for (const e of events) this.sentEvents.set(e.id, (this.sentEvents.get(e.id) ?? 0) + 1);
    const live = new Set(frame.events.map(e => e.id));
    for (const id of this.sentEvents.keys()) if (!live.has(id)) this.sentEvents.delete(id);
  }
  async stop() {
    this.stopped = true; clearTimeout(this.timer); this.latest = undefined;
    await this.running?.catch(() => {});
    if (this.publisher && !this.client.closed) {
      const claim = this.publisher; this.publisher = undefined;
      await this.client.manage('audio_release', { boot_id: claim.boot_id, generation: claim.generation }).catch(() => {});
    }
  }
}
