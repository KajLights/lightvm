// Shared browser checks for release consumers; no device writes or UI policy.
export function assessCandidate(device, artifact, requiredFeatures, method, supportedTools = []) {
  const build = artifact.build, reasons = [], hardware = device.hardware ?? {};
  for (const key of ['product', 'board']) if (device[key] !== build[key]) reasons.push(`wrong_${key}`);
  for (const key of ['variant', 'chip']) if ((hardware[key] ?? null) !== (build.hardware[key] ?? null)) reasons.push(`wrong_${key}`);
  if (requiredFeatures.some(feature => !build.features.includes(feature))) reasons.push('missing_features');
  if (!artifact.installation.methods.includes(method)) reasons.push('unsupported_method');
  if (reasons.length) return { state: 'incompatible', reasons };
  if (!device.controller_id || !device.boot_id) return { state: 'cannot_assess', reasons: ['device_identity_required'] };
  if (hardware.flash_verified !== true || !Number.isSafeInteger(hardware.flash_bytes) || hardware.flash_bytes <= 0) return { state: 'cannot_assess', reasons: ['physical_flash_probe_required'] };
  if (hardware.flash_bytes !== build.hardware.declared_flash_bytes) return { state: 'incompatible', reasons: ['wrong_flash_geometry'] };
  const revision = artifact.installation.chip_revision;
  if (revision && (revision.minimum || revision.maximum !== null)) {
    if (!Number.isSafeInteger(hardware.chip_revision)) return { state: 'cannot_assess', reasons: ['chip_revision_probe_required'] };
    if (hardware.chip_revision < revision.minimum || revision.maximum !== null && hardware.chip_revision > revision.maximum) return { state: 'incompatible', reasons: ['wrong_chip_revision'] };
  }
  if (!supportedTools.includes(artifact.tools.contract)) return { state: 'matching_tools_required', reasons: ['tool_contract_unavailable'] };
  return device.firmware?.build_id === build.firmware.build_id
    ? { state: 'installed', reasons: [] }
    : { state: 'clean_local_install', reasons: ['export_and_clean_install_required'] };
}

export function decodeUf2(bytes, firmwareEnd, family) {
  const start = 0x10000000;
  if (!(bytes instanceof Uint8Array) || !bytes.length || bytes.length % 512 || ![0xe48bff56, 0xe48bff59].includes(family) || !Number.isSafeInteger(firmwareEnd) || firmwareEnd % 4096 || firmwareEnd <= start || firmwareEnd > start + 16 * 1024 * 1024) throw Error('Invalid UF2 geometry.');
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let first = 0;
  if (family === 0xe48bff59 && view.getUint32(28, true) === 0xe48bff57) {
    const words = [0x0a324655, 0x9e5d5157, 0xa000, 0x10ffff00, 256, 0, 2, 0xe48bff57];
    if (words.some((word, i) => view.getUint32(i * 4, true) !== word) || bytes.subarray(32, 288).some(b => b !== 0xef) || view.getUint32(288, true) !== 0x9957e304 || bytes.subarray(292, 508).some(b => b !== 0) || view.getUint32(508, true) !== 0x0ab16f30) throw Error('Invalid RP2350 workaround block.');
    first = 1;
  }
  const count = bytes.length / 512 - first;
  if (count < 1 || count * 256 > firmwareEnd - start) throw Error('UF2 exceeds its firmware region.');
  const image = new Uint8Array(count * 256);
  for (let i = 0; i < count; i++) {
    const offset = (i + first) * 512;
    const expected = [0x0a324655, 0x9e5d5157, 0x2000, start + i * 256, 256, i, count, family];
    if (expected.some((word, n) => view.getUint32(offset + n * 4, true) !== word) || view.getUint32(offset + 508, true) !== 0x0ab16f30) throw Error(`Invalid UF2 block ${i}.`);
    image.set(bytes.subarray(offset + 32, offset + 288), i * 256);
  }
  return image;
}

export async function verifiedPayload(url, asset, request = globalThis.fetch) {
  const parsed = new URL(url, globalThis.location?.href);
  if (globalThis.location && parsed.origin !== globalThis.location.origin) throw Error('Firmware must come from the verified website mirror.');
  if (!Number.isSafeInteger(asset.bytes) || asset.bytes <= 0 || asset.bytes > 32 * 1024 * 1024 || !/^[0-9a-f]{64}$/.test(asset.sha256)) throw Error('Invalid firmware asset record.');
  const response = await request(parsed.href, { cache: 'no-store', credentials: 'omit', redirect: 'error' });
  if (!response.ok || !response.body) throw Error('Firmware download failed.');
  const reader = response.body.getReader(), chunks = []; let size = 0;
  try {
    while (true) {
      const { value, done } = await reader.read(); if (done) break;
      size += value.byteLength;
      if (size > asset.bytes) throw Error('Firmware download exceeds its signed size.');
      chunks.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  if (size !== asset.bytes) throw Error('Firmware download is incomplete.');
  const bytes = new Uint8Array(size); let at = 0;
  for (const chunk of chunks) { bytes.set(chunk, at); at += chunk.length; }
  const hash = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), n => n.toString(16).padStart(2, '0')).join('');
  if (hash !== asset.sha256) throw Error('Firmware checksum failed; nothing was written.');
  return bytes;
}
