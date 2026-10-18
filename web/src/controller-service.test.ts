import { describe, expect, it, vi } from 'vitest';
import { ControllerService } from './controller-service';
import { createAppStore, controllerActions } from './store';
import type { Description, NetworkConfig } from './types';
import type { LightVMClient } from '../protocol.mjs';
import { LightVMClient as Client } from '../protocol.mjs';
import { HttpTransport, WebSocketTransport } from '../transports.mjs';

describe('network connection selection', () => {
  function connection(websocket = true) {
    const store = createAppStore(), service = new ControllerService(store);
    const description = { controller_id: 'board', boot_id: 'boot', board: 'pico_w', websocket_supported: websocket } as Description;
    const httpOpen = vi.spyOn(HttpTransport.prototype, 'open').mockImplementation(async function(this: HttpTransport) { this.identity = { session_id: '1'.repeat(32), controller_id: 'board', boot_id: 'boot' }; return this; });
    const httpClose = vi.spyOn(HttpTransport.prototype, 'close').mockResolvedValue();
    const socketOpen = vi.spyOn(WebSocketTransport.prototype, 'open').mockImplementation(async function(this: WebSocketTransport) { return this; });
    const socketClose = vi.spyOn(WebSocketTransport.prototype, 'close').mockResolvedValue(undefined);
    const describe = vi.spyOn(Client.prototype, 'describe').mockResolvedValue(description);
    const refresh = vi.spyOn(service, 'refresh').mockResolvedValue();
    return { store, service, description, httpOpen, httpClose, socketOpen, socketClose, describe, refresh };
  }
  it('upgrades before refreshing or attaching and closes its bootstrap HTTP session', async () => {
    const c = connection();
    expect(await c.service.connect('http', 'http://192.168.1.2:8080')).toBe(true);
    expect(c.service.client?.transport).toBeInstanceOf(WebSocketTransport);
    expect(c.httpOpen).toHaveBeenCalledOnce(); expect(c.socketOpen).toHaveBeenCalledOnce();
    expect(c.httpClose).toHaveBeenCalled(); expect(c.socketClose).not.toHaveBeenCalled();
    expect(c.refresh).toHaveBeenCalledWith(c.service.client);
    expect(c.store.getState().controller.paths).toEqual([{ key: 'http', label: '192.168.1.2:8080 · WebSocket', protocol: 'WebSocket' }]);
    expect(c.store.getState().activity.entries[0].text).toContain('via WebSocket');
  });
  it('uses HTTP when the optional server is compiled out', async () => {
    const c = connection(false);
    expect(await c.service.connect('http', 'http://controller.local')).toBe(true);
    expect(c.service.client?.transport).toBeInstanceOf(HttpTransport);
    expect(c.socketOpen).not.toHaveBeenCalled(); expect(c.httpClose).not.toHaveBeenCalled();
  });
  it.each(['controller_id', 'boot_id'] as const)('rejects a changed %s during upgrade and closes both candidates', async field => {
    const c = connection();
    c.describe.mockResolvedValueOnce(c.description).mockResolvedValueOnce({ ...c.description, [field]: 'changed' });
    expect(await c.service.connect('http', 'http://controller.local')).toBe(false);
    expect(c.httpClose).toHaveBeenCalled(); expect(c.socketClose).toHaveBeenCalled();
    expect(c.service.paths.paths.size).toBe(0); expect(c.refresh).not.toHaveBeenCalled();
    expect(c.store.getState().activity.notice).toContain('rebooted or changed');
  });
  it('reports a failed advertised upgrade without silently attaching HTTP', async () => {
    const c = connection(); c.socketOpen.mockRejectedValue(new Error('WebSocket blocked'));
    expect(await c.service.connect('http', 'http://controller.local')).toBe(false);
    expect(c.httpClose).toHaveBeenCalled(); expect(c.socketClose).toHaveBeenCalled();
    expect(c.service.paths.paths.size).toBe(0); expect(c.refresh).not.toHaveBeenCalled();
  });
  it('does not attach a socket lost while its bootstrap session is being released', async () => {
    const c = connection();
    c.httpClose.mockImplementation(async () => {
      const socket = c.socketOpen.mock.contexts[0] as WebSocketTransport;
      socket.onDisconnect(new Error('Socket lost during bootstrap close'));
    });
    expect(await c.service.connect('http', 'http://controller.local')).toBe(false);
    expect(c.service.paths.paths.size).toBe(0); expect(c.refresh).not.toHaveBeenCalled();
  });
  it('detaches a lost socket without switching to USB or replaying commands', async () => {
    const c = connection();
    expect(await c.service.connect('http', 'http://controller.local')).toBe(true);
    const current = c.service.client!;
    const usb = { transport: { label: 'USB', protocol: 'USB' } } as LightVMClient;
    c.service.paths.attach('usb', usb, c.description); c.service.paths.select('http');
    current.transport.onDisconnect(new Error('Socket lost'));
    expect(c.service.client).toBeNull(); expect(c.service.paths.paths.get('usb')?.client).toBe(usb);
    expect(c.socketOpen).toHaveBeenCalledOnce();
    expect(c.store.getState().activity.notice).toBe('Socket lost');
  });
});

