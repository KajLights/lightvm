import Meyda from 'meyda';
import type { MeydaFeaturesObject } from 'meyda';
import { AudioFeatureEnvelope, AudioFeatureExtractor, type AudioAnalysis, type AudioResponse } from './audio-features';

const finite = (value: number) => Number.isFinite(value) ? value : 0;
const bounded = (value: number) => Math.max(0, Math.min(1, finite(value)));

/** Meyda runs on the same 2,048 PCM samples as the native reference, without a ScriptProcessor. */
export class MeydaFeatureExtractor {
  private meyda?: typeof Meyda;
  private envelope = new AudioFeatureEnvelope();
  private native = new AudioFeatureExtractor();
  extract(waveform: Float32Array, spectrumDb: Float32Array, sampleRate: number,
    now: number, response: AudioResponse): AudioAnalysis {
    const nativeStart = performance.now();
    const reference = this.native.extract(waveform, spectrumDb, sampleRate, now, response);
    const nativeMs = performance.now() - nativeStart, meydaStart = performance.now();
    if (!this.meyda || this.meyda.sampleRate !== sampleRate || this.meyda.bufferSize !== waveform.length) {
      // extract() mutates its receiver's caches. Keep rates and buffers local to this capture.
      this.meyda = { ...Meyda, sampleRate, bufferSize: waveform.length, windowingFunction: 'blackman' };
    }
    const extracted = this.meyda.extract(['amplitudeSpectrum', 'spectralCentroid', 'spectralFlatness',
      'spectralRolloff', 'zcr', 'chroma', 'loudness'], waveform) as MeydaFeaturesObject;
    // Meyda's normal extract() windows every feature, including RMS. Use its RMS
    // extractor on raw PCM so level has the same meaning on both pages.
    const rms: number = finite(this.meyda.featureExtractors.rms({ signal: waveform }));
    const binHz = sampleRate / waveform.length;
    let bass = 0, mid = 0, treble = 0;
    for (let i = 1; i < extracted.amplitudeSpectrum.length; i++) {
      const hz = i * binHz;
      if (hz < 40 || hz >= 12000) continue;
      // Meyda exposes unscaled FFT magnitudes; Web Audio's magnitudes include 1/N.
      const power = (extracted.amplitudeSpectrum[i] / waveform.length) ** 2;
      if (hz < 250) bass += power;
      else if (hz < 2000) mid += power;
      else treble += power;
    }
    const audible = rms > 1e-8;
    const measurements = {
      rms, centroidHz: audible ? finite(extracted.spectralCentroid * binHz) : 0,
      rolloffHz: audible ? finite(extracted.spectralRolloff) : 0,
      flatness: audible ? bounded(extracted.spectralFlatness) : 0,
      crossings: audible ? finite(extracted.zcr) : 0,
      loudness: audible ? finite(extracted.loudness.total) : 0,
    };
    const features = this.envelope.process({ rms, bass: Math.sqrt(bass), mid: Math.sqrt(mid), treble: Math.sqrt(treble),
      centroidHz: measurements.centroidHz, flatness: measurements.flatness,
      rolloff: measurements.rolloffHz / (sampleRate / 2), zcr: measurements.crossings / Math.max(1, waveform.length - 1) }, now, response);
    return { features, reference, measurements,
      chroma: Array.from(extracted.chroma, value => audible ? bounded(value) : 0),
      bark: Array.from(extracted.loudness.specific, value => audible ? Math.max(0, finite(value)) : 0),
      timings: { nativeMs, meydaMs: performance.now() - meydaStart } };
  }
}
