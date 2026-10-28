import type { AnalysisFrame, AudioManifest, AudioParameters } from './audio-features';
import { audioNow } from './audio-wire';
export const parameters: AudioParameters = { gain_db: 0, floor_db: -50, attack_ms: 30, release_ms: 220, band_count: 16, onset_threshold: 0.08, onset_refractory_ms: 200 };
export const manifest: AudioManifest = { abi: 1, revision: 'fixture-core', dependency: 'fixture-revision', geometry: { sample_rate: 48000, window: 2048, hop: 1024, publish_hops: 5 },
  parameters: Object.fromEntries(Object.entries(parameters).map(([key, value]) => [key, { default: value, min: -100, max: 2000 }])) as AudioManifest['parameters'],
  profiles: [{ id: 1, label: 'Adaptive percussion v1', detector: '' }, { id: 2, label: 'Level rise baseline v1', detector: '' }] };
export function analysisFrame(sequence = 1n, level = 0.7, profile = 1): AnalysisFrame {
  return { profile, epoch: 1, hop: Number(sequence) * 5, sampleEnd: Number(sequence) * 5120, novelty: 0.2, threshold: 0.1,
    source: BigInt(profile + 7), session: 9n, sequence, audioUs: audioNow(), synthetic: false,
    values: [level, 0.3, 0, 0, 0, 0], features: { level, bass: 0.3, mid: 0, treble: 0, centroid: 0, onset: 0 }, bands: [], edges: [0], events: [] };
}
