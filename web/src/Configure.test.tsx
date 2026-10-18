import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { runtime } from './runtime';
import { Configure } from './Configure';
import { createAppStore, deviceActions } from './store';
import type { Description, HardwareConfig, NetworkConfig } from './types';

const description: Description = {
  device_id: 'test-device', boot_id: 'boot-1', board: 'Test board', version: '1.0.0', module_limit: 32768, config_limit: 8192,
  pixel_capacity: 100, pins: ['GPIO2'], slots: ['A'], storage_available: true,
  wifi_supported: true, wifi_ap_supported: true, http_supported: true, active_revision: 1, saved_revision: 1,
  catalog_revision: 0, network_revision: 1, active_procedure: '',
};
function hardware(count: number): HardwareConfig {
  return {
    schema: 'lightvm.config',
    devices: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GPIO2', count } }],
  };
}
function network(port: number): NetworkConfig {
  return { schema: 'lightvm.network', wifi: { enabled: false, country: 'XX', networks: [] }, http: { enabled: true, port } };
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
  it('assigns one HA preset per output and preserves it through other edits', async () => {
    const store = readyStore();
    store.dispatch(deviceActions.description({ ...description, mqtt_supported: true }));
    store.dispatch(deviceActions.presets([{ name: 'warm', size: 100, crc32: 1 }]));
    const configure = vi.spyOn(runtime.devices, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.change(screen.getByLabelText('Home Assistant ON preset'), { target: { value: 'warm' } });
    fireEvent.change(screen.getByLabelText('Home Assistant name'), { target: { value: 'Desk lamp' } });
    fireEvent.change(screen.getByLabelText('Pixel count'), { target: { value: '7' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith(expect.objectContaining({ devices: [expect.objectContaining({ home_assistant: { on_preset: 'warm', name: 'Desk lamp' }, config: expect.objectContaining({ count: 7 }) })] }), expect.anything(), { save: true }));
    fireEvent.change(screen.getByLabelText('Home Assistant ON preset'), { target: { value: '' } });
    expect(JSON.parse((screen.getByLabelText('Device configuration JSON') as HTMLTextAreaElement).value).devices[0]).not.toHaveProperty('home_assistant');
  });

  it('configures an MQTT-only host and clears attempted broker passwords', async () => {
    const store = readyStore();
    store.dispatch(deviceActions.description({ ...description, wifi_supported: false, http_supported: false, mqtt_supported: true }));
    const save = vi.spyOn(runtime.devices, 'configureNetwork').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi, HTTP & MQTT' }));
    fireEvent.click(screen.getByLabelText('Enable MQTT control'));
    fireEvent.change(screen.getByLabelText('Broker IPv4 address'), { target: { value: '192.168.1.10' } });
    fireEvent.change(screen.getByLabelText('MQTT username'), { target: { value: 'kaj' } });
    fireEvent.change(screen.getByLabelText('MQTT password'), { target: { value: 'mqtt-secret' } });
    expect(JSON.stringify(store.getState())).not.toContain('mqtt-secret');
    fireEvent.click(screen.getByRole('button', { name: 'Save & reconnect network' }));
    await waitFor(() => expect(screen.getByLabelText('MQTT password')).toHaveValue(''));
    expect(save).toHaveBeenLastCalledWith(expect.objectContaining({ mqtt: { enabled: true, host: '192.168.1.10', port: 1883, username: 'kaj', password: 'mqtt-secret' } }), 1);
    fireEvent.click(screen.getByRole('button', { name: 'Save & reconnect network' }));
    await waitFor(() => expect(save).toHaveBeenLastCalledWith(expect.objectContaining({ mqtt: { enabled: true, host: '192.168.1.10', port: 1883, username: 'kaj' } }), 1));
    fireEvent.click(screen.getByLabelText('Clear MQTT password'));
    fireEvent.click(screen.getByRole('button', { name: 'Save & reconnect network' }));
    await waitFor(() => expect(save).toHaveBeenLastCalledWith(expect.objectContaining({ mqtt: expect.objectContaining({ password: '' }) }), 1));
  });

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

  it('edits each startup independently and submits constructor arguments', async () => {
    const store = readyStore(), config = hardware(5);
    config.devices[0].startup = { procedure: 'rainbow', args: { duration: 3.3 } };
    config.devices.push({ id: 'desk', type: 'ws2812b', config: { pin: 'GPIO3', count: 8 }, startup: { procedure: 'color', args: { color: 0x123456, random_color: false } } });
    store.dispatch(deviceActions.configuration({ config, revision: 1, savedRevision: 1 }));
    store.dispatch(deviceActions.procedures([{ name: 'rainbow', size: 100, crc32: 1 }, { name: 'color', size: 100, crc32: 2 }]));
    const configure = vi.spyOn(runtime.devices, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    const first = within(screen.getByRole('group', { name: 'Output 1' }));
    const second = within(screen.getByRole('group', { name: 'Output 2' }));
    fireEvent.change(first.getByLabelText('Output name'), { target: { value: 'ceiling' } });
    fireEvent.change(first.getByLabelText('Startup arguments (JSON)'), { target: { value: '{"duration": 6.6}' } });
    expect(second.getByLabelText('Startup program')).toHaveValue('color');
    expect(JSON.parse((second.getByLabelText('Startup arguments (JSON)') as HTMLTextAreaElement).value)).toEqual(config.devices[1].startup!.args);
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith({ schema: 'lightvm.config', devices: [
      { ...config.devices[0], id: 'ceiling', startup: { procedure: 'rainbow', args: { duration: 6.6 } } }, config.devices[1],
    ] }, expect.objectContaining({ active_revision: 1, saved_revision: 1 }), { save: true }));
    fireEvent.change(first.getByLabelText('Startup program'), { target: { value: 'color' } });
    expect(first.getByLabelText('Startup arguments (JSON)')).toHaveValue('{}');
    fireEvent.change(first.getByLabelText('Startup program'), { target: { value: '' } });
    const json = JSON.parse((screen.getByLabelText('Device configuration JSON') as HTMLTextAreaElement).value);
    expect(json.devices[0]).not.toHaveProperty('startup');
    expect(json.devices[1]).toEqual(config.devices[1]);
    expect(screen.queryByLabelText('Bind this output to slot A')).toBeNull();
  });

  it('keeps incomplete arguments visible and blocks submitting them', () => {
    const store = readyStore(), config = hardware(5);
    config.devices[0].startup = { procedure: 'rainbow', args: { duration: 3.3 } };
    store.dispatch(deviceActions.configuration({ config, revision: 1, savedRevision: 1 }));
    render(<Provider store={store}><Configure /></Provider>);
    const args = screen.getByLabelText('Startup arguments (JSON)');
    for (const value of ['{', 'null', '[]', '{"duration":"fast"}']) {
      fireEvent.change(args, { target: { value } });
      expect(args).toHaveValue(value);
      expect(args).toHaveAttribute('aria-invalid', 'true');
      expect(screen.getByRole('button', { name: 'Validate' })).toBeDisabled();
      expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    }
    act(() => { store.dispatch(deviceActions.configuration({ config: hardware(12), revision: 2, savedRevision: 2 })); });
    expect(args).toHaveValue('{"duration":"fast"}');
    fireEvent.change(args, { target: { value: '{}' } });
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load device values' }));
    expect(screen.queryByLabelText('Startup arguments (JSON)')).toBeNull();
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeEnabled();
  });

  it('synchronizes full JSON edits and preserves other devices when removing an output', () => {
    const store = readyStore(), config = hardware(5);
    config.devices[0].startup = { procedure: 'rainbow', args: { duration: 3.3 } };
    config.devices.push({ id: 'desk', type: 'ws2812b', config: { pin: 'GPIO3', count: 8 }, startup: { procedure: 'rainbow', args: { duration: 9 } } });
    render(<Provider store={store}><Configure /></Provider>);
    const editor = screen.getByLabelText('Device configuration JSON') as HTMLTextAreaElement;
    fireEvent.change(editor, { target: { value: JSON.stringify(config) } });
    expect(screen.getAllByLabelText('Startup program').map(node => (node as HTMLSelectElement).value)).toEqual(['rainbow', 'rainbow']);
    const second = within(screen.getByRole('group', { name: 'Output 2' }));
    fireEvent.change(second.getByLabelText('Startup arguments (JSON)'), { target: { value: '{' } });
    fireEvent.click(within(screen.getByRole('group', { name: 'Output 1' })).getByRole('button', { name: 'Remove output' }));
    expect(JSON.parse(editor.value).devices).toEqual([config.devices[1]]);
    expect(screen.getByLabelText('Startup arguments (JSON)')).toHaveValue('{');
    fireEvent.change(screen.getByLabelText('Startup arguments (JSON)'), { target: { value: '{"duration": 10}' } });
    fireEvent.click(screen.getByRole('button', { name: 'Add WS2812B output' }));
    const next = JSON.parse(editor.value);
    expect(next.devices[0].startup.args).toEqual({ duration: 10 });
    expect(next.devices[1].id).toBe('lights');
    expect(next.devices[1]).not.toHaveProperty('startup');
    expect(next).not.toHaveProperty('targets');
    fireEvent.change(editor, { target: { value: JSON.stringify({ ...config, targets: {} }) } });
    expect(screen.getByRole('button', { name: 'Apply in RAM' })).toBeDisabled();
    expect(screen.getByRole('alert')).toHaveTextContent('Startup belongs inside each device');
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
  it('shows saved settings and the next-boot save action during recovery', () => {
    const store = readyStore();
    store.dispatch(deviceActions.networkStatus({ state: 'ap_active', mode: 'ap', recovery: true, ssid: 'lightvm-123456', ip: '192.168.4.1', http_port: 80, revision: 1 }));
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    expect(screen.getByRole('status')).toHaveTextContent('saved network settings');
    expect(screen.getByRole('button', { name: 'Save network for next boot' })).toBeEnabled();
    expect(screen.getByLabelText('Wi-Fi mode')).toHaveValue('station');
  });
});
