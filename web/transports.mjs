const pause = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const FILTERS = [{ vendorId: 0x2e8a }, { vendorId: 0x303a, productId: 0x1001 }];

export class SerialTransport {
  constructor() { this.onData = () => {}; this.onDisconnect = () => {}; this.closing = false; }

  async open() {
    if (!navigator.serial) throw new Error("USB Serial requires desktop Chrome or Edge in a secure context.");
    this.port = await navigator.serial.requestPort({ filters: FILTERS.map(({ vendorId, productId }) => ({ usbVendorId: vendorId, ...(productId ? { usbProductId: productId } : {}) })) });
    try {
      await this.port.open({ baudRate: 115200, bufferSize: 4096 });
      const info = this.port.getInfo();
      // Web Serial cannot set signals before open; ESP32 may reset on some hosts.
      await this.port.setSignals({ dataTerminalReady: info.usbVendorId !== 0x303a, requestToSend: false });
      this.reader = this.port.readable.getReader(); this.writer = this.port.writable.getWriter();
      this.reading = this.readLoop();
      // Drop ROM boot text / stale replies while opening, before attaching a client.
      await pause(400);
      this.label = info.usbVendorId === 0x303a ? "ESP32 · USB Serial" : "Pico · USB Serial";
      return this;
    } catch (error) { await this.close(); throw error; }
  }

  async readLoop() {
    try {
      while (!this.closing) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value?.length) this.onData(value);
      }
      if (!this.closing) this.onDisconnect(new Error("USB Serial connection ended."));
    } catch (error) { if (!this.closing) this.onDisconnect(error); }
    finally { this.reader.releaseLock(); }
  }

  async write(bytes) { await this.writer.write(bytes); }

  close() {
    if (this.closePromise) return this.closePromise;
    this.closing = true;
    return this.closePromise = (async () => {
      if (this.reader) { await this.reader.cancel().catch(() => {}); await this.reading; }
      if (this.writer) { this.writer.releaseLock(); this.writer = null; }
      if (this.port?.readable) await this.port.close().catch(() => {});
    })();
  }
}

export class HttpTransport {
  constructor(url, { timeout = 5000, fetch: request = globalThis.fetch } = {}) {
    const parsed = new URL(url);
    if (!["http:", "https:"].includes(parsed.protocol) || parsed.username || parsed.password || parsed.search || parsed.hash || parsed.pathname !== "/") throw new Error("Enter a device HTTP origin, e.g. http://192.168.1.20");
    this.addressSpace = parsed.hostname === "localhost" || parsed.hostname.startsWith("127.") || parsed.hostname === "[::1]" ? "loopback" : "local";
    this.url = parsed.origin; this.timeout = timeout; this.request = request.bind(globalThis);
    this.onData = () => {}; this.onDisconnect = () => {}; this.closing = false;
    this.label = `${parsed.host} · HTTP`; this.controllers = new Set();
  }
  async exchange(path, { method = "POST", body } = {}) {
    const controller = new AbortController(); this.controllers.add(controller);
    const timer = setTimeout(() => controller.abort(), this.timeout);
    try {
      const response = await this.request(this.url + path, { method, body, signal: controller.signal,
        headers: { "Content-Type": "application/octet-stream", ...(this.session ? { "X-LightVM-Session": this.session } : {}) },
        credentials: "omit", redirect: "error", cache: "no-store", targetAddressSpace: this.addressSpace });
      if (!response.ok) throw new Error(`Device HTTP error ${response.status}. Reconnect and refresh state before retrying.`);
      return await response.arrayBuffer();
    } catch (error) {
      if (error.name === "TypeError") throw new Error("Cannot reach the device. Check its address, Wi-Fi and browser local-network permission.");
      throw error;
    } finally { clearTimeout(timer); this.controllers.delete(controller); }
  }
  async open() {
    const bytes = await this.exchange("/api/v1/sessions"); this.identity = JSON.parse(new TextDecoder().decode(bytes));
    if (!/^[0-9a-f]{32}$/.test(this.identity.session_id)) throw new Error("Invalid HTTP session response.");
    this.session = this.identity.session_id; return this;
  }
  async write(bytes) {
    if (this.closing) throw new Error("HTTP connection closed.");
    const response = new Uint8Array(await this.exchange("/api/v1/command", { body: bytes }));
    if (!this.closing) this.onData(response);
  }
  close() {
    if (this.closePromise) return this.closePromise;
    this.closing = true; for (const controller of this.controllers) controller.abort();
    return this.closePromise = (async () => {
      if (this.session) await this.exchange(`/api/v1/sessions/${this.session}`, { method: "DELETE" }).catch(() => {});
      this.session = null;
    })();
  }
}
