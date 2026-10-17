// LightVM protocol v1. Compatible with tools/client.py and src/protocol.c.
export const COMMAND = { STATUS: 1, BEGIN: 2, CHUNK: 3, FINISH: 4, PLAY: 7, STOP: 8, MANAGE: 11, OBJECT_BEGIN: 12, OBJECT_CHUNK: 13, OBJECT_FINISH: 14, OBJECT_CANCEL: 15 };
const RESULT = 129, PIXELS = 130, DATA = 131;
const MAGIC = new Uint8Array([76, 86, 77, 49]);
const encoder = new TextEncoder(), decoder = new TextDecoder();
export const FRAME_LIMIT = 512;

// Preserve integer telemetry beyond JavaScript Number precision.
export function parseJSON(text) {
  const integers = [];
  const protectedText = text.replace(/"(?:\\.|[^"\\])*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g, token => {
    if (token.startsWith('"') || !/^-?\d+$/.test(token) || Number.isSafeInteger(Number(token))) return token;
    const index = integers.push(BigInt(token)) - 1;
    return JSON.stringify(`\u0000lightvm.integer.${index}`);
  });
  return JSON.parse(protectedText, (_, value) => {
    if (typeof value === "string" && value.startsWith("\u0000lightvm.integer.")) return integers[Number(value.slice(17))];
    return value;
  });
}
export function encodeJSON(value) {
  if (typeof value === "bigint") return value.toString();
  if (Array.isArray(value)) return `[${value.map(encodeJSON).join(",")}]`;
  if (value && typeof value === "object") return `{${Object.entries(value).filter(([, item]) => item !== undefined).map(([key, item]) => `${JSON.stringify(key)}:${encodeJSON(item)}`).join(",")}}`;
  if (typeof value === "number" && !Number.isFinite(value)) throw new Error("JSON numbers must be finite.");
  return JSON.stringify(value);
}

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
  /** @param {any} transport
   *  @param {{ timeout?: number, onDisconnect?: (error: Error) => void }} [options] */
  constructor(transport, { timeout = 5000, onDisconnect = () => {} } = {}) {
    this.transport = transport; this.timeout = timeout;
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
    if (kind === DATA) { pending.data.push(payload); return; }
    if (kind === PIXELS) { pending.pixels.push(payload); return; }
    if (kind !== RESULT || payload.length < 8) throw new Error("Invalid device response type.");
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    const code = view.getUint32(0, true), value = view.getInt32(4, true);
    const message = decoder.decode(payload.subarray(8));
    clearTimeout(pending.timer); this.pending = null;
    if (code) {
      const error = new Error(`Device error ${code}: ${message}`);
      error.code = code;
      let details;
      try { details = JSON.parse(decoder.decode(join(pending.data))); } catch {}
      error.conflict = details?.error === "stale_revision" || /\bstale\b/.test(message);
      pending.reject(error);
    }
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
    return this.command(COMMAND.MANAGE, encoder.encode(encodeJSON({ op, ...fields })));
  }

  async describe() { const description = JSON.parse(decoder.decode((await this.manage("describe")).data)); this.connectionsSupported = description.connections; return description; }

  /** @param {Uint8Array} bytecode @param {number} limit @param {(progress: number) => void} [onProgress] @param {string} [device] @param {Record<string, number | boolean>} [settings] */
  async upload(bytecode, limit, onProgress = () => {}, device = '', settings = {}) {
    if (!Number.isInteger(limit) || limit < 8 || bytecode.length < 8 || bytecode.length > limit) {
      throw new Error(`Program must fit the device's ${limit}-byte module limit.`);
    }
    if (device && !/^[A-Za-z0-9_.-]{1,32}$/.test(device)) throw new Error('Invalid output id.');
    const argumentsBytes = Object.keys(settings).length ? encoder.encode(encodeJSON({ settings })) : new Uint8Array();
    if (argumentsBytes.length > FRAME_LIMIT) throw new Error('Preview settings exceed the command size limit.');
    const output = encoder.encode(device);
    const metadata = new Uint8Array(8 + output.length), view = new DataView(metadata.buffer);
    view.setUint32(0, bytecode.length, true); view.setUint32(4, crc32(bytecode), true);
    metadata.set(output, 8);
    let started = false;
    try {
      await this.command(COMMAND.BEGIN, metadata);
      started = true;
      for (let offset = 0; offset < bytecode.length; offset += FRAME_LIMIT - 4) {
        const part = bytecode.subarray(offset, offset + FRAME_LIMIT - 4), payload = new Uint8Array(4 + part.length);
        new DataView(payload.buffer).setUint32(0, offset, true); payload.set(part, 4);
        const reply = await this.command(COMMAND.CHUNK, payload);
        if (reply.value !== offset + part.length) {
          const error = new Error("Device acknowledged an unexpected upload offset.");
          this.fail(error); throw error;
        }
        onProgress((offset + part.length) / bytecode.length);
      }
      await this.command(COMMAND.FINISH, argumentsBytes);
    } catch (error) {
      if (started && !this.closed) await this.command(COMMAND.OBJECT_CANCEL).catch(() => {});
      throw error;
    }
  }

  async json(op, fields = {}) { return parseJSON(decoder.decode((await this.manage(op, fields)).data)); }

  async uploadObject(kind, data, name) {
    const limits = await this.describe(), limit = kind === "procedure" ? limits.module_limit : limits.config_limit;
    if (data.length < (kind === "procedure" ? 8 : 1) || data.length > limit) throw new Error(`Object exceeds the ${limit}-byte limit.`);
    await this.command(COMMAND.OBJECT_BEGIN, encoder.encode(JSON.stringify({ kind, size: data.length, crc32: crc32(data), ...(name ? { name } : {}) })));
    for (let offset = 0; offset < data.length; offset += FRAME_LIMIT - 4) {
      const part = data.subarray(offset, offset + FRAME_LIMIT - 4), payload = new Uint8Array(part.length + 4);
      new DataView(payload.buffer).setUint32(0, offset, true); payload.set(part, 4);
      const reply = await this.command(COMMAND.OBJECT_CHUNK, payload);
      if (reply.value !== offset + part.length) { this.fail(new Error("Unexpected upload offset.")); throw new Error("Unexpected upload offset."); }
    }
    await this.command(COMMAND.OBJECT_FINISH);
  }

  /** @param {any} config
   *  @param {{ save?: boolean, validate?: boolean, revisions?: { active_revision: number, saved_revision: number } | null }} [options] */
  async configure(config, { save = false, validate = false, revisions = null } = {}) {
    const state = revisions || await this.describe();
    await this.uploadObject("config", encoder.encode(JSON.stringify(config)));
    await this.manage("config_validate");
    if (validate) { await this.command(COMMAND.OBJECT_CANCEL); return; }
    const applied = await this.manage("config_apply", { expected_revision: state.active_revision });
    if (save) {
      await this.manage("config_save", { expected_revision: applied.value >>> 0, expected_saved_revision: state.saved_revision });
    }
  }

  /** @param {any} config @param {number | null} [revision] */
  async configureNetwork(config, revision = null) {
    const state = revision === null ? await this.describe() : { network_revision: revision };
    await this.uploadObject("network", encoder.encode(JSON.stringify(config)));
    await this.manage("network_validate");
    return this.manage("network_commit", { expected_revision: state.network_revision });
  }

  async saveProcedure(name, bytecode) {
    const state = await this.describe();
    try {
      await this.uploadObject("procedure", bytecode, name);
      return await this.manage("procedure_save", { name, expected_revision: state.catalog_revision });
    } catch (error) { await this.command(COMMAND.OBJECT_CANCEL).catch(() => {}); throw error; }
  }

  async configureScene(layers, device) {
    const state = await this.json('scene_get', { device });
    try {
      await this.uploadObject('scene', encoder.encode(JSON.stringify({ schema: 'lightvm.scene', layers })));
      return await this.manage('scene_apply', { device, expected_revision: state.revision });
    } catch (error) { await this.command(COMMAND.OBJECT_CANCEL).catch(() => {}); throw error; }
  }

  async savePreset(name, layers, device) {
    const state = await this.describe();
    try {
      await this.uploadObject('preset', encoder.encode(JSON.stringify({ schema: 'lightvm.preset', layers })), name);
      return await this.manage('preset_save', { name, device, expected_revision: state.catalog_revision });
    } catch (error) { await this.command(COMMAND.OBJECT_CANCEL).catch(() => {}); throw error; }
  }

  async activatePreset(name, device) {
    const state = await this.json('scene_get', { device });
    return this.manage('preset_activate', { name, device, expected_revision: state.revision });
  }

  async deletePreset(name) {
    const state = await this.describe();
    return this.manage('preset_delete', { name, expected_revision: state.catalog_revision });
  }

  async deleteProcedure(name) {
    const state = await this.describe(); return this.manage("procedure_delete", { name, expected_revision: state.catalog_revision });
  }



  async close() {
    if (!this.closed && !this.pending && this.connectionsSupported) await this.manage("session_close").catch(() => {});
    this.fail(new Error("Connection closed."));
    await this.transport.close();
  }
}