describe('network commit', () => {
  it('keeps the successful save when changing network settings interrupts the path', async () => {
    const store = createAppStore();
    const service = new ControllerService(store);
    const client = { closed: false, configureNetwork: vi.fn().mockResolvedValue({ value: 3 }) } as unknown as LightVMClient;
    service.paths.attach('http', client, { controller_id: 'board', boot_id: 'boot' } as Description);
    vi.spyOn(service, 'readNetwork').mockRejectedValue(new Error('HTTP port changed'));
    const config: NetworkConfig = {
      schema: 'lightvm.network',
      wifi: { enabled: true, country: 'XX', mode: 'ap', ap: { ssid: 'lights', password: 'ap-secret' }, networks: [{ ssid: 'studio', password: 'secret' }] },
      http: { enabled: true, port: 8080 },
      mqtt: { enabled: true, host: '192.168.1.10', port: 1883, username: 'kaj', password: 'mqtt-secret' },
    };

    expect(await service.configureNetwork(config, 2)).toBe(true);
    expect(client.configureNetwork).toHaveBeenCalledWith(config, 2);
    expect(store.getState().controller.networkRevision).toBe(3);
    expect(store.getState().controller.networkConfig?.wifi.networks[0]).toEqual({ ssid: 'studio' });
    expect(store.getState().controller.networkConfig?.wifi.ap).toEqual({ ssid: 'lights' });
    expect(store.getState().controller.networkConfig?.mqtt).not.toHaveProperty('password');
    expect(JSON.stringify(store.getState())).not.toContain('secret');
    expect(store.getState().activity.notice).toMatch(/saved.*Reconnect/);
  });
  it('removes AP passwords from controller readbacks before dispatching them', async () => {
    const store = createAppStore(), service = new ControllerService(store);
    const client = {
      manage: vi.fn().mockResolvedValue({ value: 2, data: new TextEncoder().encode(JSON.stringify({
        schema: 'lightvm.network', wifi: { enabled: true, mode: 'ap', networks: [], ap: { ssid: 'lights', password: 'ap-secret' } }, http: { enabled: true, port: 80 }, mqtt: { enabled: true, host: '127.0.0.1', port: 1883, username: 'kaj', password: 'mqtt-secret' },
      })) }),
      json: vi.fn().mockResolvedValue({ state: 'ap_active', mode: 'ap', ip: '192.168.4.1', revision: 2, http_port: 80 }),
    } as unknown as LightVMClient;
    service.paths.attach('http', client, { controller_id: 'board', boot_id: 'boot', mqtt_supported: true } as Description);
    store.dispatch(controllerActions.description({ controller_id: 'board', boot_id: 'boot', mqtt_supported: true } as Description));
    await service.readNetwork(client);
    expect(store.getState().controller.networkConfig?.wifi.ap).toEqual({ ssid: 'lights' });
    expect(store.getState().controller.networkConfig?.mqtt).not.toHaveProperty('password');
    expect(JSON.stringify(store.getState())).not.toContain('ap-secret');
  });
  it('keeps the current path and explains that recovery saves apply next boot', async () => {
    const store = createAppStore(), service = new ControllerService(store);
    const client = { closed: false, configureNetwork: vi.fn().mockResolvedValue({ value: 4 }) } as unknown as LightVMClient;
    service.paths.attach('http', client, { controller_id: 'board', boot_id: 'boot' } as Description);
    store.dispatch(controllerActions.networkStatus({ state: 'ap_active', recovery: true, http_port: 80, revision: 3 }));
    vi.spyOn(service, 'readNetwork').mockResolvedValue();
    const config: NetworkConfig = { schema: 'lightvm.network', wifi: { enabled: false, country: 'XX', networks: [] }, http: { enabled: false, port: 8080 } };
    expect(await service.configureNetwork(config, 3)).toBe(true);
    expect(service.client).toBe(client);
    expect(JSON.stringify(store.getState().activity)).toContain('Recovery AP remains active');
    expect(JSON.stringify(store.getState().activity)).not.toContain('Reconnect');
  });
});

describe('selected preview output', () => {
  function workspace() {
    const store = createAppStore(), service = new ControllerService(store);
    const description = { controller_id: 'pico', boot_id: 'boot', module_limit: 32768, transient_settings: true } as Description;
    const client = { closed: false, transport: { label: 'USB' }, upload: vi.fn(), manage: vi.fn(), json: vi.fn().mockResolvedValue({ generation: 2, output: 'second', settings_values: { duration: 2 } }) } as unknown as LightVMClient;
    service.paths.attach('usb', client, description); service.syncPaths(description);
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
      { id: 'first', type: 'ws2812b', config: { pin: 'GP16', count: 20 } },
      { id: 'second', type: 'ws2812b', config: { pin: 'GP17', count: 20 } },
    ] }, revision: 1, savedRevision: 1 }));
    return { store, service, client };
  }

  it('routes upload, initialization settings, play, reads and stop to the chosen output', async () => {
    const { store, service, client } = workspace();
    store.dispatch(controllerActions.outputSelected('second'));
    const bytes = new Uint8Array(8), editor = store.getState().editor;
    expect(await service.upload(bytes, editor.source, editor.revision, { duration: 2 })).toBe(true);
    expect(client.upload).toHaveBeenCalledWith(bytes, 32768, expect.any(Function), 'second', { duration: 2 });
    expect(client.manage).toHaveBeenCalledWith('output_command', { output: 'second', command: 7 });
    expect(client.json).toHaveBeenCalledWith('program_state', { output: 'second' });
    await service.stopPlayback();
    expect(client.manage).toHaveBeenLastCalledWith('output_command', { output: 'second', command: 8 });
  });

  it('ignores a program-state response for an output deselected during a background read', async () => {
    const { store, service, client } = workspace();
    let complete!: (state: unknown) => void;
    vi.mocked(client.json).mockImplementation(() => new Promise(resolve => { complete = resolve; }));
    const reading = service.readProgramState(client, true);
    store.dispatch(controllerActions.outputSelected('second'));
    complete({ generation: 1, output: 'first' }); await reading;
    expect(store.getState().controller.programState).toBeNull();
  });
});
