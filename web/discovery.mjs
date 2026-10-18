export const DISCOVERY_ORIGIN = "http://lightvm.local";

function origin(value) {
  const url = new URL(value);
  if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || url.pathname !== "/" || url.search || url.hash) throw new Error("Enter a controller HTTP address.");
  return url.origin;
}

export function controllerList(payload) {
  if (payload?.schema !== "lightvm.discovery" || !Array.isArray(payload.controllers) || payload.controllers.length > 8) throw new Error("This firmware does not provide a valid controller list.");
  const controllers = new Map();
  for (const value of payload.controllers) {
    const parts = typeof value.ip === "string" ? value.ip.split(".") : [];
    if (parts.length !== 4 || parts.some(part => !/^\d{1,3}$/.test(part) || Number(part) > 255) || Number(parts[0]) === 0 || Number(parts[0]) >= 224 ||
        !Number.isInteger(value.port) || value.port < 1 || value.port > 65535 ||
        typeof value.controller_id !== "string" || !/^[a-z0-9_-]{1,63}$/i.test(value.controller_id) ||
        typeof value.board !== "string" || !/^[a-z0-9_-]{1,31}$/i.test(value.board) ||
        typeof value.hostname !== "string" || !/^lightvm-[a-z0-9_-]{1,23}\.local$/i.test(value.hostname)) throw new Error("This firmware returned an invalid controller address.");
    // Build the origin from validated address fields, never a supplied URL.
    const ip = parts.map(Number).join(".");
    if (!controllers.has(value.controller_id)) controllers.set(value.controller_id, { ...value, ip, url: new URL(`http://${ip}:${value.port}`).origin });
  }
  return { controllers: [...controllers.values()], truncated: Boolean(payload.truncated) };
}

export class ControllerDiscovery {
  constructor({ fetch: request = globalThis.fetch, timeout = 1800 } = {}) {
    this.request = request.bind(globalThis); this.timeout = timeout; this.seed = "";
  }
  async read(address) {
    const url = origin(address), controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeout);
    const hostname = new URL(url).hostname;
    try {
      const response = await this.request(`${url}/api/v1/controllers`, { method: "GET", signal: controller.signal,
        credentials: "omit", redirect: "error", cache: "no-store",
        targetAddressSpace: hostname === "localhost" || hostname.startsWith("127.") || hostname === "[::1]" ? "loopback" : "local" });
      if (!response.ok) throw new Error(`Discovery HTTP error ${response.status}.`);
      const text = await response.text();
      if (text.length > 8192) throw new Error("Controller list exceeds the discovery limit.");
      const result = controllerList(JSON.parse(text));
      // The first entry is the bridge itself. Pin future reads to its IP so the
      // shared bootstrap name cannot switch controllers between HTTP requests.
      this.seed = result.controllers[0]?.url || url;
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
    throw new Error("No controllers found. Check Wi-Fi and browser local-network permission, or connect by USB or address.");
  }
}
