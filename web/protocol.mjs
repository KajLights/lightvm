// LightVM protocol v1. Compatible with tools/client.py and src/protocol.c.
export const COMMAND = { STATUS: 1, BEGIN: 2, CHUNK: 3, FINISH: 4, PLAY: 7, STOP: 8, MANAGE: 11 };
const OUTPUT = 128, RESULT = 129, PIXELS = 130, DATA = 131;
const MAGIC = new Uint8Array([87, 65, 83, 77]);
const encoder = new TextEncoder(), decoder = new TextDecoder();
export const FRAME_LIMIT = 512;

export function crc32(bytes, previous = 0) {
  let crc = (previous ^ 0xffffffff) >>> 0;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function join(parts) {
  const result = new Uint8Array(parts.reduce((size, part) => size + part.length, 0));
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

export function frame(kind, id, payload = new Uint8Array()) {
  if (payload.length > FRAME_LIMIT) throw new Error("Payload exceeds the 512-byte frame limit.");
  const bytes = new Uint8Array(16 + payload.length), view = new DataView(bytes.buffer);
  bytes.set(MAGIC);
  view.setUint8(4, 1); view.setUint8(5, kind);
  view.setUint16(6, id, true); view.setUint32(8, payload.length, true);
  bytes.set(payload, 16);
  view.setUint32(12, crc32(payload, crc32(bytes.subarray(4, 12))), true);
  return bytes;
}

export class FrameDecoder {
  constructor(receive) { this.receive = receive; this.buffer = new Uint8Array(); }
  feed(bytes) {
    this.buffer = join([this.buffer, bytes]);
    while (this.buffer.length >= 4) {
      const index = this.buffer.findIndex((_, i) => MAGIC.every((byte, j) => this.buffer[i + j] === byte));
      if (index < 0) { this.buffer = this.buffer.slice(-3); return; }
      this.buffer = this.buffer.subarray(index);
      if (this.buffer.length < 16) return;
      const view = new DataView(this.buffer.buffer, this.buffer.byteOffset, this.buffer.byteLength);
      const size = view.getUint32(8, true);
      if (view.getUint8(4) !== 1 || size > FRAME_LIMIT) throw new Error("Invalid device response header.");
      if (this.buffer.length < 16 + size) return;
      const payload = this.buffer.slice(16, 16 + size);
      if (view.getUint32(12, true) !== crc32(payload, crc32(this.buffer.subarray(4, 12)))) {
        throw new Error("Device response checksum mismatch.");
      }
      const kind = view.getUint8(5), id = view.getUint16(6, true);
      this.buffer = this.buffer.slice(16 + size);
      this.receive({ kind, id, payload });
    }
  }
}

export class LightVMClient {
  constructor(transport, { timeout = 5000, onOutput = () => {}, onDisconnect = () => {} } = {}) {
    this.transport = transport; this.timeout = timeout; this.onOutput = onOutput;
    this.onDisconnect = onDisconnect; this.requestId = 0; this.pending = null;
    this.closed = false; this.queue = Promise.resolve();
    this.decoder = new FrameDecoder(message => this.receive(message));
    transport.onData = bytes => {
      try { this.decoder.feed(bytes); } catch (error) { this.fail(error); }
    };
    transport.onDisconnect = error => this.fail(error || new Error("Device disconnected."));
  }

  fail(error) {
    if (this.closed) return;
    this.closed = true;
    if (this.pending) {
      clearTimeout(this.pending.timer); this.pending.reject(error); this.pending = null;
    }
    void this.transport.close().catch(() => {});
    this.onDisconnect(error);
  }

  receive({ kind, id, payload }) {
    const pending = this.pending;
    // Boot messages and replies left by a previous connection have no owner here.
    if (!pending || id !== pending.id) return;
    if (kind === OUTPUT) { this.onOutput(decoder.decode(payload)); return; }
    if (kind === DATA) { pending.data.push(payload); return; }
    if (kind === PIXELS) { pending.pixels.push(payload); return; }
    if (kind !== RESULT || payload.length < 8) throw new Error("Invalid device response type.");
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    const code = view.getUint32(0, true), value = view.getInt32(4, true);
    const message = decoder.decode(payload.subarray(8));
    clearTimeout(pending.timer); this.pending = null;
    if (code) pending.reject(new Error(`Device error ${code}: ${message}`));
    else pending.resolve({ code, value, message, data: join(pending.data), pixels: join(pending.pixels) });
  }

  command(kind, payload = new Uint8Array()) {
    const operation = this.queue.then(() => {
      if (this.closed) throw new Error("Reconnect the device before sending another command.");
      this.requestId = this.requestId % 65535 + 1;
      const id = this.requestId;
      const bytes = frame(kind, id, payload);
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => this.fail(new Error("Device response timed out. Reconnect and try again.")), this.timeout);
        this.pending = { id, resolve, reject, timer, data: [], pixels: [] };
        Promise.resolve().then(() => this.transport.write(bytes)).catch(error => this.fail(error));
      });
    });
    this.queue = operation.catch(() => {});
    return operation;
  }

  async status() {
    const reply = await this.command(COMMAND.STATUS);
    return Object.fromEntries(reply.message.split(/\s+/).filter(item => item.includes("=")).map(item => {
      const index = item.indexOf("="); return [item.slice(0, index), item.slice(index + 1)];
    }));
  }

  async manage(op, fields = {}) {
    return this.command(COMMAND.MANAGE, encoder.encode(JSON.stringify({ op, ...fields })));
  }

  async describe() { return JSON.parse(decoder.decode((await this.manage("describe")).data)); }

  async upload(wasm, limit, onProgress = () => {}) {
    if (!Number.isInteger(limit) || limit < 8 || wasm.length < 8 || wasm.length > limit) {
      throw new Error(`Program must fit the device's ${limit}-byte module limit.`);
    }
    const metadata = new Uint8Array(8), view = new DataView(metadata.buffer);
    view.setUint32(0, wasm.length, true); view.setUint32(4, crc32(wasm), true);
    await this.command(COMMAND.BEGIN, metadata);
    for (let offset = 0; offset < wasm.length; offset += FRAME_LIMIT - 4) {
      const part = wasm.subarray(offset, offset + FRAME_LIMIT - 4), payload = new Uint8Array(4 + part.length);
      new DataView(payload.buffer).setUint32(0, offset, true); payload.set(part, 4);
      const reply = await this.command(COMMAND.CHUNK, payload);
      if (reply.value !== offset + part.length) {
        const error = new Error("Device acknowledged an unexpected upload offset.");
        this.fail(error); throw error;
      }
      onProgress((offset + part.length) / wasm.length);
    }
    await this.command(COMMAND.FINISH);
  }

  async close() {
    this.fail(new Error("Connection closed."));
    await this.transport.close();
  }
}
