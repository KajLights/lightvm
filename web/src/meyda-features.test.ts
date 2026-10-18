import Meyda from 'meyda';
import { describe, expect, it } from 'vitest';
import { DEFAULT_RESPONSE, SILENCE } from './audio-features';
import { MeydaFeatureExtractor } from './meyda-features';

const spectrum = () => new Float32Array(1024).fill(-Infinity);
const tone = (hz: number, sampleRate = 48000, amplitude = 0.1) => Float32Array.from({ length: 2048 }, (_, index) => amplitude * Math.sin(2 * Math.PI * hz * index / sampleRate));
describe('Meyda comparison processor', () => {
  it('returns finite silent controls, measurements and arrays', () => {
    const frame = new MeydaFeatureExtractor().extract(new Float32Array(2048), spectrum(), 48000, 0, DEFAULT_RESPONSE);
    expect(frame.features).toEqual(SILENCE); expect(frame.reference).toEqual(SILENCE);
    expect(Object.values(frame.measurements!)).toEqual([0, 0, 0, 0, 0, 0]);
    expect(frame.chroma).toHaveLength(12); expect(frame.chroma?.every(value => value === 0)).toBe(true);
    expect(frame.bark).toHaveLength(24); expect(frame.bark?.every(value => value === 0)).toBe(true);
  });
  it('detects known tones in bass, midrange and treble with centroid in Hz', () => {
    for (const [hz, band] of [[100, 'bass'], [1000, 'mid'], [6000, 'treble']] as const) {
      const frame = new MeydaFeatureExtractor().extract(tone(hz), spectrum(), 48000, 0, DEFAULT_RESPONSE);
      expect(frame.features[band]).toBeGreaterThan(0.2);
      for (const other of ['bass', 'mid', 'treble'] as const) if (other !== band) expect(frame.features[other]).toBeLessThan(0.01);
      expect(frame.measurements?.centroidHz).toBeGreaterThan(hz - 30);
      expect(frame.measurements?.centroidHz).toBeLessThan(hz + 30);
      expect(frame.measurements?.rolloffHz).toBeGreaterThan(hz - 50);
      expect(frame.measurements?.rolloffHz).toBeLessThan(hz + 100);
    }
  });
  it('uses unwindowed RMS for comparable levels and scales the FFT magnitude', () => {
    const frame = new MeydaFeatureExtractor().extract(tone(1000), spectrum(), 48000, 0, DEFAULT_RESPONSE);
    expect(frame.measurements?.rms).toBeCloseTo(0.1 / Math.sqrt(2), 3);
    expect(frame.features.level).toBe(frame.reference?.level);
    expect(frame.features.mid).toBeLessThan(frame.features.level);
    expect(frame.features.mid).toBeLessThan(0.6); // An unscaled FFT would clip this to full scale.
  });
  it('distinguishes noise from a tonal signal and exposes pitch-class strengths', () => {
    let seed = 99;
    const noise = Float32Array.from({ length: 2048 }, () => { seed = (Math.imul(seed, 1664525) + 1013904223) | 0; return seed / 2147483648 * 0.1; });
    const tonal = new MeydaFeatureExtractor().extract(tone(440), spectrum(), 48000, 0, DEFAULT_RESPONSE);
    const noisy = new MeydaFeatureExtractor().extract(noise, spectrum(), 48000, 0, DEFAULT_RESPONSE);
    expect(noisy.measurements!.flatness).toBeGreaterThan(tonal.measurements!.flatness + 0.5);
    expect(noisy.features.zcr).toBeGreaterThan(tonal.features.zcr);
    expect(tonal.chroma![9]).toBeGreaterThan(tonal.chroma![3]); // A has more energy than D♯.
  });
  it('keeps sample-rate configuration local and recomputes frequency conversion', () => {
    const defaults = { rate: Meyda.sampleRate, size: Meyda.bufferSize, window: Meyda.windowingFunction };
    const first = new MeydaFeatureExtractor(), second = new MeydaFeatureExtractor();
    first.extract(tone(1000, 44100), spectrum(), 44100, 0, DEFAULT_RESPONSE);
    second.extract(tone(1000, 48000), spectrum(), 48000, 0, DEFAULT_RESPONSE);
    const frame = first.extract(tone(1000, 44100), spectrum(), 44100, 33, DEFAULT_RESPONSE);
    expect(frame.measurements!.centroidHz).toBeCloseTo(1000, -1);
    expect({ rate: Meyda.sampleRate, size: Meyda.bufferSize, window: Meyda.windowingFunction }).toEqual(defaults);
  });
});
