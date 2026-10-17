import { LightVMClient, COMMAND, parseJSON } from '../protocol.mjs';
import { HttpTransport, SerialTransport } from '../transports.mjs';
import { Connections } from '../connections.mjs';
import { DeviceDiscovery } from '../discovery.mjs';
import { activityActions, deviceActions, editorActions, type AppStore } from './store';
import type { Description, HardwareConfig, NetworkConfig, Path } from './types';
import type { SettingsValues } from './library';

export function download(bytes: BlobPart, filename: string, type: string) {
  const url = URL.createObjectURL(new Blob([bytes], { type }));
  const anchor = document.createElement('a'); anchor.href = url; anchor.download = filename; anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function serializable(value: unknown): any {
  if (typeof value === 'bigint') return value.toString();
  if (Array.isArray(value)) return value.map(serializable);
  if (value && typeof value === 'object') return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, serializable(item)]));
  return value;
}
export class DeviceService {
  readonly paths = new Connections();
  readonly discovery = new DeviceDiscovery();
  private timer?: ReturnType<typeof setInterval>;
  private discoveryTimer?: ReturnType<typeof setInterval>;
  private polling = false;
  private finding = false;
  private stopping = false;
  constructor(private store: AppStore) {}
  get snapshot() { return this.store.getState(); }
  get client(): LightVMClient | null { return this.paths.active?.client || null; }
  log(text: string, kind = '') { this.store.dispatch(activityActions.log({ text, kind })); }
  syncPaths(description?: Description) {
    this.store.dispatch(deviceActions.connections({
      paths: [...this.paths.paths].map(([key, value]) => ({ key, label: value.client.transport.label })),
      selectedPath: this.paths.selected, description,
    }));
  }
  start() {
    this.stopping = false; void this.find();
    this.discoveryTimer = setInterval(() => { if (!document.hidden && !this.store.getState().activity.operation) void this.find(); }, 10000);
    this.timer = setInterval(() => { if (!document.hidden) void this.poll(); }, 5000);
  }
  async stop() {
    this.stopping = true; clearInterval(this.timer); clearInterval(this.discoveryTimer);
    for (const { client } of this.paths.paths.values()) await client.close();
  }
  async run(label: string, operation: (client: LightVMClient | null) => Promise<void>): Promise<boolean> {
    if (this.store.getState().activity.operation) return false;
    const client = this.client; // Pin all commands in an operation to one path.
    this.store.dispatch(activityActions.operation(label)); this.store.dispatch(activityActions.notice(''));
    try { await operation(client); return true; }
    catch (error) {
      const problem = error as Error & { conflict?: boolean };
      if (problem.name === 'NotFoundError') { this.log('Device selection cancelled.'); return false; }
      let refreshed = false;
      if (problem.conflict && client && !client.closed) {
        try { await this.refresh(client); refreshed = true; } catch { /* Preserve the original error. */ }
      }
      const message = problem.message + (refreshed ? ' Device state refreshed. Review it before trying again.' : '');
      this.store.dispatch(activityActions.notice(message)); this.log(problem.message, 'error'); return false;
    } finally { this.store.dispatch(activityActions.operation(null)); this.store.dispatch(activityActions.progress(null)); }
  }
  require(client: LightVMClient | null): LightVMClient {
    if (!client || client.closed) throw new Error('Select a connected command path before sending commands.');
    return client;
  }
  async connect(key: Path, address = '') {
    return this.run('Connecting', async () => {
      if (key === 'http' && new URL(address).hostname === 'lightvm.local') {
        const found = await this.discovery.read(address);
        const selected = this.store.getState().device.description;
        const device = this.paths.paths.size && selected ? found.devices.find(item => item.device_id === selected.device_id) : found.devices[0];
        if (!device) throw new Error('Select a discovered device or enter its direct address.');
        address = device.url;
      }
      const transport = key === 'usb' ? new SerialTransport() : new HttpTransport(address);
      let candidate: LightVMClient | undefined;
      try {
        await transport.open();
        candidate = new LightVMClient(transport, {
          onDisconnect: (error: Error) => {
            if (this.paths.paths.get(key)?.client !== candidate) return;
            const selected = this.paths.selected === key;
            this.paths.detach(key); this.syncPaths();
            if (selected) {
              this.store.dispatch(deviceActions.programState(null));
              this.store.dispatch(editorActions.targetChanged());
            }
            if (error?.message !== 'Connection closed.' && !this.stopping) {
              this.store.dispatch(activityActions.notice(error.message)); this.log(error.message, 'error');
            }
          },
        });
        const description = await candidate.describe() as Description;
        if (!description.device_id || !description.boot_id) throw new Error('This firmware cannot identify its device and boot. Install current LightVM firmware.');
        this.paths.attach(key, candidate, description); this.syncPaths(description);
        await this.refresh(candidate);
        this.store.dispatch(editorActions.targetChanged());
        this.log('Connected to ' + description.board + ' via ' + key.toUpperCase() + '.', 'success');
      } catch (error) {
        if (candidate) await candidate.close(); else await transport.close();
        throw error;
      }
    });
  }
  async select(key: Path) {
    return this.run('Selecting connection', async () => {
      this.paths.select(key); this.syncPaths();
      await this.refresh(this.require(this.client));
      this.store.dispatch(editorActions.targetChanged());
    });
  }
  disconnect() {
    return this.run('Disconnecting', async client => {
      await this.require(client).close(); this.log('Path disconnected. Autonomous playback continues.');
    });
  }
  disconnectAll() {
    return this.run('Disconnecting device', async () => {
      for (const { client } of [...this.paths.paths.values()]) await client.close();
      this.log('Device disconnected. Autonomous playback continues.');
    });
  }
  async find(seeds: string[] = []) {
    if (this.finding || this.stopping) return;
    this.finding = true; this.store.dispatch(deviceActions.discoveryStarted());
    try {
      const result = await this.discovery.find([this.paths.paths.get('http')?.client.transport.url, this.suggestedAddress(), ...seeds]);
      if (this.stopping) return;
      this.store.dispatch(deviceActions.discoveryFinished({
        devices: result.devices,
        message: result.devices.length + (result.devices.length === 1 ? ' device found' : ' devices found') + (result.truncated ? ' · list full; use a direct address for others' : '') + '.',
      }));
    } catch (error) {
      if (!this.stopping) this.store.dispatch(deviceActions.discoveryFinished({ devices: [], message: (error as Error).message }));
    } finally { this.finding = false; }
  }
  suggestedAddress() {
    const status = this.store.getState().device.networkStatus;
    return status?.ip ? 'http://' + status.ip + ':' + status.http_port : '';
  }
  async readConfig(client: LightVMClient) {
    const description = await client.describe() as Description;
    const reply = await client.manage('config_get');
    if (client !== this.client) return;
    this.store.dispatch(deviceActions.description(description));
    this.store.dispatch(deviceActions.configuration({ config: parseJSON(new TextDecoder().decode(reply.data)), revision: reply.value >>> 0, savedRevision: description.saved_revision }));
  }
  async readNetwork(client: LightVMClient) {
    const description = this.store.getState().device.description;
    if (!(description?.wifi_supported || description?.http_supported)) return;
    const reply = await client.manage('network_get'), status = await client.json('network_status');
    if (client !== this.client) return;
    // Passwords never enter Redux state or DevTools.
    const config = parseJSON(new TextDecoder().decode(reply.data)) as NetworkConfig;
    for (const network of config.wifi.networks) delete network.password;
    if (config.wifi.ap) delete config.wifi.ap.password;
    this.store.dispatch(deviceActions.network({ config, revision: reply.value >>> 0 }));
    this.store.dispatch(deviceActions.networkStatus(status));
  }
  async readProcedures(client: LightVMClient) {
    const procedures = await client.json('procedure_list');
    if (client === this.client) this.store.dispatch(deviceActions.procedures(procedures));
  }
  async readPresets(client: LightVMClient) {
    if (!this.snapshot.device.description?.preset_api) return;
    const presets = await client.json('preset_list');
    if (client === this.client) this.store.dispatch(deviceActions.presets(presets));
  }
  async readProgramState(client: LightVMClient, background = false) {
    const output = this.snapshot.device.selectedOutput;
    const current = () => client === this.client && output === this.snapshot.device.selectedOutput && (!background || !this.store.getState().activity.operation);
    try {
      const programState = await client.json('program_state', ...(output ? [{ device: output }] : []));
      if (current()) this.store.dispatch(deviceActions.programState(serializable(programState)));
    } catch (error) {
      if (client.closed || !('code' in (error as object)) || (error as { code: number }).code !== 4) throw error;
      if (current()) this.store.dispatch(deviceActions.programState(null));
    }
  }
  selectOutput(id: string) {
    return this.run('Selecting preview output', async client => {
      this.store.dispatch(deviceActions.outputSelected(id));
      if (client) await this.readProgramState(client);
    });
  }
  async refresh(client = this.client) {
    if (!client) return;
    await this.readConfig(client); await this.readNetwork(client);
    await this.readProcedures(client); await this.readPresets(client); await this.readProgramState(client);
  }
  async poll() {
    if (this.polling || this.stopping || this.store.getState().activity.operation) return;
    this.polling = true;
    try {
      for (const [key, connection] of this.paths.paths) {
        const description = await connection.client.describe() as Description;
        if (this.store.getState().activity.operation || this.paths.paths.get(key)?.client !== connection.client) continue;
        if (description.device_id !== connection.description.device_id || description.boot_id !== connection.description.boot_id) {
          await connection.client.close();
          this.store.dispatch(activityActions.notice('The device rebooted or changed. Reconnect and review its state.'));
          continue;
        }
        if (connection.client !== this.client) continue;
        const state = this.store.getState().device;
        this.store.dispatch(deviceActions.description(description));
        if (description.active_revision !== state.configRevision || description.saved_revision !== state.savedRevision) await this.readConfig(connection.client);
        if (!this.store.getState().activity.operation && description.catalog_revision !== state.description?.catalog_revision) { await this.readProcedures(connection.client); await this.readPresets(connection.client); }
        if (!this.store.getState().activity.operation && description.network_revision !== state.networkRevision) await this.readNetwork(connection.client);
        if (!this.store.getState().activity.operation && (description.wifi_supported || description.http_supported)) {
          const status = await connection.client.json('network_status');
          if (connection.client === this.client) this.store.dispatch(deviceActions.networkStatus(status));
        }
        if (!this.store.getState().activity.operation) await this.readProgramState(connection.client, true);
      }
    } catch (error) { if (!this.store.getState().activity.operation) this.store.dispatch(activityActions.notice((error as Error).message)); }
    finally { this.polling = false; }
  }
  configure(config: HardwareConfig, revisions: { active_revision: number; saved_revision: number }, options: { save?: boolean; validate?: boolean } = {}) {
    return this.run(options.validate ? 'Validating configuration' : 'Applying configuration', async client => {
      const connection = this.require(client);
      await connection.configure(config, { ...options, revisions });
      if (!options.validate) {
        await this.readConfig(connection); await this.readProgramState(connection);
        this.store.dispatch(editorActions.targetChanged());
      }
      this.log(options.validate ? 'Configuration is valid.' : 'Output configuration applied; unchanged devices keep playing.', 'success');
    });
  }
  configureNetwork(config: NetworkConfig, revision: number) {
    return this.run('Saving network settings', async client => {
      const connection = this.require(client);
      const recovery = Boolean(this.store.getState().device.networkStatus?.recovery);
      const reply = await connection.configureNetwork(config, revision);
      const safeConfig = structuredClone(config);
      for (const network of safeConfig.wifi.networks) delete network.password;
      if (safeConfig.wifi.ap) delete safeConfig.wifi.ap.password;
      this.store.dispatch(deviceActions.network({ config: safeConfig, revision: reply.value >>> 0 }));
      this.log(recovery ? 'Network settings saved for next boot. Recovery AP remains active.' : 'Network settings saved. Reconnect HTTP after the network changes.', 'success');
      if (connection.closed) {
        this.store.dispatch(activityActions.notice('Network settings saved. Reconnect to continue.'));
      } else {
        try { await this.readNetwork(connection); }
        catch { this.store.dispatch(activityActions.notice('Network settings saved. Reconnect HTTP to refresh its status.')); }
      }
    });
  }
  async upload(bytecode: Uint8Array, source: string, revision: number, settings: SettingsValues = {}) {
    return this.run('Previewing animation', async client => {
      const connection = this.require(client), state = this.store.getState();
      const output = state.device.config?.devices.find(item => item.id === state.device.selectedOutput);
      if (state.editor.revision !== revision || !output) throw new Error('Wait for current code checks and select an output before previewing.');
      if (Object.keys(settings).length && !state.device.description?.transient_settings) throw new Error('Install firmware with preview settings support before previewing overrides.');
      await connection.upload(bytecode, state.device.description!.module_limit, progress => this.store.dispatch(activityActions.progress(progress)), output.id, settings);
      await connection.manage('device_command', { device: output.id, command: COMMAND.PLAY });
      this.log('Preview running on ' + output.id + ' (' + output.config.count + ' pixels).', 'success');
      await this.readProgramState(connection);
      if (this.store.getState().editor.source !== source) this.store.dispatch(activityActions.notice('The animation is running. Your newer source changes have not been uploaded yet.'));
    });
  }
  stopPlayback() { return this.run('Stopping playback', async client => {
    const output = this.snapshot.device.selectedOutput;
    if (!output) throw new Error('Select an output first.');
    await this.require(client).manage('device_command', { device: output, command: COMMAND.STOP });
    await this.readProgramState(this.require(client)); this.log('Playback stopped on ' + output + '. Lights cleared.', 'success');
  }); }
}
