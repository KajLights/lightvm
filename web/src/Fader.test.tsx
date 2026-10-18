import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { webcrypto } from 'node:crypto';
import { afterEach, beforeEach, expect, it, vi } from 'vitest';
import fixtures from '../../tests/portable_fixtures.h?raw';
import type { LightVMClient } from '../protocol.mjs';
import { crc32 } from '../protocol.mjs';
import { App } from './App';
import { Compiler } from './compiler';
import { DeviceService } from './device-service';
import { RuntimeContext } from './runtime';
import { createAppStore, deviceActions } from './store';
import { FADER_SOURCE } from './transition-fader';
import { ANIMATION_DRAG_TYPE } from './Fader';
import { emptyLibrary, programName, saveAnimation, writeLibrary, type DeviceLayer } from './library';
import type { Compilation, Description, Procedure, TransitionState } from './types';

beforeEach(() => vi.stubGlobal('crypto', webcrypto));
afterEach(() => { cleanup(); localStorage.clear(); vi.useRealTimers(); vi.restoreAllMocks(); vi.unstubAllGlobals(); });

async function savedAnimations() {
  const raw = fixtures.split('library_lbc[] = {')[1].split('};')[0];
  const bytecode = Uint8Array.from(raw.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value.slice(2), 16));
  const size = new DataView(bytecode.buffer).getUint32(76, true);
  const manifest = JSON.parse(new TextDecoder().decode(bytecode.subarray(bytecode.length - size)));
  const compiled: Compilation = { id: 1, source: '# saved animation', bytecode, diagnostics: [], manifest };
  const first = await saveAnimation(emptyLibrary(), compiled, 'ember.py', 'Ember', { duration: 2 });
  const second = await saveAnimation(first.library, compiled, 'aurora.py', 'Aurora', { duration: 4 });
  writeLibrary(second.library);
  return { first: first.animation, second: second.animation, library: second.library, bytecode };
}

function drag(animation: HTMLElement, slot: HTMLElement) {
  const values = new Map<string, string>();
  const dataTransfer = { types: [ANIMATION_DRAG_TYPE], setData: (type: string, value: string) => values.set(type, value),
    getData: (type: string) => values.get(type) ?? '', effectAllowed: '', dropEffect: '' };
  fireEvent.dragStart(animation, { dataTransfer }); fireEvent.dragOver(slot, { dataTransfer });
  expect(slot).toHaveClass('drag-over');
  fireEvent.drop(slot, { dataTransfer }); fireEvent.dragEnd(animation, { dataTransfer });
}

function setup(initialDriver: TransitionState['driver'] = { kind: 'timed', assignment: 1 }) {
  const store = createAppStore(), devices = new DeviceService(store), compiler = new Compiler(store);
  const description = { device_id: 'controller', boot_id: 'boot', board: 'simulator', ab_transitions: true,
    transition_sources: true, transition_programs: true, scenes: true, scene_layer_limit: 8, module_limit: 32768, version: '1.0.0' } as Description;
  let state = { revision: 1, active: true, progress: 35, effect: 'program', procedure: 'crossfade', seed: 0,
    driver: initialDriver, A: { active: true, layers: [{ id: 'base', procedure: 'rainbow' }] },
    B: { active: true, layers: [{ id: 'base', procedure: 'stars' }] }, error: '' } as TransitionState;
  let sequence = 1, generation = 10, procedures: Procedure[] = [];
  const client = {
    closed: false, transport: { label: 'HTTP' }, transition: vi.fn(async () => state),
    describe: vi.fn(async () => description), json: vi.fn(async () => procedures),
    saveProcedure: vi.fn(async (name: string, bytes: Uint8Array) => { procedures = [...procedures, { name, crc32: crc32(bytes), size: bytes.length, profile: 'animation', type: 'animation' }]; }),
    configureScene: vi.fn(async (layers: DeviceLayer[], _output: string, slot: 'A' | 'B') => {
      state = { ...state, revision: state.revision + 1, [slot]: { active: true, layers: layers.map(layer => ({ id: layer.id, procedure: layer.procedure, generation: ++generation })) } };
      return { value: state.revision };
    }),
    setProgress: vi.fn(async (progress: number) => { state = { ...state, progress, driver: { kind: 'manual', assignment: ++sequence } }; return state; }),
    runTransition: vi.fn(async () => { state = { ...state, driver: { kind: 'timed', assignment: ++sequence } }; return state; }),
    holdTransition: vi.fn(async () => { state = { ...state, driver: { kind: 'manual', assignment: ++sequence } }; return state; }),
    assignTransitionSource: vi.fn(async (source: string) => { state = { ...state, driver: { kind: 'source', source, assignment: ++sequence } }; return state; }),
    updateTransitionSource: vi.fn(async (_source: string, _token: number, progress: number) => { state = { ...state, progress }; return true; }),
  };
  devices.paths.attach('http', client as unknown as LightVMClient, description); devices.syncPaths(description);
  store.dispatch(deviceActions.configuration({ config: { schema: 'lightvm.config', devices: [
    { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 67 } },
    { id: 'other', type: 'ws2812b', config: { pin: 'GP17', count: 5 } },
  ] }, revision: 1, savedRevision: 0 }));
  vi.spyOn(devices, 'readProgramState').mockResolvedValue();
  const view = render(<Provider store={store}><MemoryRouter initialEntries={['/devices/controller/fader']}>
    <RuntimeContext.Provider value={{ compiler, devices }}><App /></RuntimeContext.Provider>
  </MemoryRouter></Provider>);
  return { client, store, view, setState: (next: TransitionState) => { state = next; }, getState: () => state,
    setPrograms: (next: Procedure[]) => { procedures = next; store.dispatch(deviceActions.procedures(next)); } };
}

