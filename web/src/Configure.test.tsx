import { deployedTransition, WIPE_SETTINGS } from '../../tests/web-transitions';
import type { LightVMClient } from '../protocol.mjs';
import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { runtime } from './runtime';
import { Configure } from './Configure';
import { createAppStore, controllerActions } from './store';
import type { Description, HardwareConfig, NetworkConfig } from './types';

const description: Description = {
  controller_id: 'test-controller', boot_id: 'boot-1', board: 'Test board', version: '1.0.0', module_limit: 32768, config_limit: 8192,
  pixel_capacity: 100, pins: ['GPIO2'], slots: ['A'], storage_available: true,
  wifi_supported: true, wifi_ap_supported: true, http_supported: true, active_revision: 1, saved_revision: 1,
  catalog_revision: 0, network_revision: 1, active_program: '',
};
function hardware(count: number): HardwareConfig {
  return {
    schema: 'lightvm.config',
    outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GPIO2', count } }],
  };
}
function network(port: number): NetworkConfig {
  return { schema: 'lightvm.network', wifi: { enabled: false, country: 'XX', networks: [] }, http: { enabled: true, port } };
}
function readyStore() {
  const store = createAppStore();
  store.dispatch(controllerActions.connections({ paths: [{ key: 'usb', label: 'USB' }], selectedPath: 'usb', description }));
  store.dispatch(controllerActions.configuration({ config: hardware(5), revision: 1, savedRevision: 1 }));
  store.dispatch(controllerActions.network({ config: network(80), revision: 1 }));
  return store;
}
afterEach(() => { cleanup(); vi.restoreAllMocks(); });

