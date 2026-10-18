import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { Develop } from './Develop';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions, editorActions } from './store';
import type { Description, ProgramState } from './types';
import type { LightVMClient } from '../protocol.mjs';
import originalRainbow from '../../examples/animations/rainbow.py?raw';
import crossfade from '../../examples/transitions/crossfade.py?raw';

afterEach(() => { cleanup(); localStorage.clear(); vi.restoreAllMocks(); });

function workspace() {
  const store = createAppStore();
  const compiler = new Compiler(store), controllers = new ControllerService(store);
  vi.spyOn(compiler, 'start').mockImplementation(() => {});
  return { store, compiler, controllers, show: () => render(
    <Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, controllers }}>
      <Develop active />
    </RuntimeContext.Provider></MemoryRouter></Provider>,
  ) };
}

describe('editor examples and playback status', () => {
  it('routes playback controls to the reported output, slot and layer', async () => {
    const { store, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0' } as Description;
    const client = { closed: false, transport: { label: 'USB' }, manage: vi.fn(async () => {}) };
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    vi.spyOn(controllers, 'readProgramState').mockResolvedValue();
    store.dispatch(controllerActions.programState({ output: 'second', slot: 'A', layer: 'stars', generation: 7,
      paused: false, faulted: false, manifest: { schema: 'lightvm.program', profile: 'animation', compiler_version: '1.0.0', requires: '1.0.0', pixels: { format: 'rgba8-premul' }, settings: [] } }));
    show();
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Pause' })); });
    expect(client.manage).toHaveBeenCalledWith('program_pause', { output: 'second', slot: 'A', layer: 'stars', expected_generation: 7 });
  });

  it('activates a stored animation on the selected output', async () => {
    const { store, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0', storage_available: true } as Description;
    const client = { closed: false, transport: { label: 'USB' }, manage: vi.fn(async () => {}) };
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    vi.spyOn(controllers, 'readProgramState').mockResolvedValue();
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
      { id: 'first', type: 'ws2812b', config: { pin: 'GP16', count: 5 } },
      { id: 'second', type: 'ws2812b', config: { pin: 'GP17', count: 5 } },
    ] }, revision: 1, savedRevision: 1 }));
    store.dispatch(controllerActions.outputSelected('second'));
    store.dispatch(controllerActions.programs([{ name: 'color', type: 'animation', size: 100, crc32: 1 }]));
    show();
    fireEvent.change(screen.getByLabelText('Stored compiled program'), { target: { value: 'animation:color' } });
    await act(async () => { fireEvent.click(screen.getByRole('button', { name: 'Run stored program' })); });
    expect(client.manage).toHaveBeenCalledWith('program_activate', { name: 'color', output: 'second' });
  });

  it('saves and selects a Python transition under its recallable controller name', async () => {
    const { store, compiler, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0', transition_programs: true,
      module_limit: 32768 } as Description;
    const client = { closed: false, transport: { label: 'HTTP' }, saveProgram: vi.fn(async () => {}),
      selectTransition: vi.fn(async () => {}) };
    controllers.paths.attach('http', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    vi.spyOn(controllers, 'readPrograms').mockResolvedValue();
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
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
    expect(client.saveProgram).toHaveBeenCalledWith('gentle_fade', new Uint8Array([1, 2]));
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
    const description = { controller_id: 'pico', version: '1.1.0' } as Description;
    store.dispatch(controllerActions.connections({ paths: [{ key: 'http', label: 'HTTP' }], selectedPath: 'http', description }));
    store.dispatch(editorActions.compiled({ revision: 0, bytes: 100,
      manifest: { compiler_version: '1.5.0', requires: '1.2', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [] }, diagnostics: [] }));
    show();
    expect(screen.getByText(/Requires LightVM 1.2 within major 1; controller runs 1.1.0/)).toBeInTheDocument();
    expect(document.querySelector('#upload')).toBeDisabled();
    expect(screen.getByLabelText('Animation Python source')).toBeEnabled();
    fireEvent.change(screen.getByLabelText('Animation name'), { target: { value: 'Future animation' } });
    expect(screen.getByRole('button', { name: 'Save animation' })).toBeEnabled();
  });

  it('polls playback without replacing the edited draft settings with a running program', async () => {
    const { store, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', board: 'pico_w', active_revision: 0,
      saved_revision: 0, catalog_revision: 0, network_revision: 0 } as Description;
    const running: ProgramState = { generation: 1, slot: 'A', paused: false,
      faulted: false, render_count: 0, settings_values: { duration: 60 },
      manifest: { compiler_version: '1.0.0', requires: '1.0.0', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [{ name: 'duration', type: 'float' }] } };
    const response = vi.fn().mockResolvedValue({ ...running, render_count: 3 });
    const client = { closed: false, transport: { label: 'HTTP' }, describe: vi.fn().mockResolvedValue(description), json: response } as unknown as LightVMClient;
    controllers.paths.attach('http', client, description); controllers.syncPaths(description);
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
      { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 20 } },
    ] }, revision: 0, savedRevision: 0 }));
    store.dispatch(controllerActions.programState(running));
    store.dispatch(editorActions.compiled({ revision: 0, bytes: 100, manifest: running.manifest, diagnostics: [] })); show();
    fireEvent.click(screen.getByLabelText('Override duration'));
    fireEvent.change(screen.getByLabelText('duration'), { target: { value: '3' } });

    await act(async () => { await controllers.poll(); });
    expect(response).toHaveBeenCalledWith('program_state', { output: 'lights' });
    expect(document.querySelector('#playback-state')).toHaveTextContent('Running');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, render_count: 3, paused: true });
    await act(async () => { await controllers.poll(); });
    expect(document.querySelector('#playback-state')).toHaveTextContent('Paused');
    expect(screen.getByRole('button', { name: /^Pause$/ })).toBeDisabled();
    expect(screen.getByRole('button', { name: /^Resume$/ })).toBeEnabled();
    expect(screen.queryByRole('button', { name: 'Apply parameter values' })).toBeNull();
    expect(screen.getByLabelText('duration')).toHaveValue(3);

    response.mockResolvedValue({ ...running, generation: 2, settings_values: { duration: 20 } });
    await act(async () => { await controllers.poll(); });
    expect(screen.getByLabelText('duration')).toHaveValue(3);
    expect(document.querySelector('#playback-state')).toHaveTextContent('Running');
  });
});

