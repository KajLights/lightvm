const FLASH_START = 0x10000000;
const FIRMWARE_END = 0x1017e000;
const UF2_FAMILY = 0xe48bff56;

export function decodePicoUf2(bytes: Uint8Array): Uint8Array {
  if (!bytes.length || bytes.length % 512 !== 0) throw new Error('Invalid Pico UF2 size.');
  const count = bytes.length / 512;
  const image = new Uint8Array(count * 256);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  for (let i = 0; i < count; i++) {
    const offset = i * 512;
    if (view.getUint32(offset, true) !== 0x0a324655 ||
        view.getUint32(offset + 4, true) !== 0x9e5d5157 ||
        view.getUint32(offset + 508, true) !== 0x0ab16f30 ||
        view.getUint32(offset + 8, true) !== 0x2000 ||
        view.getUint32(offset + 28, true) !== UF2_FAMILY ||
        view.getUint32(offset + 12, true) !== FLASH_START + i * 256 ||
        view.getUint32(offset + 16, true) !== 256 ||
        view.getUint32(offset + 20, true) !== i ||
        view.getUint32(offset + 24, true) !== count ||
        FLASH_START + (i + 1) * 256 > FIRMWARE_END) {
      throw new Error(`Invalid Pico UF2 block ${i}; nothing was written.`);
    }
    image.set(bytes.subarray(offset + 32, offset + 288), i * 256);
  }
  return image;
}
