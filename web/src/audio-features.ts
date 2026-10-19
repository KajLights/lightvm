export const AUDIO_SIGNALS = ['level', 'bass', 'mid', 'treble', 'centroid', 'onset'] as const;
export type AudioSignal = typeof AUDIO_SIGNALS[number];
export type AudioFeatures = Record<AudioSignal, number>;
export const SILENCE: AudioFeatures = { level: 0, bass: 0, mid: 0, treble: 0, centroid: 0, onset: 0 };
export interface AudioResponse { gainDb: number; floorDb: number; releaseMs: number }
export const DEFAULT_RESPONSE: AudioResponse = { gainDb: 0, floorDb: -50, releaseMs: 220 };
export const EYE_INPUTS = ['position', 'width', 'hue', 'brightness'] as const;
export type EyeInput = typeof EYE_INPUTS[number];
export type EyeValues = Record<EyeInput, number>;
export interface AudioMapping { signal: AudioSignal; inputMinimum: number; inputMaximum: number; minimum: number; maximum: number }
export type EyeMappings = Record<EyeInput, AudioMapping>;
export const DEFAULT_MAPPINGS: EyeMappings = {
  position: { signal: 'centroid', inputMinimum: 0, inputMaximum: 1, minimum: 0.05, maximum: 0.95 },
  width: { signal: 'bass', inputMinimum: 0, inputMaximum: 1, minimum: 0.04, maximum: 0.4 },
  hue: { signal: 'treble', inputMinimum: 0, inputMaximum: 1, minimum: 0, maximum: 0.85 },
  brightness: { signal: 'level', inputMinimum: 0, inputMaximum: 1, minimum: 0, maximum: 1 },
};
const clamp = (value: number) => Number.isFinite(value) ? Math.max(0, Math.min(1, value)) : 0;
export function normalizeAudioSignal(value: number, mapping: AudioMapping) {
  const span = mapping.inputMaximum - mapping.inputMinimum;
  if (!Number.isFinite(span) || span <= 0 || !Number.isFinite(value)) return 0;
  return clamp((value - mapping.inputMinimum) / span);
}
export function mapAudioToEye(features: AudioFeatures, mappings: EyeMappings): EyeValues {
  return Object.fromEntries(EYE_INPUTS.map(name => {
    const mapping = mappings[name], amount = normalizeAudioSignal(features[mapping.signal], mapping);
    return [name, clamp(mapping.minimum + amount * (mapping.maximum - mapping.minimum))];
  })) as EyeValues;
}

/** Browser FFT magnitudes are dB amplitudes. Sum linear power before measuring a band. */
export class AudioFeatureExtractor {
  private smoothed = { ...SILENCE };
  private previousTime?: number;
  private startedAt?: number;
  private slowLevel = 0;
  private lastOnset = -Infinity;
  private pulse = 0;
  extract(waveform: Float32Array, spectrumDb: Float32Array, sampleRate: number,
    now: number, response: AudioResponse): AudioFeatures {
    const elapsed = this.previousTime === undefined ? 33 : Math.max(0, Math.min(1000, now - this.previousTime));
    this.previousTime = now; this.startedAt ??= now;
    let squares = 0, bass = 0, mid = 0, treble = 0, weighted = 0, amplitudes = 0;
    for (const sample of waveform) if (Number.isFinite(sample)) squares += sample * sample;
    const rms = Math.sqrt(squares / Math.max(1, waveform.length));
    const binHz = sampleRate / (spectrumDb.length * 2);
    for (let i = 1; i < spectrumDb.length; i++) {
      const hz = i * binHz;
      if (hz < 40 || hz >= 12000 || !Number.isFinite(spectrumDb[i])) continue;
      const amplitude = 10 ** (spectrumDb[i] / 20), power = amplitude * amplitude;
      if (hz < 250) bass += power;
      else if (hz < 2000) mid += power;
      else treble += power;
      amplitudes += amplitude; weighted += amplitude * hz;
    }
    const normalize = (amplitude: number) => clamp((20 * Math.log10(Math.max(1e-12, amplitude)) + response.gainDb - response.floorDb) / (-6 - response.floorDb));
    const level = normalize(rms);
    const raw: AudioFeatures = {
      level, bass: level > 0 ? normalize(Math.sqrt(bass)) : 0,
      mid: level > 0 ? normalize(Math.sqrt(mid)) : 0,
      treble: level > 0 ? normalize(Math.sqrt(treble)) : 0,
      centroid: level > 0 && amplitudes > 0 ? clamp(Math.log2(Math.max(100, weighted / amplitudes) / 100) / Math.log2(80)) : 0,
      onset: 0,
    };
    // A level transient, not a tempo or beat-phase estimator. Warm up before triggering.
    if (now - this.startedAt > 300 && level > 0.1 && level > this.slowLevel * 1.5 &&
      level - this.slowLevel > 0.08 && now - this.lastOnset > 200) {
      this.lastOnset = now; this.pulse = 1;
    } else this.pulse *= Math.exp(-elapsed / 160);
    this.slowLevel += (level - this.slowLevel) * (1 - Math.exp(-elapsed / 500));
    for (const name of AUDIO_SIGNALS) {
      if (name === 'onset') { this.smoothed.onset = this.pulse; continue; }
      const time = raw[name] > this.smoothed[name] ? 30 : Math.max(1, response.releaseMs);
      this.smoothed[name] += (raw[name] - this.smoothed[name]) * (1 - Math.exp(-elapsed / time));
    }
    return { ...this.smoothed };
  }
}

