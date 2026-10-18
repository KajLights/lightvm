import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { DeviceService } from './device-service';
import { Develop } from './Develop';
import { RuntimeContext } from './runtime';
import { createAppStore, deviceActions, editorActions } from './store';
import type { Description, ProgramState } from './types';
import type { LightVMClient } from '../protocol.mjs';
import originalRainbow from '../../examples/animations/rainbow.py?raw';
import crossfade from '../../examples/transitions/crossfade.py?raw';

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
  it('saves and selects a Python transition under its recallable device name', async () => {
    const { store, compiler, devices, show } = workspace();
    const description = { device_id: 'pico', boot_id: 'boot', version: '1.0.0', transition_programs: true,
      module_limit: 32768 } as Description;
    const client = { closed: false, transport: { label: 'HTTP' }, saveProcedure: vi.fn(async () => {}),
      selectTransition: vi.fn(async () => {}) };
    devices.paths.attach('http', client as unknown as LightVMClient, description); devices.syncPaths(description);
    vi.spyOn(devices, 'readProcedures').mockResolvedValue();
    store.dispatch(deviceActions.configuration({ config: { schema: 'lightvm.config', devices: [
      { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 200 } },
    ] }, revision: 1, savedRevision: 1 }));
    store.dispatch(editorActions.edited({ source: crossfade, filename: 'crossfade.py' }));
    const revision = store.getState().editor.revision;
    const manifest = { compiler_version: '1.0.0', requires: '1.0.0', schema: 'lightvm.program', profile: 'transition', pixels: { format: 'rgba8-premul' }, settings: [] };
    compiler.artifact = { id: revision, source: crossfade, bytecode: new Uint8Array([1, 2]), manifest, diagnostics: [] };
    store.dispatch(editorActions.compiled({ revision, bytes: 2, manifest, diagnostics: [] }));
    show();
    fireEvent.change(screen.getByLabelText('Stored transition name'), { target: { value: 'gentle_fade' } });
    fireEvent.click(screen.getByRole('button', { name: 'Save and select transition →' }));
    await act(async () => {});
    expect(client.saveProcedure).toHaveBeenCalledWith('gentle_fade', new Uint8Array([1, 2]));
    expect(client.selectTransition).toHaveBeenCalledWith('gentle_fade', 'lights', { settings: {} });
  });
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

  it('explains a newer minimum release while keeping editing and browser saves available', () => {
    const { store, show } = workspace();
    const description = { device_id: 'pico', version: '1.1.0' } as Description;
    store.dispatch(deviceActions.connections({ paths: [{ key: 'http', label: 'HTTP' }], selectedPath: 'http', description }));
    store.dispatch(editorActions.compiled({ revision: 0, bytes: 100,
      manifest: { compiler_version: '1.5.0', requires: '1.2', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [] }, diagnostics: [] }));
    show();
    expect(screen.getByText(/Requires LightVM 1.2 within major 1; device runs 1.1.0/)).toBeInTheDocument();
    expect(document.querySelector('#upload')).toBeDisabled();
    expect(screen.getByLabelText('Animation Python source')).toBeEnabled();
    fireEvent.change(screen.getByLabelText('Animation name'), { target: { value: 'Future animation' } });
    expect(screen.getByRole('button', { name: 'Save animation' })).toBeEnabled();
  });

  it('polls playback without replacing the edited draft settings with a running program', async () => {
    const { store, devices, show } = workspace();
    const description = { device_id: 'pico', boot_id: 'boot', board: 'pico_w', active_revision: 0,
      saved_revision: 0, catalog_revision: 0, network_revision: 0 } as Description;
    const running: ProgramState = { generation: 1, slot: 'A', paused: false,
      faulted: false, render_count: 0, settings_values: { duration: 60 },
      manifest: { compiler_version: '1.0.0', requires: '1.0.0', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [{ name: 'duration', type: 'float' }] } };
    const response = vi.fn().mockResolvedValue({ ...running, render_count: 3 });
    const client = { closed: false, transport: { label: 'HTTP' }, describe: vi.fn().mockResolvedValue(description), json: response } as unknown as LightVMClient;
    devices.paths.attach('http', client, description); devices.syncPaths(description);
    store.dispatch(deviceActions.programState(running));
    store.dispatch(editorActions.compiled({ revision: 0, bytes: 100, manifest: running.manifest, diagnostics: [] })); show();
    fireEvent.click(screen.getByLabelText('Override duration'));
    fireEvent.change(screen.getByLabelText('duration'), { target: { value: '3' } });

    await act(async () => { await devices.poll(); });
    expect(response).toHaveBeenCalledWith('program_state');
    expect(document.querySelector('#playback-state')).toHaveTextContent('Running');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, render_count: 3, paused: true });
    await act(async () => { await devices.poll(); });
    expect(document.querySelector('#playback-state')).toHaveTextContent('Paused');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeDisabled();
    expect(screen.getByRole('button', { name: /^Resume$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, generation: 2, settings_values: { duration: 20 } });
    await act(async () => { await devices.poll(); });
    expect(screen.getByLabelText('duration')).toHaveValue(3);
    expect(document.querySelector('#playback-state')).toHaveTextContent('Running');
  });
});

describe('typed device storage', () => {
  it('distinguishes same-name programs and deletes only the selected type', async () => {
    const { store, devices, show } = workspace();
    const description = { device_id: 'pico', boot_id: 'boot', version: '1.0.0',
      module_limit: 32768, storage_bytes: 1048576, free_bytes: 524288 } as Description;
    const client = { closed: false, transport: { label: 'HTTP' }, deleteProcedure: vi.fn(async () => {}) };
    devices.paths.attach('http', client as unknown as LightVMClient, description); devices.syncPaths(description);
    vi.spyOn(devices, 'readProcedures').mockResolvedValue();
    store.dispatch(deviceActions.configuration({ config: { schema: 'lightvm.config', devices: [
      { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 5 } },
    ] }, revision: 1, savedRevision: 1 }));
    store.dispatch(deviceActions.procedures([
      { name: 'wipe', type: 'animation', profile: 'animation', size: 500, crc32: 1 },
      { name: 'wipe', type: 'transition', profile: 'transition', size: 600, crc32: 2 },
    ]));
    show();
    expect(screen.getByText('512 KiB free of 1024 KiB')).toBeInTheDocument();
    expect(screen.getByRole('option', { name: 'wipe (animation)' })).toHaveValue('animation:wipe');
    expect(screen.getByRole('option', { name: 'wipe (transition)' })).toHaveValue('transition:wipe');
    fireEvent.change(screen.getByLabelText('Stored compiled program'), { target: { value: 'transition:wipe' } });
    fireEvent.click(screen.getByRole('button', { name: 'Delete stored program' }));
    await act(async () => {});
    expect(client.deleteProcedure).toHaveBeenCalledWith('wipe', 'transition');
  });
});