describe('typed controller storage', () => {
  it('recalls controller presets and routes power through the recall API', async () => {
    const { store, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0', storage_available: true } as Description;
    const client = { closed: false, transport: { label: 'USB' }, recallPreset: vi.fn(async () => {}), setPower: vi.fn(async () => {}),
      describe: vi.fn(async () => ({ catalog_revision: 1 })), json: vi.fn(async () => ({ layers: [] })) };
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    vi.spyOn(controllers, 'readProgramState').mockResolvedValue();
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
      { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 5 } },
    ] }, revision: 1, savedRevision: 1 }));
    store.dispatch(controllerActions.presets([{ name: 'blue', size: 100, crc32: 1 }]));
    show();
    fireEvent.change(screen.getByLabelText('Controller preset'), { target: { value: 'blue' } });
    fireEvent.click(screen.getByRole('button', { name: 'Run on output' }));
    await waitFor(() => expect(client.recallPreset).toHaveBeenCalledWith('blue', 'lights'));
    await waitFor(() => expect(screen.getByRole('button', { name: 'Turn off' })).toBeEnabled());
    fireEvent.click(screen.getByRole('button', { name: 'Turn off' }));
    await waitFor(() => expect(client.setPower).toHaveBeenCalledWith(false, 'lights'));
  });

  it('distinguishes same-name programs and deletes only the selected type', async () => {
    const { store, controllers, show } = workspace();
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0',
      module_limit: 32768, storage_bytes: 1048576, free_bytes: 524288 } as Description;
    const client = { closed: false, transport: { label: 'HTTP' }, deleteProgram: vi.fn(async () => {}) };
    controllers.paths.attach('http', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    vi.spyOn(controllers, 'readPrograms').mockResolvedValue();
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
      { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 5 } },
    ] }, revision: 1, savedRevision: 1 }));
    store.dispatch(controllerActions.programs([
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
    expect(client.deleteProgram).toHaveBeenCalledWith('wipe', 'transition');
  });
});
