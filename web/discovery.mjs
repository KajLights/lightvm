export const DISCOVERY_ORIGIN = "http://lightvm.local";

function origin(value) {
  const url = new URL(value);
  if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || url.pathname !== "/" || url.search || url.hash) throw new Error("Enter a device HTTP address.");
  return url.origin;
}

export function deviceList(payload) {
  if (payload?.schema !== "lightvm.discovery/1" || !Array.isArray(payload.devices) || payload.devices.length > 8) throw new Error("This firmware does not provide a valid device list.");
  const devices = new Map();
  for (const value of payload.devices) {
    const parts = typeof value.ip === "string" ? value.ip.split(".") : [];
    if (parts.length !== 4 || parts.some(part => !/^\d{1,3}$/.test(part) || Number(part) > 255) || Number(parts[0]) === 0 || Number(parts[0]) >= 224 ||
        !Number.isInteger(value.port) || value.port < 1 || value.port > 65535 ||
        typeof value.device_id !== "string" || !/^[a-z0-9_-]{1,63}$/i.test(value.device_id) ||
        typeof value.board !== "string" || !/^[a-z0-9_-]{1,31}$/i.test(value.board) ||
        typeof value.hostname !== "string" || !/^lightvm-[a-z0-9_-]{1,23}\.local$/i.test(value.hostname)) throw new Error("This firmware returned an invalid device address.");
    // Build the origin from validated address fields, never a supplied URL.
    const ip = parts.map(Number).join(".");
    if (!devices.has(value.device_id)) devices.set(value.device_id, { ...value, ip, url: new URL(`http://${ip}:${value.port}`).origin });
  }
  return { devices: [...devices.values()], truncated: Boolean(payload.truncated) };
}

export class DeviceDiscovery {
  constructor({ fetch: request = globalThis.fetch, timeout = 1800 } = {}) {
    this.request = request.bind(globalThis); this.timeout = timeout; this.seed = "";
  }
  async read(address) {
    const url = origin(address), controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeout);
    const hostname = new URL(url).hostname;
    try {
      const response = await this.request(`${url}/api/v1/devices`, { method: "GET", signal: controller.signal,
        credentials: "omit", redirect: "error", cache: "no-store",
        targetAddressSpace: hostname === "localhost" || hostname.startsWith("127.") || hostname === "[::1]" ? "loopback" : "local" });
      if (!response.ok) throw new Error(`Discovery HTTP error ${response.status}.`);
      const text = await response.text();
      if (text.length > 8192) throw new Error("Device list exceeds the discovery limit.");
      const result = deviceList(JSON.parse(text));
      // The first entry is the bridge itself. Pin future reads to its IP so the
      // shared bootstrap name cannot switch devices between HTTP requests.
      this.seed = result.devices[0]?.url || url;
      return result;
    } finally { clearTimeout(timer); }
  }
  async find(seeds = []) {
    const candidates = [];
    for (const value of [...seeds, this.seed, DISCOVERY_ORIGIN]) {
      if (!value) continue;
      try { const address = origin(value); if (!candidates.includes(address)) candidates.push(address); } catch { /* Ignore a partially typed address. */ }
    }
    for (const address of candidates) {
      try { return await this.read(address); } catch { /* Try the next known origin, never scan address ranges. */ }
    }
    this.seed = "";
    throw new Error("No devices found. Check Wi-Fi and browser local-network permission, or connect by USB or address.");
  }
}
