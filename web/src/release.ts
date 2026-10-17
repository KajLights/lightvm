import release from '../../lightvm/release.json';

export const VERSION = release.version;
const number = '(0|[1-9][0-9]*)';
const pattern = new RegExp(`^${number}(?:\\.${number}(?:\\.${number})?)?(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?$`);
export function parseRelease(value: unknown, shorthand = true) {
  if (typeof value !== 'string' || value.length > 95) return null;
  const match = pattern.exec(value);
  if (!match || match[0] !== value) return null;
  const [, major, minor, patch, pre, build] = match;
  if ((!shorthand || pre || build) && patch === undefined) return null;
  const core = [major, minor ?? '0', patch ?? '0'].map(Number);
  const prerelease = pre?.split('.') ?? [];
  if (core.some(part => part > 0xffffffff) || prerelease.some(part => /^0[0-9]+$/.test(part))) return null;
  return { core, prerelease };
}
function compareIdentifiers(a: string, b: string) {
  const an = /^[0-9]+$/.test(a), bn = /^[0-9]+$/.test(b);
  if (an !== bn) return an ? -1 : 1;
  if (an) return BigInt(a) < BigInt(b) ? -1 : BigInt(a) > BigInt(b) ? 1 : 0;
  return a < b ? -1 : a > b ? 1 : 0;
}
export function releaseError(version: unknown, required: unknown): string | null {
  const current = parseRelease(version, false), minimum = parseRelease(required);
  if (!current) return 'The device does not report a valid LightVM release.';
  if (!minimum) return 'This animation has no valid minimum LightVM release. Recompile its source.';
  const message = `Requires LightVM ${required} within major ${minimum.core[0]}; device runs ${version}.`;
  if (current.core[0] !== minimum.core[0]) return message;
  for (let i = 1; i < 3; i++) if (current.core[i] !== minimum.core[i]) return current.core[i] < minimum.core[i] ? message : null;
  const a = current.prerelease, b = minimum.prerelease;
  if (!a.length || !b.length) return a.length ? message : null;
  for (let i = 0; i < Math.min(a.length, b.length); i++) {
    const order = compareIdentifiers(a[i], b[i]);
    if (order) return order < 0 ? message : null;
  }
  return a.length < b.length ? message : null;
}
