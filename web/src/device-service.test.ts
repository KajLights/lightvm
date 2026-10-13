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
