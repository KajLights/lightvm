import { afterEach, describe, expect, it, vi } from 'vitest';
import { AudioFeatureExtractor, DEFAULT_MAPPINGS, DEFAULT_RESPONSE, MicrophoneAudio, SILENCE, mapAudioToEye } from './audio-features';

afterEach(() => { vi.unstubAllGlobals(); vi.restoreAllMocks(); vi.useRealTimers(); });
const wave = (amplitude = 0.1) => Float32Array.from({ length: 2048 }, (_, i) => amplitude * Math.sin(i * Math.PI / 12));
const spectrum = (hz: number) => { const bins = new Float32Array(1024).fill(-Infinity); bins[Math.round(hz / (48000 / 2048))] = -20; return bins; };

describe('audio features and Eye mapping', () => {
  it('handles silent and invalid FFT bins without producing NaN or phantom controls', () => {
    const extractor = new AudioFeatureExtractor();
    expect(extractor.extract(new Float32Array(2048), new Float32Array(1024).fill(-Infinity), 48000, 0, DEFAULT_RESPONSE)).toEqual(SILENCE);
  });
  it('distinguishes bass, mids and treble using the actual sample rate', () => {
    for (const [hz, band] of [[100, 'bass'], [1000, 'mid'], [6000, 'treble']] as const) {
      const features = new AudioFeatureExtractor().extract(wave(), spectrum(hz), 48000, 0, DEFAULT_RESPONSE);
      expect(features[band]).toBeGreaterThan(0.3);
      for (const other of ['bass', 'mid', 'treble'] as const) if (other !== band) expect(features[other]).toBe(0);
    }
  });
  it('normalizes centroid by frequency and decays smoothed levels in silence', () => {
    const extractor = new AudioFeatureExtractor();
    const low = extractor.extract(wave(), spectrum(100), 48000, 0, DEFAULT_RESPONSE);
    const high = extractor.extract(wave(), spectrum(6000), 48000, 33, DEFAULT_RESPONSE);
    expect(high.centroid).toBeGreaterThan(low.centroid + 0.3);
    const quiet = extractor.extract(new Float32Array(2048), new Float32Array(1024).fill(-Infinity), 48000, 1033, DEFAULT_RESPONSE);
    expect(quiet.level).toBeLessThan(0.01); expect(quiet.bass).toBeLessThan(0.01);
  });
  it('warms up, detects a transient and limits rapid retriggering', () => {
    const extractor = new AudioFeatureExtractor();
    const quiet = new Float32Array(2048), empty = new Float32Array(1024).fill(-Infinity);
    expect(extractor.extract(wave(), spectrum(100), 48000, 0, DEFAULT_RESPONSE).onset).toBe(0);
    for (let now = 33; now < 1000; now += 33) extractor.extract(quiet, empty, 48000, now, DEFAULT_RESPONSE);
    expect(extractor.extract(wave(), spectrum(100), 48000, 1000, DEFAULT_RESPONSE).onset).toBe(1);
    expect(extractor.extract(wave(), spectrum(100), 48000, 1033, DEFAULT_RESPONSE).onset).toBeLessThan(1);
  });
  it('maps all four values, supports inversion and clamps malformed signals', () => {
    const mappings = { ...DEFAULT_MAPPINGS, position: { signal: 'centroid' as const, minimum: 1, maximum: 0 } };
    const values = mapAudioToEye({ ...SILENCE, bass: 1, level: 0.5, centroid: 0.25, treble: NaN }, mappings);
    expect(values).toEqual({ position: 0.75, width: 0.4, hue: 0, brightness: 0.5 });
  });
});

describe('microphone lifecycle', () => {
  function audio() {
    vi.stubGlobal('isSecureContext', true);
    const stop = vi.fn(), connect = vi.fn(), disconnect = vi.fn(), close = vi.fn(async () => {});
    const analyser = { fftSize: 2048, frequencyBinCount: 1024, getFloatTimeDomainData: vi.fn(), getFloatFrequencyData: vi.fn() };
    const context = { state: 'running', sampleRate: 48000, resume: vi.fn(async () => {}), close,
      createAnalyser: () => analyser, createMediaStreamSource: () => ({ connect, disconnect }) };
    vi.stubGlobal('AudioContext', class { constructor() { return context; } });
    const track = { stop, addEventListener: vi.fn() };
    const stream = { getTracks: () => [track], getAudioTracks: () => [track] } as unknown as MediaStream;
    return { context, stream, stop, close, connect, disconnect };
  }
  it('disables speech processing, never routes to speakers, and closes on stop', async () => {
    vi.useFakeTimers();
    const mock = audio(), getUserMedia = vi.fn(async () => mock.stream);
    vi.stubGlobal('navigator', { mediaDevices: { getUserMedia } });
    const capture = new MicrophoneAudio(vi.fn(), vi.fn(), () => DEFAULT_RESPONSE);
    await capture.start('line-in');
    expect(getUserMedia).toHaveBeenCalledWith({ audio: { deviceId: { exact: 'line-in' }, echoCancellation: false, noiseSuppression: false, autoGainControl: false }, video: false });
    expect(mock.connect).toHaveBeenCalledTimes(1);
    capture.stop(); expect(mock.stop).toHaveBeenCalled(); expect(mock.close).toHaveBeenCalled(); expect(mock.disconnect).toHaveBeenCalled();
    expect(vi.getTimerCount()).toBe(0);
  });
  it('stops a stream granted after capture was cancelled', async () => {
    const mock = audio(); let grant!: (stream: MediaStream) => void;
    vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: () => new Promise<MediaStream>(resolve => { grant = resolve; }) } });
    const capture = new MicrophoneAudio(vi.fn(), vi.fn(), () => DEFAULT_RESPONSE);
    const pending = capture.start(''); capture.stop(); grant(mock.stream); await pending;
    expect(mock.stop).toHaveBeenCalled(); expect(mock.connect).not.toHaveBeenCalled();
  });
  it('closes the audio context if microphone access is denied', async () => {
    const mock = audio();
    vi.stubGlobal('navigator', { mediaDevices: { getUserMedia: vi.fn().mockRejectedValue(new Error('Permission denied')) } });
    await expect(new MicrophoneAudio(vi.fn(), vi.fn(), () => DEFAULT_RESPONSE).start('')).rejects.toThrow('Permission denied');
    expect(mock.close).toHaveBeenCalled();
  });
});
