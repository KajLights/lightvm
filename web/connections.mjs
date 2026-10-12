// A selected path is explicit. Operations capture it once and never fail over/replay.
export class Connections {
  constructor() { this.paths = new Map(); this.selected = null; }
  attach(path, client, description) {
    for (const current of this.paths.values()) {
      if (!description.device_id || description.device_id !== current.description.device_id) throw new Error("This path belongs to a different device. Disconnect the current device first.");
      if (description.boot_id !== current.description.boot_id) throw new Error("The device rebooted. Reconnect its existing path before attaching another.");
    }
    if (this.paths.has(path)) throw new Error("This path is already connected.");
    this.paths.set(path, { client, description }); this.selected = path;
  }
  detach(path) { this.paths.delete(path); if (this.selected === path) this.selected = null; }
  select(path) { if (!this.paths.has(path)) throw new Error("Select a connected command path."); this.selected = path; }
  get active() { return this.paths.get(this.selected) || null; }
}