async function claim() {
  await screen.findByText('Timed transition running');
  fireEvent.click(screen.getByRole('button', { name: 'Use fader' }));
  await screen.findByText('Fader controls the transition');
}

it('opens the workspace Fader tab without interrupting the timed driver', async () => {
  const { client } = setup();
  await screen.findByText('Timed transition running');
  expect(screen.getByRole('link', { name: 'Fader' })).toHaveAttribute('aria-current', 'page');
  expect(screen.getByRole('slider', { name: 'Transition fader' })).toHaveValue('35');
  expect(screen.getByRole('slider')).toBeDisabled();
  expect(client.assignTransitionSource).not.toHaveBeenCalled(); expect(client.runTransition).not.toHaveBeenCalled();
  expect(screen.getByText('rainbow')).toBeInTheDocument(); expect(screen.getByText('stars')).toBeInTheDocument();
});

it('takes over at the current position and streams external input while dragging', async () => {
  const { client } = setup(); await claim();
  expect(client.assignTransitionSource).toHaveBeenCalledWith(FADER_SOURCE, 'lights');
  expect(client.setProgress).not.toHaveBeenCalled();
  expect(screen.getByRole('slider')).toHaveValue('35'); vi.useFakeTimers();
  fireEvent.change(screen.getByRole('slider'), { target: { value: '40' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  fireEvent.change(screen.getByRole('slider'), { target: { value: '75' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(33); });
  expect(client.updateTransitionSource.mock.calls).toEqual([[FADER_SOURCE, 2, 40, 'lights'], [FADER_SOURCE, 2, 75, 'lights']]);
  expect(screen.getByRole('slider')).toBeEnabled();
});

it('cancels unsent fader data before Hold or a timed demo', async () => {
  const { client } = setup(); await claim(); vi.useFakeTimers();
  fireEvent.change(screen.getByRole('slider'), { target: { value: '75' } });
  fireEvent.click(screen.getByRole('button', { name: 'Hold' }));
  await act(async () => { await vi.advanceTimersByTimeAsync(100); });
  expect(client.updateTransitionSource).not.toHaveBeenCalled(); expect(client.holdTransition).toHaveBeenCalledWith('lights');
  fireEvent.click(screen.getByRole('button', { name: 'Start timed demo' }));
  await act(async () => {});
  expect(client.setProgress).toHaveBeenCalledWith(0, 'lights');
  expect(client.runTransition).toHaveBeenCalledWith(100, 2000, 'lights', 10000);
  expect(screen.getByRole('slider')).toBeDisabled();
});

it('runs to an endpoint from the current position and validates timed intervals', async () => {
  const { client } = setup(); await screen.findByText('Timed transition running');
  fireEvent.change(screen.getByLabelText('Fade duration (ms)'), { target: { value: '1200' } });
  fireEvent.change(screen.getByLabelText('Repeat every (ms)'), { target: { value: '1000' } });
  expect(screen.getByRole('button', { name: 'Start timed demo' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Run to B' }));
  await waitFor(() => expect(client.runTransition).toHaveBeenCalledWith(100, 1200, 'lights'));
  expect(client.setProgress).not.toHaveBeenCalled();
});

it('releases the slider when another controller replaces its assignment', async () => {
  const workspace = setup(); await claim();
  workspace.setState({ ...workspace.getState(), progress: 65, driver: { kind: 'timed', assignment: 9 } });
  await waitFor(() => expect(screen.getByRole('slider')).toBeDisabled());
  expect(screen.getByRole('slider')).toHaveValue('65');
  expect(workspace.client.assignTransitionSource).toHaveBeenCalledTimes(1);
});

it('does not adopt another fader assignment issued while claiming control', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  workspace.client.assignTransitionSource.mockImplementationOnce(async source => {
    const claimed = { ...workspace.getState(), driver: { kind: 'source' as const, source, assignment: 2 } };
    workspace.setState({ ...claimed, driver: { ...claimed.driver, assignment: 3 } });
    return claimed;
  });
  fireEvent.click(screen.getByRole('button', { name: 'Use fader' }));
  await waitFor(() => expect(workspace.client.transition).toHaveBeenCalledTimes(2));
  expect(screen.getByRole('slider')).toBeDisabled();
});

it('drops pending input on output changes and unmount without stopping playback', async () => {
  const { client, view } = setup(); await claim(); vi.useFakeTimers();
  fireEvent.change(screen.getByRole('slider'), { target: { value: '75' } });
  fireEvent.change(screen.getByLabelText('Output'), { target: { value: 'other' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(100); });
  expect(client.updateTransitionSource).not.toHaveBeenCalled(); expect(client.holdTransition).not.toHaveBeenCalled();
  view.unmount();
  await act(async () => { await vi.advanceTimersByTimeAsync(1000); });
  expect(client.updateTransitionSource).not.toHaveBeenCalled();
});

it('loads a dragged browser animation with saved settings while retaining the fader and the other scene', async () => {
  const { first, bytecode } = await savedAnimations();
  const workspace = setup(); await claim();
  const before = workspace.getState();
  drag(screen.getByRole('article', { name: 'Ember v1' }), screen.getByRole('region', { name: 'Scene B' }));
  await waitFor(() => expect(workspace.client.configureScene).toHaveBeenCalledWith([
    { id: 'main', procedure: programName(first), crc32: crc32(bytecode), settings: { duration: 2 }, opacity: 255, blend: 'over' },
  ], 'lights', 'B'));
  await waitFor(() => expect(screen.getByRole('slider')).toBeEnabled());
  expect(workspace.client.saveProcedure).toHaveBeenCalledExactlyOnceWith(programName(first), bytecode);
  expect(workspace.getState().A).toEqual(before.A);
  expect(workspace.getState().driver).toEqual(before.driver);
  expect(workspace.getState().progress).toBe(35);
  expect(workspace.client.holdTransition).not.toHaveBeenCalled();
  expect(screen.getByRole('region', { name: 'Scene B' })).toHaveTextContent('Ember v1');
});

it('loads the same compiled program with independent saved settings in A and B without uploading it twice', async () => {
  await savedAnimations(); const workspace = setup();
  await screen.findByText('Timed transition running');
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into A' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Load Aurora v1 into B' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Load Aurora v1 into B' }));
  await waitFor(() => expect(workspace.client.configureScene).toHaveBeenCalledTimes(2));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Use fader' })).toBeEnabled());
  expect(workspace.client.configureScene.mock.calls.map(call => [call[0][0].settings, call[2]])).toEqual([[{ duration: 2 }, 'A'], [{ duration: 4 }, 'B']]);
  expect(workspace.client.saveProcedure).toHaveBeenCalledTimes(1);
  expect(screen.getByRole('region', { name: 'Scene A' })).toHaveTextContent('Ember v1');
  expect(screen.getByRole('region', { name: 'Scene B' })).toHaveTextContent('Aurora v1');
  expect(workspace.getState().driver.kind).toBe('timed');
});

it('loads an existing device animation with its defaults and excludes stored transitions', async () => {
  const workspace = setup();
  await act(async () => workspace.setPrograms([
    { name: 'rainbow', size: 500, crc32: 123, type: 'animation', profile: 'animation' },
    { name: 'wipe', size: 300, crc32: 456, type: 'transition', profile: 'transition' },
  ]));
  await screen.findByText('Timed transition running');
  expect(screen.queryByRole('article', { name: 'wipe' })).not.toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Load rainbow into A' }));
  await waitFor(() => expect(workspace.client.configureScene).toHaveBeenCalledWith([
    { id: 'main', procedure: 'rainbow', crc32: 123, settings: {}, opacity: 255, blend: 'over' },
  ], 'lights', 'A'));
  expect(workspace.client.saveProcedure).not.toHaveBeenCalled();
});

it('preserves both loaded scenes and fader ownership when candidate preparation fails', async () => {
  await savedAnimations(); const workspace = setup(); await claim();
  const before = workspace.getState();
  workspace.client.configureScene.mockRejectedValueOnce(new Error('Animation exceeds memory budget'));
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into B' }));
  expect(await screen.findByRole('alert')).toHaveTextContent('Animation exceeds memory budget');
  await waitFor(() => expect(screen.getByRole('slider')).toBeEnabled());
  expect(workspace.getState()).toEqual(before);
  expect(screen.getByText('Fader controls the transition')).toBeInTheDocument();
});

it('ignores unrecognized drop data and does not send a scene mutation', async () => {
  const { client } = setup(); await screen.findByText('Timed transition running');
  fireEvent.drop(screen.getByRole('region', { name: 'Scene A' }), { dataTransfer: {
    types: [ANIMATION_DRAG_TYPE], getData: () => 'browser:missing',
  } });
  expect(client.configureScene).not.toHaveBeenCalled(); expect(client.saveProcedure).not.toHaveBeenCalled();
});

it('does not label a replacement instance with the previously selected saved version', async () => {
  await savedAnimations(); const workspace = setup(); await screen.findByText('Timed transition running');
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into A' }));
  await waitFor(() => expect(screen.getByRole('region', { name: 'Scene A' })).toHaveTextContent('Ember v1'));
  const state = workspace.getState();
  workspace.setState({ ...state, A: { ...state.A, layers: state.A.layers!.map(layer => ({ ...layer, generation: 99 })) } });
  await waitFor(() => expect(screen.getByRole('region', { name: 'Scene A' })).not.toHaveTextContent('Ember v1'));
});
