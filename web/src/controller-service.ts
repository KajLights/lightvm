import { LightVMClient, COMMAND, parseJSON } from '../protocol.mjs';
import { HttpTransport, SerialTransport, WebSocketTransport } from '../transports.mjs';
import { Connections } from '../connections.mjs';
import { ControllerDiscovery } from '../discovery.mjs';
import { activityActions, controllerActions, editorActions, type AppStore } from './store';
import type { Description, HardwareConfig, InputBindings, NetworkConfig, Path } from './types';
import type { SettingsValues } from './program';

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
export class ControllerService {
  readonly paths = new Connections();
  readonly discovery = new ControllerDiscovery();
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
    this.store.dispatch(controllerActions.connections({
      paths: [...this.paths.paths].map(([key, value]) => ({ key, label: value.client.transport.label, protocol: value.client.transport.protocol || key.toUpperCase() })),
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
      if (problem.name === 'NotFoundError') { this.log('Controller selection cancelled.'); return false; }
      let refreshed = false;
      if (problem.conflict && client && !client.closed) {
        try { await this.refresh(client); refreshed = true; } catch { /* Preserve the original error. */ }
      }
      const message = problem.message + (refreshed ? ' Controller state refreshed. Review it before trying again.' : '');
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
        const selected = this.store.getState().controller.description;
        const controller = this.paths.paths.size && selected ? found.controllers.find(item => item.controller_id === selected.controller_id) : found.controllers[0];
        if (!controller) throw new Error('Select a discovered controller or enter its direct address.');
        address = controller.url;
      }
      let transport: SerialTransport | HttpTransport | WebSocketTransport = key === 'usb' ? new SerialTransport() : new HttpTransport(address);
      let candidate: LightVMClient | undefined;
      const clientFor = (transport: SerialTransport | HttpTransport | WebSocketTransport): LightVMClient => {
        const client = new LightVMClient(transport, {
          onDisconnect: (error: Error) => {
            if (this.paths.paths.get(key)?.client !== client) return;
            const selected = this.paths.selected === key;
            this.paths.detach(key); this.syncPaths();
            if (selected) {
              this.store.dispatch(controllerActions.programState(null));
              this.store.dispatch(editorActions.targetChanged());
            }
            if (error?.message !== 'Connection closed.' && !this.stopping) {
              this.store.dispatch(activityActions.notice(error.message)); this.log(error.message, 'error');
            }
          },
        });
        return client;
      };
      try {
        await transport.open();
        candidate = clientFor(transport);
        let description = await candidate.describe() as Description;
        if (!description.controller_id || !description.boot_id) throw new Error('This firmware cannot identify its controller and boot. Install current LightVM firmware.');
        if (key === 'http') {
          const identity = (transport as HttpTransport).identity;
          if (identity.controller_id !== description.controller_id || identity.boot_id !== description.boot_id) throw new Error('The controller rebooted or changed while connecting. Reconnect and review its state.');
          if (description.websocket_supported) {
            const bootstrap = candidate;
            try {
              transport = new WebSocketTransport(address); candidate = undefined;
              await transport.open(); candidate = clientFor(transport);
              const upgraded = await candidate.describe() as Description;
              if (upgraded.controller_id !== description.controller_id || upgraded.boot_id !== description.boot_id) throw new Error('The controller rebooted or changed during WebSocket connection. Reconnect and review its state.');
              description = upgraded;
            } finally { await bootstrap.close(); }
          }
        }
        if (this.stopping) throw new Error('Connection closed.');
        if (candidate.closed) throw new Error('Controller connection ended while connecting. Reconnect and review its state.');
        this.paths.attach(key, candidate, description); this.syncPaths(description);
        await this.refresh(candidate);
        this.store.dispatch(editorActions.targetChanged());
        this.log('Connected to ' + description.board + ' via ' + transport.protocol + '.', 'success');
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
    return this.run('Disconnecting controller', async () => {
      for (const { client } of [...this.paths.paths.values()]) await client.close();
      this.log('Controller disconnected. Autonomous playback continues.');
    });
  }
  async find(seeds: string[] = []) {
    if (this.finding || this.stopping) return;
    this.finding = true; this.store.dispatch(controllerActions.discoveryStarted());
    try {
      const result = await this.discovery.find([this.paths.paths.get('http')?.client.transport.url, this.suggestedAddress(), ...seeds]);
      if (this.stopping) return;
      this.store.dispatch(controllerActions.discoveryFinished({
        controllers: result.controllers,
        message: result.controllers.length + (result.controllers.length === 1 ? ' controller found' : ' controllers found') + (result.truncated ? ' · list full; use a direct address for others' : '') + '.',
      }));
    } catch (error) {
      if (!this.stopping) this.store.dispatch(controllerActions.discoveryFinished({ controllers: [], message: (error as Error).message }));
    } finally { this.finding = false; }
  }
  suggestedAddress() {
    const status = this.store.getState().controller.networkStatus;
    return status?.ip ? 'http://' + status.ip + ':' + status.http_port : '';
  }
  async readConfig(client: LightVMClient) {
    const description = await client.describe() as Description;
    const reply = await client.manage('config_get');
    if (client !== this.client) return;
    this.store.dispatch(controllerActions.description(description));
    this.store.dispatch(controllerActions.configuration({ config: parseJSON(new TextDecoder().decode(reply.data)), revision: reply.value >>> 0, savedRevision: description.saved_revision }));
  }
  async readNetwork(client: LightVMClient) {
    const description = this.store.getState().controller.description;
    if (!(description?.wifi_supported || description?.http_supported || description?.mqtt_supported)) return;
    const reply = await client.manage('network_get'), status = await client.json('network_status');
    if (client !== this.client) return;
    // Passwords never enter Redux state or DevTools.
    const config = parseJSON(new TextDecoder().decode(reply.data)) as NetworkConfig;
    for (const network of config.wifi.networks) delete network.password;
    if (config.wifi.ap) delete config.wifi.ap.password;
    if (config.mqtt) delete config.mqtt.password;
    this.store.dispatch(controllerActions.network({ config, revision: reply.value >>> 0 }));
    this.store.dispatch(controllerActions.networkStatus(status));
  }
  async readPrograms(client: LightVMClient) {
    const programs = await client.json('program_list');
    if (client === this.client) this.store.dispatch(controllerActions.programs(programs));
  }
  async readPresets(client: LightVMClient) {
    if (!this.snapshot.controller.description?.presets) return;
    const presets = await client.json('preset_list');
    if (client === this.client) this.store.dispatch(controllerActions.presets(presets));
  }
  async readProgramState(client: LightVMClient, background = false) {
    const output = this.snapshot.controller.selectedOutput;
    if (!output) { if (client === this.client) this.store.dispatch(controllerActions.programState(null)); return; }
    const current = () => client === this.client && output === this.snapshot.controller.selectedOutput && (!background || !this.store.getState().activity.operation);
    try {
      const transition = this.snapshot.controller.description?.ab_transitions ? await client.transition(output ?? undefined) : null;
      const fields = { ...(output ? { output } : {}), ...(transition?.recall?.power ? { slot: 'B' } : {}) };
      const programState = await client.json('program_state', ...(Object.keys(fields).length ? [fields] : []));
      if (current()) {
        const previous = this.snapshot.controller.programState;
        if (previous && previous.generation === programState.generation && previous.layer === programState.layer && previous.slot === programState.slot &&
          ((programState.inputs_revision ?? 0) < (previous.inputs_revision ?? 0) || (programState.render_count ?? 0) < (previous.render_count ?? 0))) return;
        this.store.dispatch(controllerActions.programState(serializable(programState)));
      }
    } catch (error) {
      if (client.closed || !('code' in (error as object)) || (error as { code: number }).code !== 4) throw error;
      if (current()) this.store.dispatch(controllerActions.programState(null));
    }
  }
  selectOutput(id: string) {
    return this.run('Selecting preview output', async client => {
      this.store.dispatch(controllerActions.outputSelected(id));
      if (client) await this.readProgramState(client);
    });
  }
  async refresh(client = this.client) {
    if (!client) return;
    await this.readConfig(client); await this.readNetwork(client);
    await this.readPrograms(client); await this.readPresets(client); await this.readProgramState(client);
  }
  async poll() {
    if (this.polling || this.stopping || this.store.getState().activity.operation) return;
    this.polling = true;
    try {
      for (const [key, connection] of this.paths.paths) {
        const description = await connection.client.describe() as Description;
        if (this.store.getState().activity.operation || this.paths.paths.get(key)?.client !== connection.client) continue;
        if (description.controller_id !== connection.description.controller_id || description.boot_id !== connection.description.boot_id) {
          await connection.client.close();
          this.store.dispatch(activityActions.notice('The controller rebooted or changed. Reconnect and review its state.'));
          continue;
        }
        if (connection.client !== this.client) continue;
        const state = this.store.getState().controller;
        this.store.dispatch(controllerActions.description(description));
        if (description.active_revision !== state.configRevision || description.saved_revision !== state.savedRevision) await this.readConfig(connection.client);
        if (!this.store.getState().activity.operation && description.catalog_revision !== state.description?.catalog_revision) { await this.readPrograms(connection.client); await this.readPresets(connection.client); }
        if (!this.store.getState().activity.operation && description.network_revision !== state.networkRevision) await this.readNetwork(connection.client);
        if (!this.store.getState().activity.operation && (description.wifi_supported || description.http_supported || description.mqtt_supported)) {
          const status = await connection.client.json('network_status');
          if (connection.client === this.client) this.store.dispatch(controllerActions.networkStatus(status));
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
      this.log(options.validate ? 'Configuration is valid.' : 'Output configuration applied; unchanged outputs keep playing.', 'success');
    });
  }
  configureNetwork(config: NetworkConfig, revision: number) {
    return this.run('Saving network settings', async client => {
      const connection = this.require(client);
      const recovery = Boolean(this.store.getState().controller.networkStatus?.recovery);
      const reply = await connection.configureNetwork(config, revision);
      const safeConfig = structuredClone(config);
      for (const network of safeConfig.wifi.networks) delete network.password;
      if (safeConfig.wifi.ap) delete safeConfig.wifi.ap.password;
      if (safeConfig.mqtt) delete safeConfig.mqtt.password;
      this.store.dispatch(controllerActions.network({ config: safeConfig, revision: reply.value >>> 0 }));
      this.log(recovery ? 'Network settings saved for next boot. Recovery AP remains active.' : 'Network settings saved. Reconnect after the network changes.', 'success');
      if (connection.closed) {
        this.store.dispatch(activityActions.notice('Network settings saved. Reconnect to continue.'));
      } else {
        try { await this.readNetwork(connection); }
        catch { this.store.dispatch(activityActions.notice('Network settings saved. Reconnect to refresh network status.')); }
      }
    });
  }
  async upload(bytecode: Uint8Array, source: string, revision: number, settings: SettingsValues = {}, inputs: InputBindings = {}) {
    return this.run('Previewing animation', async client => {
      const connection = this.require(client), state = this.store.getState();
      const output = state.controller.config?.outputs.find(item => item.id === state.controller.selectedOutput);
      if (state.editor.revision !== revision || !output) throw new Error('Wait for current code checks and select an output before previewing.');
      if (Object.keys(settings).length && !state.controller.description?.transient_settings) throw new Error('Install firmware with preview settings support before previewing overrides.');
      if (Object.keys(inputs).length && !state.controller.description?.live_inputs) throw new Error('Install firmware with live input support.');
      const args = [bytecode, state.controller.description!.module_limit, (progress: number) => this.store.dispatch(activityActions.progress(progress)), output.id, settings] as const;
      if (Object.keys(inputs).length) await connection.upload(...args, inputs); else await connection.upload(...args);
      await connection.manage('output_command', { output: output.id, command: COMMAND.PLAY });
      this.log('Preview running on ' + output.id + ' (' + output.config.count + ' pixels).', 'success');
      await this.readProgramState(connection);
      if (this.store.getState().editor.source !== source) this.store.dispatch(activityActions.notice('The animation is running. Your newer source changes have not been uploaded yet.'));
    });
  }
  stopPlayback() { return this.run('Stopping playback', async client => {
    const output = this.snapshot.controller.selectedOutput;
    if (!output) throw new Error('Select an output first.');
    await this.require(client).manage('output_command', { output: output, command: COMMAND.STOP });
    await this.readProgramState(this.require(client)); this.log('Playback stopped on ' + output + '. Lights cleared.', 'success');
  }); }
}
