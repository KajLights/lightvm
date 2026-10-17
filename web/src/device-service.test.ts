import { describe, expect, it, vi } from 'vitest';
import { DeviceService } from './device-service';
import { createAppStore, deviceActions } from './store';
import type { Description, NetworkConfig } from './types';
import type { LightVMClient } from '../protocol.mjs';

describe('network commit', () => {
  it('keeps the successful save when changing network settings interrupts the path', async () => {
    const store = createAppStore();
    const service = new DeviceService(store);
    const client = { closed: false, configureNetwork: vi.fn().mockResolvedValue({ value: 3 }) } as unknown as LightVMClient;
    service.paths.attach('http', client, { device_id: 'board', boot_id: 'boot' } as Description);
    vi.spyOn(service, 'readNetwork').mockRejectedValue(new Error('HTTP port changed'));
    const config: NetworkConfig = {
      schema: 'lightvm.network/1',
      wifi: { enabled: true, country: 'XX', mode: 'ap', ap: { ssid: 'lights', password: 'ap-secret' }, networks: [{ ssid: 'studio', password: 'secret' }] },
      http: { enabled: true, port: 8080 },
    };

    expect(await service.configureNetwork(config, 2)).toBe(true);
    expect(client.configureNetwork).toHaveBeenCalledWith(config, 2);
    expect(store.getState().device.networkRevision).toBe(3);
    expect(store.getState().device.networkConfig?.wifi.networks[0]).toEqual({ ssid: 'studio' });
    expect(store.getState().device.networkConfig?.wifi.ap).toEqual({ ssid: 'lights' });
    expect(JSON.stringify(store.getState())).not.toContain('secret');
    expect(store.getState().activity.notice).toMatch(/saved.*Reconnect HTTP/);
  });
  it('removes AP passwords from device readbacks before dispatching them', async () => {
    const store = createAppStore(), service = new DeviceService(store);
    const client = {
      manage: vi.fn().mockResolvedValue({ value: 2, data: new TextEncoder().encode(JSON.stringify({
        schema: 'lightvm.network/1', wifi: { enabled: true, mode: 'ap', networks: [], ap: { ssid: 'lights', password: 'ap-secret' } }, http: { enabled: true, port: 80 },
      })) }),
      json: vi.fn().mockResolvedValue({ state: 'ap_active', mode: 'ap', ip: '192.168.4.1', revision: 2, http_port: 80 }),
    } as unknown as LightVMClient;
    service.paths.attach('http', client, { device_id: 'board', boot_id: 'boot', wifi_supported: true } as Description);
    store.dispatch(deviceActions.description({ device_id: 'board', boot_id: 'boot', wifi_supported: true } as Description));
    await service.readNetwork(client);
    expect(store.getState().device.networkConfig?.wifi.ap).toEqual({ ssid: 'lights' });
    expect(JSON.stringify(store.getState())).not.toContain('ap-secret');
  });
  it('keeps the current path and explains that recovery saves apply next boot', async () => {
    const store = createAppStore(), service = new DeviceService(store);
    const client = { closed: false, configureNetwork: vi.fn().mockResolvedValue({ value: 4 }) } as unknown as LightVMClient;
    service.paths.attach('http', client, { device_id: 'board', boot_id: 'boot' } as Description);
    store.dispatch(deviceActions.networkStatus({ state: 'ap_active', recovery: true, http_port: 80, revision: 3 }));
    vi.spyOn(service, 'readNetwork').mockResolvedValue();
    const config: NetworkConfig = { schema: 'lightvm.network/1', wifi: { enabled: false, country: 'XX', networks: [] }, http: { enabled: false, port: 8080 } };
    expect(await service.configureNetwork(config, 3)).toBe(true);
    expect(service.client).toBe(client);
    expect(JSON.stringify(store.getState().activity)).toContain('Recovery AP remains active');
    expect(JSON.stringify(store.getState().activity)).not.toContain('Reconnect HTTP');
  });
});

describe('selected preview output', () => {
  function workspace() {
    const store = createAppStore(), service = new DeviceService(store);
    const description = { device_id: 'pico', boot_id: 'boot', module_limit: 32768, transient_settings: true } as Description;
    const client = { closed: false, transport: { label: 'USB' }, upload: vi.fn(), manage: vi.fn(), json: vi.fn().mockResolvedValue({ generation: 2, device: 'second', settings_values: { duration: 2 } }) } as unknown as LightVMClient;
    service.paths.attach('usb', client, description); service.syncPaths(description);
    store.dispatch(deviceActions.configuration({ config: { schema: 'lightvm.config/1', devices: [
      { id: 'first', type: 'ws2812b', config: { pin: 'GP16', count: 20 } },
      { id: 'second', type: 'ws2812b', config: { pin: 'GP17', count: 20 } },
    ] }, revision: 1, savedRevision: 1 }));
    return { store, service, client };
  }

  it('routes upload, initialization settings, play, reads and stop to the chosen output', async () => {
    const { store, service, client } = workspace();
    store.dispatch(deviceActions.outputSelected('second'));
    const bytes = new Uint8Array(8), editor = store.getState().editor;
    expect(await service.upload(bytes, editor.source, editor.revision, { duration: 2 })).toBe(true);
    expect(client.upload).toHaveBeenCalledWith(bytes, 32768, expect.any(Function), 'second', { duration: 2 });
    expect(client.manage).toHaveBeenCalledWith('device_command', { device: 'second', command: 7 });
    expect(client.json).toHaveBeenCalledWith('program_parameters_get', { device: 'second' });
    await service.stopPlayback();
    expect(client.manage).toHaveBeenLastCalledWith('device_command', { device: 'second', command: 8 });
  });

  it('ignores a parameter response for an output deselected during a background read', async () => {
    const { store, service, client } = workspace();
    let complete!: (state: unknown) => void;
    vi.mocked(client.json).mockImplementation(() => new Promise(resolve => { complete = resolve; }));
    const reading = service.readParameters(client, true);
    store.dispatch(deviceActions.outputSelected('second'));
    complete({ generation: 1, device: 'first' }); await reading;
    expect(store.getState().device.parameters).toBeNull();
  });
});
