/** LVAF v5 feature record. Capture times and occurrence identities stay lossless.
 * The gateway uses neutral lease 1; its boot/generation belongs to the envelope. */
export interface AudioOccurrence { id: bigint; timeUs: bigint; strength: number }
export interface AudioRecord {
  source: bigint; session: bigint; sequence: bigint; audioUs: bigint;
  synthetic: boolean; values: ArrayLike<number>; bands: ArrayLike<number>;
  edges: ArrayLike<number>; events: readonly AudioOccurrence[];
}
const u64 = (n: bigint) => typeof n === 'bigint' && n > 0n && n < 1n << 64n;
const unit = (n: number) => Number.isFinite(n) && n >= 0 && n <= 1;
export const AUDIO_EVENT_TTL_US = 350000n;
export const audioNow = () => BigInt(Math.max(1, Math.round(performance.now() * 1000)));
export const audioHex = (n: bigint) => {
  if (!u64(n)) throw new Error('Audio identity must be a nonzero uint64.');
  return n.toString(16).padStart(16, '0');
};
export function audioRandom() {
  const words = new Uint32Array(2);
  do { crypto.getRandomValues(words); } while (!words[0] && !words[1]);
  return BigInt(words[0]) << 32n | BigInt(words[1]);
}
export function encodeAudioRecord(f: AudioRecord, sendUs: bigint): Uint8Array {
  if (![f.source, f.session, f.sequence, f.audioUs, sendUs].every(u64) || sendUs < f.audioUs ||
      f.values.length !== 6 || f.bands.length > 64 || f.events.length > 4 ||
      f.edges.length !== f.bands.length + 1 ||
      !Array.from(f.values).every(unit) || !Array.from(f.bands).every(unit)) throw new Error('Invalid audio feature record.');
  if (f.bands.length && (f.edges[0] !== 2 || f.edges[f.bands.length] !== 512)) throw new Error('Invalid audio spectrum boundaries.');
  for (let i = 0; i < f.bands.length; i++) {
    if (!Number.isInteger(f.edges[i]) || !Number.isInteger(f.edges[i + 1]) || f.edges[i] >= f.edges[i + 1]) throw new Error('Invalid audio spectrum interval.');
  }
  for (let i = 0; i < f.events.length; i++) {
    const e = f.events[i], previous = f.events[i - 1];
    if (!u64(e.id) || !u64(e.timeUs) || e.timeUs > f.audioUs || sendUs - e.timeUs > AUDIO_EVENT_TTL_US ||
        !unit(e.strength) || !e.strength || previous && (e.id <= previous.id || e.timeUs < previous.timeUs)) throw new Error('Invalid audio occurrence.');
  }
  const bytes = new Uint8Array(92 + f.bands.length * 8 + f.events.length * 24), view = new DataView(bytes.buffer);
  bytes.set([76, 86, 65, 70, 5, 6, f.events.length, f.synthetic ? 1 : 0]);
  [f.source, f.session, f.sequence, f.audioUs, sendUs, 1n].forEach((n, i) => view.setBigUint64(8 + i * 8, n));
  view.setUint32(56, 48000); view.setUint16(60, 1024); view.setUint16(62, 2048);
  for (let i = 0; i < 6; i++) view.setFloat32(64 + i * 4, f.values[i]);
  bytes[88] = f.bands.length; bytes[89] = 1;
  for (let i = 0; i < f.bands.length; i++) {
    view.setUint16(92 + i * 8, f.edges[i]); view.setUint16(94 + i * 8, f.edges[i + 1]); view.setFloat32(96 + i * 8, f.bands[i]);
  }
  f.events.forEach((e, i) => {
    const offset = 92 + f.bands.length * 8 + i * 24;
    view.setBigUint64(offset, e.id); view.setBigUint64(offset + 8, e.timeUs); bytes[offset + 16] = 1; view.setFloat32(offset + 20, e.strength);
  });
  return bytes;
}
