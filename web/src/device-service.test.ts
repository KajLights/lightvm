import { describe, expect, it, vi } from 'vitest';
import { DeviceService } from './device-service';
import { createAppStore } from './store';
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
      wifi: { enabled: true, country: 'XX', networks: [{ ssid: 'studio', password: 'secret' }] },
      http: { enabled: true, port: 8080 },
    };

    expect(await service.configureNetwork(config, 2)).toBe(true);
    expect(client.configureNetwork).toHaveBeenCalledWith(config, 2);
    expect(store.getState().device.networkRevision).toBe(3);
    expect(store.getState().device.networkConfig?.wifi.networks[0]).toEqual({ ssid: 'studio' });
    expect(store.getState().activity.notice).toMatch(/saved.*Reconnect HTTP/);
  });
});
