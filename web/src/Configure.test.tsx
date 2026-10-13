import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { runtime } from './runtime';
import { Configure } from './Configure';
import { createAppStore, deviceActions } from './store';
import type { Description, HardwareConfig, NetworkConfig } from './types';

const description: Description = {
  device_id: 'test-device', boot_id: 'boot-1', board: 'Test board', control_abi: 2,
  program_schemas: ['lightvm.program/3'], module_limit: 65536, config_limit: 4096,
  pixel_capacity: 100, pins: ['GPIO2'], slots: ['A'], storage_available: true,
  wifi_supported: true, wifi_ap_supported: true, http_supported: true, active_revision: 1, saved_revision: 1,
  catalog_revision: 0, network_revision: 1, active_procedure: '',
};
function hardware(count: number): HardwareConfig {
  return {
    schema: 'lightvm.config/1',
    devices: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GPIO2', count } }],
    targets: { A: { device: 'lights' } }, startup: null,
  };
}
function network(port: number): NetworkConfig {
  return { schema: 'lightvm.network/1', wifi: { enabled: false, country: 'XX', networks: [] }, http: { enabled: true, port } };
}
function readyStore() {
  const store = createAppStore();
  store.dispatch(deviceActions.connections({ paths: [{ key: 'usb', label: 'USB' }], selectedPath: 'usb', description }));
  store.dispatch(deviceActions.configuration({ config: hardware(5), revision: 1, savedRevision: 1 }));
  store.dispatch(deviceActions.network({ config: network(80), revision: 1 }));
  return store;
}
afterEach(() => { cleanup(); vi.restoreAllMocks(); });

describe('configuration drafts', () => {
  it('keeps a hardware draft and blocks saving when device revisions change', () => {
    const store = readyStore();
    render(<Provider store={store}><Configure /></Provider>);
    const count = screen.getByLabelText('Pixel count') as HTMLInputElement;
    fireEvent.change(count, { target: { value: '7' } });
    act(() => { store.dispatch(deviceActions.configuration({ config: hardware(12), revision: 2, savedRevision: 2 })); });
    expect(count).toHaveValue(7);
    expect(screen.getByRole('alert')).toHaveTextContent('Device configuration changed');
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load device values' }));
    expect(count).toHaveValue(12);
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeEnabled();
  });

  it('keeps a network draft and blocks saving when its revision changes', () => {
    const store = readyStore();
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    const port = screen.getByLabelText('HTTP port') as HTMLInputElement;
    fireEvent.change(port, { target: { value: '8080' } });
    act(() => { store.dispatch(deviceActions.network({ config: network(9090), revision: 2 })); });
    expect(port).toHaveValue(8080);
    expect(screen.getByRole('alert')).toHaveTextContent('Network settings changed');
    expect(screen.getByRole('button', { name: 'Save & reconnect network' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load device values' }));
    expect(port).toHaveValue(9090);
    expect(screen.getByRole('button', { name: 'Save & reconnect network' })).toBeEnabled();
  });
  it('retains station and AP drafts across mode changes and clears attempted passwords', async () => {
    const store = readyStore(), config = network(80);
    config.wifi.networks = [{ ssid: 'studio' }];
    config.wifi.ap = { ssid: 'lights' };
    store.dispatch(deviceActions.network({ config, revision: 1 }));
    const save = vi.spyOn(runtime.devices, 'configureNetwork').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    fireEvent.change(screen.getByLabelText('Password'), { target: { value: 'station-secret' } });
    const mode = screen.getByLabelText('Wi-Fi mode');
    fireEvent.change(mode, { target: { value: 'ap' } });
    fireEvent.change(screen.getByLabelText('Access point SSID'), { target: { value: 'new-lights' } });
    fireEvent.change(screen.getByLabelText('Access point password'), { target: { value: 'ap-secret' } });
    fireEvent.change(mode, { target: { value: 'station' } });
    expect(screen.getByLabelText('Password')).toHaveValue('station-secret');
    fireEvent.change(mode, { target: { value: 'ap' } });
    expect(screen.getByLabelText('Access point SSID')).toHaveValue('new-lights');
    expect(screen.getByLabelText('Access point password')).toHaveValue('ap-secret');
    expect(JSON.stringify(store.getState())).not.toContain('secret');
    fireEvent.click(screen.getByRole('button', { name: 'Save & reconnect network' }));
    await waitFor(() => expect(screen.getByLabelText('Access point password')).toHaveValue(''));
    expect(save).toHaveBeenCalledWith(expect.objectContaining({ wifi: expect.objectContaining({ mode: 'ap', ap: { ssid: 'new-lights', password: 'ap-secret' }, networks: [{ ssid: 'studio', hidden: false, password: 'station-secret' }] }) }), 1);
    fireEvent.click(screen.getByLabelText('Use an open access point'));
    fireEvent.click(screen.getByRole('button', { name: 'Save & reconnect network' }));
    await waitFor(() => expect(save).toHaveBeenLastCalledWith(expect.objectContaining({ wifi: expect.objectContaining({ ap: { ssid: 'new-lights', password: '' } }) }), 1));
  });
  it('hides AP configuration for older firmware', () => {
    const store = readyStore();
    store.dispatch(deviceActions.description({ ...description, wifi_ap_supported: undefined }));
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    expect(screen.queryByLabelText('Wi-Fi mode')).toBeNull();
    expect(screen.queryByLabelText('Access point SSID')).toBeNull();
  });
});
