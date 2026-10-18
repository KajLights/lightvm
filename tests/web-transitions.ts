import fixtures from './portable_fixtures.h?raw';
import { crc32 } from '../web/protocol.mjs';
import type { Manifest, Program, Setting } from '../web/src/types';

// Use a compiler-generated transition package and replace only its manifest to
// exercise catalogue checks and setting controls without a browser compiler.
export function deployedTransition(name: string, settings: Setting[] = []) {
  const raw = fixtures.split('transition_lbc[] = {')[1].split('};')[0];
  const original = Uint8Array.from(raw.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value, 16));
  const size = new DataView(original.buffer).getUint32(76, true);
  const manifest: Manifest = JSON.parse(new TextDecoder().decode(original.subarray(original.length - size)));
  manifest.settings = settings;
  const metadata = new TextEncoder().encode(JSON.stringify(manifest));
  const bytes = new Uint8Array(original.length - size + metadata.length);
  bytes.set(original.subarray(0, original.length - size)); bytes.set(metadata, original.length - size);
  new DataView(bytes.buffer).setUint32(76, metadata.length, true);
  const program: Program = { name, size: bytes.length, crc32: crc32(bytes), type: 'transition', profile: 'transition' };
  return { bytes, program };
}

export const WIPE_SETTINGS: Setting[] = [{ name: 'reverse', type: 'bool' }, { name: 'separator', type: 'bool' }, { name: 'color', type: 'rgb8' }];
