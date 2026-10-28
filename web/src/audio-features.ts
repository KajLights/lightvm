import { audioHex, audioRandom, type AudioRecord } from './audio-wire';
import { RECORD, PROFILES } from '../audio/pcm-adapter.mjs';
export const AUDIO_SIGNALS = ['level', 'bass', 'mid', 'treble', 'centroid', 'onset'] as const;
export type AudioSignal = typeof AUDIO_SIGNALS[number];
export type AudioFeatures = Record<AudioSignal, number>;
export const SILENCE: AudioFeatures = { level: 0, bass: 0, mid: 0, treble: 0, centroid: 0, onset: 0 };
export interface AudioParameters { gain_db: number; floor_db: number; attack_ms: number; release_ms: number; band_count: number; onset_threshold: number; onset_refractory_ms: number }
export interface AudioManifest {
  abi: number; revision: string; dependency: string;
  geometry: { sample_rate: number; window: number; hop: number; publish_hops: number };
  parameters: Record<keyof AudioParameters, { default: number; min: number; max: number; integer?: boolean }>;
  profiles: { id: number; label: string; detector: string }[];
}
export interface AnalysisFrame extends AudioRecord { profile: number; epoch: number; hop: number; sampleEnd: number; novelty: number; threshold: number; features: AudioFeatures }
export interface AudioSnapshot { live: AnalysisFrame[]; published: AnalysisFrame[] }
export interface AudioFixture { name: string; hash: string; rate: number; channels: Float32Array[] }
export interface AudioExport { manifest: AudioManifest; parameters: AudioParameters; input: Record<string, unknown>; streams: { profile: number; source_id: string; session: string }[]; truncated: boolean; epoch: number; columns: string; records: number[][] }
const base = () => new URL('audio/', new URL(import.meta.env.BASE_URL, document.baseURI));
let assets: Promise<{ manifest: AudioManifest; module: WebAssembly.Module }> | undefined;
export function loadAudioAssets() {
  return assets ??= (async () => {
    const [profile, revision, wasm] = await Promise.all(['profiles.json', 'revision.txt', 'kaj-audio.wasm'].map(name => fetch(new URL(name, base())).then(response => {
      if (!response.ok) throw new Error('Shared audio assets are unavailable. Build the pinned WASM analyser before starting capture.'); return response;
    })));
    const manifest = await profile.json() as AudioManifest;
    if (manifest.abi !== 1 || manifest.geometry.sample_rate !== 48000 || manifest.geometry.window !== 2048 || manifest.geometry.hop !== 1024 || manifest.geometry.publish_hops !== 5 || manifest.profiles.length !== 2 || ![1, 2].every(id => manifest.profiles.some(p => p.id === id))) throw new Error('Unsupported shared audio manifest.');
    manifest.dependency = (await revision.text()).trim();
    return { manifest, module: await WebAssembly.compile(await wasm.arrayBuffer()) };
  })().catch(error => { assets = undefined; throw error; });
}
export function defaultParameters(manifest: AudioManifest): AudioParameters {
  return Object.fromEntries(Object.entries(manifest.parameters).map(([name, schema]) => [name, schema.default])) as unknown as AudioParameters;
}
export function validateParameters(parameters: AudioParameters, manifest: AudioManifest) {
  for (const [name, schema] of Object.entries(manifest.parameters)) {
    const value = parameters[name as keyof AudioParameters];
    if (!Number.isFinite(value) || value < schema.min || value > schema.max || schema.integer && !Number.isInteger(value)) throw new Error(`Invalid audio parameter: ${name}`);
  }
}
export async function readAudioFixture(file: File): Promise<AudioFixture> {
  if (file.size > 32 * 1024 * 1024) throw new Error('Choose a PCM fixture of at most 32 MiB.');
  const buffer = await file.arrayBuffer(), view = new DataView(buffer), bytes = new Uint8Array(buffer);
  let rate = 48000, channels = 1, bits = 16, format = 1, offset = 0, length = bytes.length;
  const ascii = (start: number, size: number) => String.fromCharCode(...bytes.subarray(start, start + size));
  if (ascii(0, 4) === 'RIFF' && ascii(8, 4) === 'WAVE') {
    let foundFormat = false, foundData = false;
    for (let at = 12; at + 8 <= bytes.length;) {
      const size = view.getUint32(at + 4, true), start = at + 8;
      if (start + size > bytes.length) throw new Error('Truncated WAV fixture.');
      if (ascii(at, 4) === 'fmt ' && size >= 16) {
        format = view.getUint16(start, true); channels = view.getUint16(start + 2, true); rate = view.getUint32(start + 4, true); bits = view.getUint16(start + 14, true); foundFormat = true;
        if (view.getUint16(start + 12, true) !== channels * bits / 8) throw new Error('Invalid WAV sample alignment.');
      } else if (ascii(at, 4) === 'data') { offset = start; length = size; foundData = true; }
      at = start + size + (size & 1);
    }
    if (!foundFormat || !foundData) throw new Error('WAV requires format and PCM data chunks.');
  } else if (!file.name.toLowerCase().endsWith('.pcm')) throw new Error('Choose PCM16/float32 WAV, or mono 48 kHz signed-16 little-endian .pcm.');
  if (!(format === 1 && bits === 16 || format === 3 && bits === 32) || channels < 1 || channels > 8 || rate < 8000 || rate > 192000 || length % (channels * bits / 8)) throw new Error('Unsupported PCM fixture geometry.');
  const samples = length / (channels * bits / 8);
  if (!samples || samples / rate > 60) throw new Error('Choose a nonempty fixture of at most 60 seconds.');
  const data = Array.from({ length: channels }, () => new Float32Array(samples));
  for (let i = 0; i < samples; i++) for (let c = 0; c < channels; c++) {
    const at = offset + (i * channels + c) * bits / 8;
    data[c][i] = format === 1 ? view.getInt16(at, true) / 32768 : view.getFloat32(at, true);
  }
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', buffer)), b => b.toString(16).padStart(2, '0')).join('');
  return { name: file.name, hash, rate, channels: data };
}

