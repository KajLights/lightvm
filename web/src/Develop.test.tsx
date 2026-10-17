import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { DeviceService } from './device-service';
import { Develop } from './Develop';
import { RuntimeContext } from './runtime';
import { createAppStore, deviceActions } from './store';
import type { Description, ParameterState } from './types';
import type { LightVMClient } from '../protocol.mjs';
import originalRainbow from '../../examples/animations/rainbow.py?raw';

afterEach(() => { cleanup(); localStorage.clear(); vi.restoreAllMocks(); });

function workspace() {
  const store = createAppStore();
  const compiler = new Compiler(store), devices = new DeviceService(store);
  vi.spyOn(compiler, 'start').mockImplementation(() => {});
  return { store, compiler, devices, show: () => render(
    <Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, devices }}>
      <Develop active />
    </RuntimeContext.Provider></MemoryRouter></Provider>,
  ) };
}

describe('editor examples and playback status', () => {
  it('identifies the default and reloads the single Rainbow example from its canonical source', () => {
    const { show, store } = workspace(); show();
    expect(document.querySelector('#filename')).toHaveTextContent('rainbow.py');
    expect(screen.getByLabelText('Load example')).toHaveValue('rainbow');
    expect(screen.getByLabelText('Animation Python source')).toHaveValue(originalRainbow);
    expect(document.querySelector('#example-description')).toHaveTextContent('60 seconds');

    expect(screen.getAllByRole('option', { name: 'rainbow.py' })).toHaveLength(1);
    expect(screen.getByRole('option', { name: 'rainbow_sprite.py' })).toBeInTheDocument();

    fireEvent.change(screen.getByLabelText('Animation Python source'), { target: { value: originalRainbow + '\n# custom edit' } });
    expect(screen.getByLabelText('Load example')).toHaveValue('');
    expect(document.querySelector('#example-description')).toBeNull();

    fireEvent.change(screen.getByLabelText('Load example'), { target: { value: 'rainbow' } });
    expect(screen.getByLabelText('Animation Python source')).toHaveValue(originalRainbow);
    expect(store.getState().editor.source).toBe(originalRainbow);
  });

  it('polls renders and pause without discarding unsaved inputs, then loads inputs for a new instance', async () => {
    const { store, devices, show } = workspace();
    const description = { device_id: 'pico', boot_id: 'boot', board: 'pico_w', active_revision: 0,
      saved_revision: 0, catalog_revision: 0, network_revision: 0 } as Description;
    const running: ParameterState = { generation: 1, parameter_revision: 0, slot: 'A', paused: false,
      faulted: false, render_count: 0, values: {}, settings_values: { duration: 60 },
      manifest: { abi: 4, parameters: [], settings: [{ name: 'duration', type: 'float' }] } };
    const response = vi.fn().mockResolvedValue({ ...running, render_count: 3 });
    const client = { closed: false, transport: { label: 'HTTP' }, describe: vi.fn().mockResolvedValue(description), json: response } as unknown as LightVMClient;
    devices.paths.attach('http', client, description); devices.syncPaths(description);
    store.dispatch(deviceActions.parameters(running)); show();
    fireEvent.change(screen.getByLabelText('duration'), { target: { value: '3' } });

    await act(async () => { await devices.poll(); });
    expect(response).toHaveBeenCalledWith('program_parameters_get');
    expect(document.querySelector('#parameter-state')).toHaveTextContent('Running');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, render_count: 3, paused: true });
    await act(async () => { await devices.poll(); });
    expect(document.querySelector('#parameter-state')).toHaveTextContent('Paused');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeDisabled();
    expect(screen.getByRole('button', { name: /^Resume$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, generation: 2, values: {}, settings_values: { duration: 20 } });
    await act(async () => { await devices.poll(); });
    expect(screen.getByLabelText('duration')).toHaveValue(20);
    expect(document.querySelector('#parameter-state')).toHaveTextContent('Running');
  });
});