describe('configuration drafts', () => {
  it('adds the Pico keypad as generic input and indicator output and preserves the binding through edits', async () => {
    const store = readyStore();
    store.dispatch(controllerActions.description({ ...description, mqtt_supported: true, pins: ['GPIO2', 'GP4', 'GP5', 'GP17', 'GP18', 'GP19'] }));
    const configure = vi.spyOn(runtime.controllers, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Add Pico RGB Keypad' }));
    const keypad = within(screen.getByRole('group', { name: 'Output 2' }));
    expect(keypad.queryByLabelText('Startup program')).toBeNull();
    expect(keypad.queryByLabelText('Home Assistant default preset')).toBeNull();
    fireEvent.change(keypad.getByLabelText('Brightness limit (0–255)'), { target: { value: '48' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith(expect.objectContaining({
      outputs: [expect.anything(), expect.objectContaining({ type: 'apa102', mode: 'indicators', config: expect.objectContaining({ data_pin: 'GP19', clock_pin: 'GP18', enable_pin: 'GP17', count: 16, brightness_limit: 48 }) })],
      inputs: [{ id: 'keypad', type: 'tca9555', config: { sda: 'GP4', scl: 'GP5', address: 32, active_low: true, debounce_ms: 20 }, home_assistant: { indicators: 'key_lights' } }],
    }), expect.anything(), { save: true }));
  });

  it('keeps indicator outputs out of animation selection', () => {
    const store = readyStore(), config = hardware(5);
    config.outputs.unshift({ id: 'keys', type: 'apa102', mode: 'indicators', config: { data_pin: 'GP19', clock_pin: 'GP18', count: 16 } });
    store.dispatch(controllerActions.configuration({ config, revision: 2, savedRevision: 1 }));
    expect(store.getState().controller.selectedOutput).toBe('lights');
    store.dispatch(controllerActions.outputSelected('keys'));
    expect(store.getState().controller.selectedOutput).toBe('lights');
    const onlyIndicators = structuredClone(config); onlyIndicators.outputs.pop();
    store.dispatch(controllerActions.configuration({ config: onlyIndicators, revision: 3, savedRevision: 1 }));
    expect(store.getState().controller.selectedOutput).toBeNull();
  });

  it('edits pinned on/off transitions and preserves their settings in output configuration', async () => {
    const store = readyStore(), wipe = deployedTransition('wipe', WIPE_SETTINGS);
    store.dispatch(controllerActions.description({ ...description, power_transitions: true }));
    store.dispatch(controllerActions.programs([wipe.program]));
    vi.spyOn(runtime.controllers, 'client', 'get').mockReturnValue({ getProgram: vi.fn(async () => wipe.bytes) } as unknown as LightVMClient);
    const configure = vi.spyOn(runtime.controllers, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.change(screen.getByLabelText('On transition'), { target: { value: 'wipe' } });
    const on = within(screen.getByRole('group', { name: 'On transition settings' }));
    fireEvent.click(await on.findByLabelText('Override reverse'));
    fireEvent.change(on.getByLabelText('reverse'), { target: { value: 'true' } });
    fireEvent.change(screen.getByLabelText('On transition duration (ms)'), { target: { value: '750' } });
    fireEvent.change(screen.getByLabelText('Off transition'), { target: { value: 'wipe' } });
    fireEvent.change(screen.getByLabelText('Off transition duration (ms)'), { target: { value: '1250' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith(expect.objectContaining({ outputs: [expect.objectContaining({
      on_transition: { program: 'wipe', crc32: wipe.program.crc32, duration_ms: 750, settings: { reverse: true } },
      off_transition: { program: 'wipe', crc32: wipe.program.crc32, duration_ms: 1250, settings: {} },
    })] }), expect.anything(), { save: true }));
    fireEvent.change(screen.getByLabelText('Off transition duration (ms)'), { target: { value: '-1' } });
    expect(screen.getByLabelText('Off transition duration (ms)')).toHaveValue(-1);
    expect(screen.getByRole('button', { name: 'Apply in RAM' })).toBeDisabled();
    fireEvent.change(screen.getByLabelText('Off transition'), { target: { value: '' } });
    expect(screen.getByRole('button', { name: 'Apply in RAM' })).toBeEnabled();
  });

  it('assigns a default and selectable HA presets per output and preserves them through other edits', async () => {
    const store = readyStore();
    store.dispatch(controllerActions.description({ ...description, mqtt_supported: true }));
    store.dispatch(controllerActions.presets([{ name: 'warm', size: 100, crc32: 1 }, { name: 'stars', size: 100, crc32: 2 }]));
    const configure = vi.spyOn(runtime.controllers, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.change(screen.getByLabelText('Home Assistant default preset'), { target: { value: 'warm' } });
    expect(screen.getByLabelText('Expose preset warm')).toBeDisabled();
    fireEvent.click(screen.getByLabelText('Expose preset stars'));
    fireEvent.change(screen.getByLabelText('Home Assistant name'), { target: { value: 'Desk lamp' } });
    fireEvent.change(screen.getByLabelText('Pixel count'), { target: { value: '7' } });
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith(expect.objectContaining({ outputs: [expect.objectContaining({ home_assistant: { default_preset: 'warm', presets: ['warm', 'stars'], name: 'Desk lamp' }, config: expect.objectContaining({ count: 7 }) })] }), expect.anything(), { save: true }));
    fireEvent.change(screen.getByLabelText('Home Assistant default preset'), { target: { value: '' } });
    expect(JSON.parse((screen.getByLabelText('Controller configuration JSON') as HTMLTextAreaElement).value).outputs[0]).not.toHaveProperty('home_assistant');
  });

  it('configures an MQTT-only host and clears attempted broker passwords', async () => {
    const store = readyStore();
    store.dispatch(controllerActions.description({ ...description, wifi_supported: false, http_supported: false, mqtt_supported: true }));
    const save = vi.spyOn(runtime.controllers, 'configureNetwork').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi, HTTP & MQTT' }));
    fireEvent.click(screen.getByLabelText('Enable MQTT control'));
    fireEvent.change(screen.getByLabelText('Broker hostname or IPv4 address'), { target: { value: '192.168.1.10' } });
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

  it('keeps a hardware draft and blocks saving when controller revisions change', () => {
    const store = readyStore();
    render(<Provider store={store}><Configure /></Provider>);
    const count = screen.getByLabelText('Pixel count') as HTMLInputElement;
    fireEvent.change(count, { target: { value: '7' } });
    act(() => { store.dispatch(controllerActions.configuration({ config: hardware(12), revision: 2, savedRevision: 2 })); });
    expect(count).toHaveValue(7);
    expect(screen.getByRole('alert')).toHaveTextContent('Controller configuration changed');
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load controller values' }));
    expect(count).toHaveValue(12);
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeEnabled();
  });

  it('edits each startup independently and submits initialization settings', async () => {
    const store = readyStore(), config = hardware(5);
    config.outputs[0].startup = { program: 'rainbow', settings: { duration: 3.3 } };
    config.outputs.push({ id: 'desk', type: 'ws2812b', config: { pin: 'GPIO3', count: 8 }, startup: { program: 'color', settings: { color: 0x123456, random_color: false } } });
    store.dispatch(controllerActions.configuration({ config, revision: 1, savedRevision: 1 }));
    store.dispatch(controllerActions.programs([{ name: 'rainbow', size: 100, crc32: 1 }, { name: 'color', size: 100, crc32: 2 }]));
    const configure = vi.spyOn(runtime.controllers, 'configure').mockResolvedValue(false);
    render(<Provider store={store}><Configure /></Provider>);
    const first = within(screen.getByRole('group', { name: 'Output 1' }));
    const second = within(screen.getByRole('group', { name: 'Output 2' }));
    fireEvent.change(first.getByLabelText('Output name'), { target: { value: 'ceiling' } });
    fireEvent.change(first.getByLabelText('Startup settings (JSON)'), { target: { value: '{"duration": 6.6}' } });
    expect(second.getByLabelText('Startup program')).toHaveValue('color');
    expect(JSON.parse((second.getByLabelText('Startup settings (JSON)') as HTMLTextAreaElement).value)).toEqual(config.outputs[1].startup!.settings);
    fireEvent.click(screen.getByRole('button', { name: 'Apply & save for reboot' }));
    await waitFor(() => expect(configure).toHaveBeenCalledWith({ schema: 'lightvm.config', outputs: [
      { ...config.outputs[0], id: 'ceiling', startup: { program: 'rainbow', settings: { duration: 6.6 } } }, config.outputs[1],
    ] }, expect.objectContaining({ active_revision: 1, saved_revision: 1 }), { save: true }));
    fireEvent.change(first.getByLabelText('Startup program'), { target: { value: 'color' } });
    expect(first.getByLabelText('Startup settings (JSON)')).toHaveValue('{}');
    fireEvent.change(first.getByLabelText('Startup program'), { target: { value: '' } });
    const json = JSON.parse((screen.getByLabelText('Controller configuration JSON') as HTMLTextAreaElement).value);
    expect(json.outputs[0]).not.toHaveProperty('startup');
    expect(json.outputs[1]).toEqual(config.outputs[1]);
    expect(screen.queryByLabelText('Bind this output to slot A')).toBeNull();
  });

  it('keeps incomplete arguments visible and blocks submitting them', () => {
    const store = readyStore(), config = hardware(5);
    config.outputs[0].startup = { program: 'rainbow', settings: { duration: 3.3 } };
    store.dispatch(controllerActions.configuration({ config, revision: 1, savedRevision: 1 }));
    render(<Provider store={store}><Configure /></Provider>);
    const args = screen.getByLabelText('Startup settings (JSON)');
    for (const value of ['{', 'null', '[]', '{"duration":"fast"}']) {
      fireEvent.change(args, { target: { value } });
      expect(args).toHaveValue(value);
      expect(args).toHaveAttribute('aria-invalid', 'true');
      expect(screen.getByRole('button', { name: 'Validate' })).toBeDisabled();
      expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    }
    act(() => { store.dispatch(controllerActions.configuration({ config: hardware(12), revision: 2, savedRevision: 2 })); });
    expect(args).toHaveValue('{"duration":"fast"}');
    fireEvent.change(args, { target: { value: '{}' } });
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load controller values' }));
    expect(screen.queryByLabelText('Startup settings (JSON)')).toBeNull();
    expect(screen.getByRole('button', { name: 'Apply & save for reboot' })).toBeEnabled();
  });

  it('synchronizes full JSON edits and preserves other outputs when removing an output', () => {
    const store = readyStore(), config = hardware(5);
    config.outputs[0].startup = { program: 'rainbow', settings: { duration: 3.3 } };
    config.outputs.push({ id: 'desk', type: 'ws2812b', config: { pin: 'GPIO3', count: 8 }, startup: { program: 'rainbow', settings: { duration: 9 } } });
    render(<Provider store={store}><Configure /></Provider>);
    const editor = screen.getByLabelText('Controller configuration JSON') as HTMLTextAreaElement;
    fireEvent.change(editor, { target: { value: JSON.stringify(config) } });
    expect(screen.getAllByLabelText('Startup program').map(node => (node as HTMLSelectElement).value)).toEqual(['rainbow', 'rainbow']);
    const second = within(screen.getByRole('group', { name: 'Output 2' }));
    fireEvent.change(second.getByLabelText('Startup settings (JSON)'), { target: { value: '{' } });
    fireEvent.click(within(screen.getByRole('group', { name: 'Output 1' })).getByRole('button', { name: 'Remove output' }));
    expect(JSON.parse(editor.value).outputs).toEqual([config.outputs[1]]);
    expect(screen.getByLabelText('Startup settings (JSON)')).toHaveValue('{');
    fireEvent.change(screen.getByLabelText('Startup settings (JSON)'), { target: { value: '{"duration": 10}' } });
    fireEvent.click(screen.getByRole('button', { name: 'Add WS2812B output' }));
    const next = JSON.parse(editor.value);
    expect(next.outputs[0].startup.settings).toEqual({ duration: 10 });
    expect(next.outputs[1].id).toBe('lights');
    expect(next.outputs[1]).not.toHaveProperty('startup');
    expect(next).not.toHaveProperty('targets');
    fireEvent.change(editor, { target: { value: JSON.stringify({ ...config, targets: {} }) } });
    expect(screen.getByRole('button', { name: 'Apply in RAM' })).toBeDisabled();
    expect(screen.getByRole('alert')).toHaveTextContent('Startup belongs inside each output');
  });

  it('keeps a network draft and blocks saving when its revision changes', () => {
    const store = readyStore();
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    const port = screen.getByLabelText('HTTP port') as HTMLInputElement;
    fireEvent.change(port, { target: { value: '8080' } });
    act(() => { store.dispatch(controllerActions.network({ config: network(9090), revision: 2 })); });
    expect(port).toHaveValue(8080);
    expect(screen.getByRole('alert')).toHaveTextContent('Network settings changed');
    expect(screen.getByRole('button', { name: 'Save & reconnect network' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Load controller values' }));
    expect(port).toHaveValue(9090);
    expect(screen.getByRole('button', { name: 'Save & reconnect network' })).toBeEnabled();
  });
  it('retains station and AP drafts across mode changes and clears attempted passwords', async () => {
    const store = readyStore(), config = network(80);
    config.wifi.networks = [{ ssid: 'studio' }];
    config.wifi.ap = { ssid: 'lights' };
    store.dispatch(controllerActions.network({ config, revision: 1 }));
    const save = vi.spyOn(runtime.controllers, 'configureNetwork').mockResolvedValue(false);
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
    store.dispatch(controllerActions.description({ ...description, wifi_ap_supported: undefined }));
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    expect(screen.queryByLabelText('Wi-Fi mode')).toBeNull();
    expect(screen.queryByLabelText('Access point SSID')).toBeNull();
  });
  it('shows saved settings and the next-boot save action during recovery', () => {
    const store = readyStore();
    store.dispatch(controllerActions.networkStatus({ state: 'ap_active', mode: 'ap', recovery: true, ssid: 'lightvm-123456', ip: '192.168.4.1', http_port: 80, revision: 1 }));
    render(<Provider store={store}><Configure /></Provider>);
    fireEvent.click(screen.getByRole('button', { name: 'Wi-Fi & HTTP' }));
    expect(screen.getByRole('status')).toHaveTextContent('saved network settings');
    expect(screen.getByRole('button', { name: 'Save network for next boot' })).toBeEnabled();
    expect(screen.getByLabelText('Wi-Fi mode')).toHaveValue('station');
  });
});