/** The only connected node consumes PCM and has zero outputs. No graph path
 * connects to speakers, for either microphone capture or fixture replay. */
export class MicrophoneAudio {
  private stopped = false;
  private stream?: MediaStream;
  private context?: AudioContext;
  private source?: AudioNode;
  private node?: AudioWorkletNode;
  private clockTimer?: ReturnType<typeof setInterval>;
  private epoch = -1;
  private sources = [audioRandom(), audioRandom()];
  private sessions = [audioRandom(), audioRandom()];
  private pendingExport?: { promise: Promise<AudioExport>; resolve: (report: AudioExport) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> };
  private saved?: AudioExport;
  private manifest?: AudioManifest;
  private parameters!: AudioParameters;
  private identity: Record<string, unknown> = {};
  private offsetUs = 0;
  private delayUs = 0;
  private stopPromise?: Promise<void>;
  constructor(private frame: (snapshot: AudioSnapshot) => void, private failed: (error: Error) => void,
    private ended: () => void = () => {}) {}
  async start(deviceId: string, parameters: AudioParameters, channel = -1, fixture?: AudioFixture) {
    if (!globalThis.isSecureContext || !globalThis.AudioWorkletNode || !fixture && !navigator.mediaDevices?.getUserMedia) throw new Error('Audio capture needs HTTPS or localhost and AudioWorklet support.');
    this.parameters = { ...parameters };
    try {
      const context = this.context = new AudioContext(fixture ? { sampleRate: fixture.rate } : {});
      const resumed = context.resume();
      const captured = fixture ? Promise.resolve(undefined) : navigator.mediaDevices.getUserMedia({ audio: {
        ...(deviceId ? { deviceId: { exact: deviceId } } : {}), echoCancellation: false, noiseSuppression: false, autoGainControl: false,
      }, video: false });
      const [resumeResult, captureResult, assetResult] = await Promise.allSettled([resumed, captured, loadAudioAssets()]);
      if (captureResult.status === 'fulfilled') this.stream = captureResult.value;
      if (this.stopped) { await this.stop(); return; }
      if (resumeResult.status === 'rejected') throw resumeResult.reason;
      if (captureResult.status === 'rejected') throw captureResult.reason;
      if (assetResult.status === 'rejected') throw assetResult.reason;
      this.manifest = assetResult.value.manifest; validateParameters(parameters, this.manifest);
      const settings = this.stream?.getAudioTracks()[0].getSettings() as (MediaTrackSettings & { latency?: number }) | undefined;
      const channels = fixture?.channels.length ?? settings?.channelCount;
      if (!Number.isInteger(channel) || channel < -1 || channels !== undefined && channel >= channels) throw new Error('The selected channel is not present in this audio input.');
      const quantum = 128 / context.sampleRate;
      // Correlate rendered sample positions to the monotonic clock. currentTime
      // alone can pause/jump during device startup. Wait for a real timestamp
      // before connecting the input, then retain a fixed mapping per session.
      if (!context.getOutputTimestamp || !Number.isFinite(context.baseLatency) || !Number.isFinite(context.outputLatency)) throw new Error('This browser cannot correlate the audio capture clock. Use a browser with AudioContext output timestamp and latency support.');
      const guardUs = Math.max(25000, 4 * quantum * 1e6);
      this.delayUs = (settings?.latency ?? 0) * 1e6 + guardUs;
      this.offsetUs = 0;
      this.identity = { kind: fixture ? 'fixture' : 'microphone', name: fixture?.name ?? settings?.deviceId ?? deviceId,
        sha256: fixture?.hash ?? null, original_rate: fixture?.rate ?? settings?.sampleRate ?? null,
        context_rate: context.sampleRate, channels: fixture?.channels.length ?? settings?.channelCount ?? null,
        channel: channel < 0 ? 'average' : channel, speech_processing: fixture ? null : {
          echoCancellation: settings?.echoCancellation ?? 'unreported', noiseSuppression: settings?.noiseSuppression ?? 'unreported', autoGainControl: settings?.autoGainControl ?? 'unreported' },
        reported_capture_latency_s: settings?.latency ?? null, timestamp_guard_us: guardUs,
        output_latency_s: context.outputLatency, base_latency_s: context.baseLatency,
        timing: 'getOutputTimestamp sample correlation minus output/base latency and reported input latency plus render-block guard; FIR lookahead retained in sample timestamps',
        unreported_device_latency: !fixture && settings?.latency === undefined };
      await context.audioWorklet.addModule(new URL('audio-worklet.mjs', base()).href);
      if (this.stopped) { await this.stop(); return; }
      const node = this.node = new AudioWorkletNode(context, 'kaj-audio', { numberOfInputs: 1, numberOfOutputs: 0,
        channelCountMode: 'max', processorOptions: { module: assetResult.value.module, parameters, channel, offsetUs: this.offsetUs, delayUs: this.delayUs } });
      await new Promise<void>((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('Audio analyser initialization timed out.')), 10000);
        node.port.onmessage = event => {
          const data = event.data;
          if (data.type === 'ready') { clearTimeout(timer); resolve(); }
          else if (data.type === 'error') { clearTimeout(timer); reject(new Error(data.message)); this.fail(new Error(data.message)); }
          else if (data.type === 'frame') this.receive(data.buffer);
          else if (data.type === 'ended') { clearInterval(this.clockTimer); this.ended(); }
          else if (data.type === 'export' && this.pendingExport && this.manifest) {
            this.setEpoch(data.epoch);
            const view = new Float64Array(data.buffer), records = [];
            for (let at = 0; at < view.length; at += RECORD) records.push(Array.from(view.subarray(at, at + RECORD)));
            this.saved = { manifest: this.manifest, parameters: this.parameters, input: this.identity,
              streams: this.sources.map((source, i) => ({ profile: i + 1, source_id: audioHex(source), session: audioHex(this.sessions[i]) })), epoch: data.epoch, truncated: data.truncated,
              columns: 'profile,hop,capture_us,event_count,metrics[6],bands[64],edges[65],novelty,threshold,events[4*(id,time_us,strength)],band_count,epoch,publication_sequence,sample_end,reserved[3]', records };
            clearTimeout(this.pendingExport.timer); this.pendingExport.resolve(this.saved); this.pendingExport = undefined;
          }
        };
        node.onprocessorerror = () => { clearTimeout(timer); const error = new Error('Shared audio processor failed. Restart capture.'); reject(error); this.fail(error); };
      });
      if (this.stopped) { await this.stop(); return; }
      // Worklet module initialization can itself stall the rendering thread.
      // Observe a stable clock after initialization before capturing any PCM.
      await this.settleClock();
      if (this.stopped) { await this.stop(); return; }
      this.identity.output_latency_s = context.outputLatency; this.identity.base_latency_s = context.baseLatency;
      this.reset();
      if (fixture) {
        const buffer = context.createBuffer(fixture.channels.length, fixture.channels[0].length, fixture.rate);
        fixture.channels.forEach((samples, c) => buffer.copyToChannel(samples as Float32Array<ArrayBuffer>, c));
        const source = context.createBufferSource(); source.buffer = buffer; this.source = source;
        // Explicit sample boundaries exclude pre-roll and the source node's
        // post-end silence. Only the worklet signals completion, after DSP.
        const start = (Math.ceil(context.currentTime * context.sampleRate / 128) + 20) * 128;
        const end = start + Math.round(buffer.duration * context.sampleRate);
        this.identity.fixture_start_frame = start; this.identity.fixture_end_frame = end;
        node.port.postMessage({ type: 'fixture', start, end });
        source.onended = () => source.disconnect(); source.connect(node); source.start(start / context.sampleRate);
      } else {
        for (const track of this.stream!.getAudioTracks()) track.addEventListener('ended', () => this.fail(new Error('Audio input disconnected. Restart capture with the desired input.')), { once: true });
        this.source = context.createMediaStreamSource(this.stream!); this.source.connect(node);
      }
      context.onstatechange = () => { if (!this.stopped && context.state !== 'running') this.fail(new Error('Audio context suspended or changed. Start again to reset both histories.')); };
      this.clockTimer = setInterval(() => {
        const offset = this.captureOffset();
        if (offset === undefined) { this.fail(new Error('Audio clock lost its sample correlation. Restart capture.')); return; }
        if (Math.abs(offset - this.offsetUs) > guardUs) this.reset();
      }, 1000);
    } catch (error) { await this.stop(); throw error; }
  }
  private decode(view: Float64Array, base: number): AnalysisFrame | undefined {
    if (!view[base + 1]) return;
    const index = view[base] - 1, bands = view[base + 153], epoch = view[base + 154];
    const values = Array.from(view.subarray(base + 4, base + 10));
    return { profile: index + 1, epoch, hop: view[base + 1], sampleEnd: view[base + 156], novelty: view[base + 139], threshold: view[base + 140],
      source: this.sources[index], session: this.sessions[index], sequence: BigInt(view[base + 155]), audioUs: BigInt(view[base + 2]), synthetic: this.identity.kind === 'fixture',
      values, features: Object.fromEntries(AUDIO_SIGNALS.map((key, i) => [key, values[i]])) as AudioFeatures,
      bands: Array.from(view.subarray(base + 10, base + 10 + bands)), edges: Array.from(view.subarray(base + 74, base + 75 + bands)),
      events: Array.from({ length: view[base + 3] }, (_, i) => ({ id: BigInt(view[base + 141 + i * 3]), timeUs: BigInt(view[base + 142 + i * 3]), strength: view[base + 143 + i * 3] })) };
  }
  private receive(buffer: ArrayBuffer) {
    if (this.stopped) return;
    try {
      const view = new Float64Array(buffer), epoch = view[154];
      this.setEpoch(epoch);
      const live: AnalysisFrame[] = [], published: AnalysisFrame[] = [];
      for (let n = 0; n < PROFILES; n++) {
        const a = this.decode(view, n * RECORD), p = this.decode(view, (n + PROFILES) * RECORD);
        if (a) live.push(a); if (p && p.sequence) published.push(p);
      }
      this.frame({ live, published });
    } finally { this.node?.port.postMessage(buffer, [buffer]); }
  }
  private setEpoch(epoch: number) { if (epoch !== this.epoch) { this.epoch = epoch; this.sessions = [audioRandom(), audioRandom()]; } }
  private captureOffset(): number | undefined {
    const context = this.context, time = context?.getOutputTimestamp();
    if (!context || !time?.contextTime || !time.performanceTime) return;
    return time.performanceTime * 1000 - (time.contextTime + context.outputLatency + context.baseLatency) * 1e6;
  }
  private async settleClock() {
    const context = this.context!, deadline = performance.now() + 5000;
    let previous: number | undefined, stableSince = performance.now();
    while (!this.stopped) {
      const now = performance.now(), offset = this.captureOffset();
      if (offset === undefined || previous === undefined || Math.abs(offset - previous) > 256e6 / context.sampleRate) stableSince = now;
      else if (now - stableSince >= Math.max(100, 2e3 * (context.outputLatency + context.baseLatency))) return;
      if (now > deadline) throw new Error('Audio render clock did not stabilize. Restart capture.');
      previous = offset; await new Promise(resolve => setTimeout(resolve, 20));
    }
  }
  reset() {
    if (!this.context || !this.node) return;
    const offset = this.captureOffset();
    if (offset === undefined) { this.fail(new Error('Audio clock lost its sample correlation. Restart capture.')); return; }
    this.offsetUs = offset;
    this.node.port.postMessage({ type: 'reset', offsetUs: this.offsetUs, delayUs: this.delayUs });
  }
  get inputInfo() { return this.identity; }
  export(): Promise<AudioExport> {
    if (this.pendingExport) return this.pendingExport.promise;
    if (!this.node || this.stopped) return this.saved ? Promise.resolve(this.saved) : Promise.reject(new Error('No audio comparison has been captured.'));
    let resolve!: (report: AudioExport) => void, reject!: (error: Error) => void;
    const promise = new Promise<AudioExport>((yes, no) => { resolve = yes; reject = no; });
    const timer = setTimeout(() => { this.pendingExport = undefined; reject(new Error('Audio comparison export timed out.')); }, 2000);
    this.pendingExport = { promise, resolve, reject, timer }; this.node.port.postMessage({ type: 'export', id: 1 }); return promise;
  }
  private fail(error: Error) { if (!this.stopped) { void this.stop(); this.failed(error); } }
  async stop() {
    clearInterval(this.clockTimer); this.source?.disconnect(); this.stream?.getTracks().forEach(track => track.stop());
    if (this.stopPromise) return this.stopPromise;
    const report = !this.stopped && this.node ? this.export().catch(() => undefined) : Promise.resolve(); this.stopped = true;
    return this.stopPromise = (async () => {
      await report;
      this.node?.port.postMessage({ type: 'stop' }); this.node?.disconnect(); this.node = undefined;
      if (this.context && this.context.state !== 'closed') await this.context.close().catch(() => {});
    })();
  }
}