/** Capture stays local. The microphone graph is never connected to the speakers. */
export class MicrophoneAudio {
  private stopped = false;
  private stream?: MediaStream;
  private context?: AudioContext;
  private source?: MediaStreamAudioSourceNode;
  private timer?: ReturnType<typeof setInterval>;
  constructor(private frame: (features: AudioFeatures) => void, private failed: (error: Error) => void,
    private response: () => AudioResponse) {}
  async start(deviceId: string) {
    if (!globalThis.isSecureContext || !navigator.mediaDevices?.getUserMedia) {
      throw new Error('Microphone input needs HTTPS or localhost and a browser with audio capture. Open this editor on localhost when the controller uses HTTP.');
    }
    try {
      // Start/resume inside the click gesture, before awaiting the permission prompt.
      const context = this.context = new AudioContext();
      const resumed = context.resume();
      const captured = navigator.mediaDevices.getUserMedia({ audio: {
        ...(deviceId ? { deviceId: { exact: deviceId } } : {}),
        echoCancellation: false, noiseSuppression: false, autoGainControl: false,
      }, video: false });
      const [resumeResult, captureResult] = await Promise.allSettled([resumed, captured]);
      if (captureResult.status === 'fulfilled') this.stream = captureResult.value;
      if (this.stopped) { this.stop(); return; }
      if (resumeResult.status === 'rejected') throw resumeResult.reason;
      if (captureResult.status === 'rejected') throw captureResult.reason;
      const stream = captureResult.value;
      for (const track of stream.getAudioTracks()) track.addEventListener('ended', () => {
        if (!this.stopped) { this.stop(); this.failed(new Error('The audio input disconnected. Choose an input and start listening again.')); }
      }, { once: true });
      const analyser = context.createAnalyser();
      analyser.fftSize = 2048; analyser.smoothingTimeConstant = 0;
      this.source = context.createMediaStreamSource(stream); this.source.connect(analyser);
      const waveform = new Float32Array(analyser.fftSize), spectrum = new Float32Array(analyser.frequencyBinCount);
      const extractor = new AudioFeatureExtractor();
      this.timer = setInterval(() => {
        if (context.state !== 'running') { this.frame({ ...SILENCE }); return; }
        try {
          analyser.getFloatTimeDomainData(waveform); analyser.getFloatFrequencyData(spectrum);
          this.frame(extractor.extract(waveform, spectrum, context.sampleRate, performance.now(), this.response()));
        } catch (error) { this.stop(); this.failed(error as Error); }
      }, 33);
    } catch (error) { this.stop(); throw error; }
  }
  stop() {
    this.stopped = true; clearInterval(this.timer); this.source?.disconnect();
    this.stream?.getTracks().forEach(track => track.stop());
    if (this.context && this.context.state !== 'closed') void this.context.close().catch(() => {});
  }
}
