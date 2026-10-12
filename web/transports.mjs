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
