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
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions } from './store';
import { FADER_SOURCE } from './transition-fader';
import { ANIMATION_DRAG_TYPE } from './Fader';
import { emptyLibrary, programName, saveAnimation, writeLibrary, type CompositionLayer } from './library';
import type { Compilation, Description, Program, TransitionState } from './types';
import { deployedTransition, WIPE_SETTINGS } from '../../tests/web-transitions';

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
  const packages = [deployedTransition('crossfade'), deployedTransition('wipe', WIPE_SETTINGS), deployedTransition('wipe_with_colour')];
  const store = createAppStore(), controllers = new ControllerService(store), compiler = new Compiler(store);
  const description = { controller_id: 'controller', boot_id: 'boot', board: 'simulator', ab_transitions: true,
    transition_sources: true, transition_programs: true, compositions: true, composition_layer_limit: 8, module_limit: 32768, version: '1.0.0' } as Description;
  let state = { revision: 1, active: true, progress: 35, effect: 'program', program: 'crossfade', seed: 0,
    driver: initialDriver, A: { active: true, layers: [{ id: 'base', program: 'rainbow' }] },
    B: { active: true, layers: [{ id: 'base', program: 'stars' }] }, error: '' } as TransitionState;
  let sequence = 1, generation = 10, programs: Program[] = packages.map(program => program.program);
  const client = {
    closed: false, transport: { label: 'HTTP' }, transition: vi.fn(async () => state),
    describe: vi.fn(async () => description), json: vi.fn(async (_op: string, options?: { type?: string }) =>
      programs.filter(program => !options?.type || program.type === options.type).map(program => ({ ...program }))),
    getProgram: vi.fn(async (name: string, _type: string) => packages.find(program => program.program.name === name)!.bytes),
    selectTransition: vi.fn(async (program: string, _output: string, _options: unknown) => {
      state = { ...state, program, revision: state.revision + 1, program_crc32: programs.find(item => item.name === program)?.crc32 };
      return state;
    }),
    saveProgram: vi.fn(async (name: string, bytes: Uint8Array) => { programs = [...programs, { name, crc32: crc32(bytes), size: bytes.length, profile: 'animation', type: 'animation' }]; }),
    configureComposition: vi.fn(async (layers: CompositionLayer[], _output: string, slot: 'A' | 'B') => {
      state = { ...state, revision: state.revision + 1, [slot]: { active: true, layers: layers.map(layer => ({ id: layer.id, program: layer.program, generation: ++generation })) } };
      return { value: state.revision };
    }),
    setProgress: vi.fn(async (progress: number) => { state = { ...state, progress, driver: { kind: 'manual', assignment: ++sequence } }; return state; }),
    runTransition: vi.fn(async () => { state = { ...state, driver: { kind: 'timed', assignment: ++sequence } }; return state; }),
    holdTransition: vi.fn(async () => { state = { ...state, driver: { kind: 'manual', assignment: ++sequence } }; return state; }),
    assignTransitionSource: vi.fn(async (source: string) => { state = { ...state, driver: { kind: 'source', source, assignment: ++sequence } }; return state; }),
    updateTransitionSource: vi.fn(async (_source: string, _token: number, progress: number) => { state = { ...state, progress }; return true; }),
  };
  controllers.paths.attach('http', client as unknown as LightVMClient, description); controllers.syncPaths(description);
  store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [
    { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 67 } },
    { id: 'other', type: 'ws2812b', config: { pin: 'GP17', count: 5 } },
  ] }, revision: 1, savedRevision: 0 }));
  store.dispatch(controllerActions.programs(programs));
  vi.spyOn(controllers, 'readProgramState').mockResolvedValue();
  const view = render(<Provider store={store}><MemoryRouter initialEntries={['/controllers/controller/fader']}>
    <RuntimeContext.Provider value={{ compiler, controllers }}><App /></RuntimeContext.Provider>
  </MemoryRouter></Provider>);
  return { client, store, view, setState: (next: TransitionState) => { state = next; }, getState: () => state,
    setPrograms: (next: Program[]) => { programs = next; store.dispatch(controllerActions.programs(next)); } };
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

it('loads a dragged browser animation with saved settings while retaining the fader and the other composition', async () => {
  const { first, bytecode } = await savedAnimations();
  const workspace = setup(); await claim();
  const before = workspace.getState();
  drag(screen.getByRole('article', { name: 'Ember v1' }), screen.getByRole('region', { name: 'Composition B' }));
  await waitFor(() => expect(workspace.client.configureComposition).toHaveBeenCalledWith([
    { id: 'main', program: programName(first), crc32: crc32(bytecode), settings: { duration: 2 }, opacity: 255, blend: 'over' },
  ], 'lights', 'B'));
  await waitFor(() => expect(screen.getByRole('slider')).toBeEnabled());
  expect(workspace.client.saveProgram).toHaveBeenCalledExactlyOnceWith(programName(first), bytecode);
  expect(workspace.getState().A).toEqual(before.A);
  expect(workspace.getState().driver).toEqual(before.driver);
  expect(workspace.getState().progress).toBe(35);
  expect(workspace.client.holdTransition).not.toHaveBeenCalled();
  expect(screen.getByRole('region', { name: 'Composition B' })).toHaveTextContent('Ember v1');
});

it('loads the same compiled program with independent saved settings in A and B without uploading it twice', async () => {
  await savedAnimations(); const workspace = setup();
  await screen.findByText('Timed transition running');
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into A' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Load Aurora v1 into B' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Load Aurora v1 into B' }));
  await waitFor(() => expect(workspace.client.configureComposition).toHaveBeenCalledTimes(2));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Use fader' })).toBeEnabled());
  expect(workspace.client.configureComposition.mock.calls.map(call => [call[0][0].settings, call[2]])).toEqual([[{ duration: 2 }, 'A'], [{ duration: 4 }, 'B']]);
  expect(workspace.client.saveProgram).toHaveBeenCalledTimes(1);
  expect(screen.getByRole('region', { name: 'Composition A' })).toHaveTextContent('Ember v1');
  expect(screen.getByRole('region', { name: 'Composition B' })).toHaveTextContent('Aurora v1');
  expect(workspace.getState().driver.kind).toBe('timed');
});

it('loads an existing controller animation with its defaults and excludes stored transitions', async () => {
  const workspace = setup();
  await act(async () => workspace.setPrograms([
    { name: 'rainbow', size: 500, crc32: 123, type: 'animation', profile: 'animation' },
    { name: 'wipe', size: 300, crc32: 456, type: 'transition', profile: 'transition' },
  ]));
  await screen.findByText('Timed transition running');
  expect(screen.queryByRole('article', { name: 'wipe' })).not.toBeInTheDocument();
  fireEvent.click(screen.getByRole('button', { name: 'Load rainbow into A' }));
  await waitFor(() => expect(workspace.client.configureComposition).toHaveBeenCalledWith([
    { id: 'main', program: 'rainbow', crc32: 123, settings: {}, opacity: 255, blend: 'over' },
  ], 'lights', 'A'));
  expect(workspace.client.saveProgram).not.toHaveBeenCalled();
});

it('preserves both loaded compositions and fader ownership when candidate preparation fails', async () => {
  await savedAnimations(); const workspace = setup(); await claim();
  const before = workspace.getState();
  workspace.client.configureComposition.mockRejectedValueOnce(new Error('Animation exceeds memory budget'));
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into B' }));
  expect(await screen.findByRole('alert')).toHaveTextContent('Animation exceeds memory budget');
  await waitFor(() => expect(screen.getByRole('slider')).toBeEnabled());
  expect(workspace.getState()).toEqual(before);
  expect(screen.getByText('Fader controls the transition')).toBeInTheDocument();
});

it('ignores unrecognized drop data and does not send a composition mutation', async () => {
  const { client } = setup(); await screen.findByText('Timed transition running');
  fireEvent.drop(screen.getByRole('region', { name: 'Composition A' }), { dataTransfer: {
    types: [ANIMATION_DRAG_TYPE], getData: () => 'browser:missing',
  } });
  expect(client.configureComposition).not.toHaveBeenCalled(); expect(client.saveProgram).not.toHaveBeenCalled();
});

it('does not label a replacement instance with the previously selected saved version', async () => {
  await savedAnimations(); const workspace = setup(); await screen.findByText('Timed transition running');
  fireEvent.click(screen.getByRole('button', { name: 'Load Ember v1 into A' }));
  await waitFor(() => expect(screen.getByRole('region', { name: 'Composition A' })).toHaveTextContent('Ember v1'));
  const state = workspace.getState();
  workspace.setState({ ...state, A: { ...state.A, layers: state.A.layers!.map(layer => ({ ...layer, generation: 99 })) } });
  await waitFor(() => expect(screen.getByRole('region', { name: 'Composition A' })).not.toHaveTextContent('Ember v1'));
});

it('chooses deployed transitions without interrupting playback until Apply', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  const before = workspace.getState();
  expect(screen.queryByRole('option', { name: 'rainbow' })).not.toBeInTheDocument();
  expect(screen.getByRole('option', { name: 'wipe_with_colour' })).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe_with_colour' } });
  await waitFor(() => expect(screen.getByRole('button', { name: 'Apply transition' })).toBeEnabled());
  expect(workspace.client.selectTransition).not.toHaveBeenCalled();
  expect(workspace.getState()).toEqual(before);
  fireEvent.click(screen.getByRole('button', { name: 'Apply transition' }));
  await screen.findByText('Active transition: wipe_with_colour');
  expect(workspace.client.selectTransition).toHaveBeenCalledWith('wipe_with_colour', 'lights', { settings: {} });
  expect(workspace.getState()).toMatchObject({ A: before.A, B: before.B, progress: before.progress, driver: before.driver });
  expect(workspace.client.runTransition).not.toHaveBeenCalled();
  expect(workspace.client.holdTransition).not.toHaveBeenCalled();
});

it('applies wipe settings and a separator colour while retaining fader ownership', async () => {
  const workspace = setup(); await claim();
  const before = workspace.getState();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  await screen.findByLabelText('Override separator');
  expect(workspace.client.getProgram).toHaveBeenCalledWith('wipe', 'transition');
  fireEvent.click(screen.getByLabelText('Override separator'));
  fireEvent.change(screen.getByLabelText('separator'), { target: { value: 'true' } });
  fireEvent.click(screen.getByLabelText('Override color'));
  fireEvent.change(screen.getByLabelText('color'), { target: { value: '#ff8800' } });
  fireEvent.click(screen.getByRole('button', { name: 'Apply transition' }));
  await screen.findByText('Active transition: wipe');
  await waitFor(() => expect(screen.getByRole('slider')).toBeEnabled());
  expect(workspace.client.selectTransition).toHaveBeenCalledWith('wipe', 'lights', { settings: { separator: true, color: 0xff8800 } });
  expect(workspace.getState()).toMatchObject({ A: before.A, B: before.B, progress: before.progress, driver: before.driver });
  expect(screen.getByLabelText('color')).toHaveValue('#ff8800');
  expect(screen.getByLabelText('separator')).toHaveValue('true');
  expect(screen.getByLabelText('Override reverse')).not.toBeChecked();
  expect(workspace.client.assignTransitionSource).toHaveBeenCalledTimes(1);
  vi.useFakeTimers();
  fireEvent.change(screen.getByRole('slider'), { target: { value: '75' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(33); });
  expect(workspace.client.updateTransitionSource).toHaveBeenCalledWith(FADER_SOURCE, before.driver.assignment, 75, 'lights');
});

it('keeps the active transition, compositions and fader when transition preparation fails', async () => {
  const workspace = setup(); await claim();
  const before = workspace.getState();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  await waitFor(() => expect(screen.getByRole('button', { name: 'Apply transition' })).toBeEnabled());
  workspace.client.selectTransition.mockRejectedValueOnce(new Error('Transition exceeds memory budget'));
  fireEvent.click(screen.getByRole('button', { name: 'Apply transition' }));
  expect(await screen.findByRole('alert')).toHaveTextContent('Transition exceeds memory budget');
  expect(workspace.getState()).toEqual(before);
  expect(screen.getByRole('slider')).toBeEnabled();
  expect(screen.getByText('Active transition: crossfade')).toBeInTheDocument();
  expect(screen.getByLabelText('Transition program')).toHaveValue('wipe');
});

it('retains transition drafts across polling and follows an externally selected program', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  await screen.findByLabelText('Override reverse');
  fireEvent.click(screen.getByLabelText('Override reverse'));
  fireEvent.change(screen.getByLabelText('reverse'), { target: { value: 'true' } });
  await waitFor(() => expect(workspace.client.transition).toHaveBeenCalledTimes(2));
  expect(screen.getByLabelText('Transition program')).toHaveValue('wipe');
  expect(screen.getByLabelText('reverse')).toHaveValue('true');
  workspace.setState({ ...workspace.getState(), program: 'wipe_with_colour', program_crc32: 5 });
  await waitFor(() => expect(screen.getByLabelText('Transition program')).toHaveValue('wipe_with_colour'));
  expect(screen.queryByLabelText('Override reverse')).not.toBeInTheDocument();
});

it('ignores stale settings reads and refuses a package that differs from the deployed catalogue', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  let complete!: (bytes: Uint8Array<ArrayBuffer>) => void;
  workspace.client.getProgram.mockImplementationOnce(() => new Promise(resolve => { complete = resolve; }));
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  expect(screen.getByRole('button', { name: 'Apply transition' })).toBeDisabled();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe_with_colour' } });
  await waitFor(() => expect(screen.getByRole('button', { name: 'Apply transition' })).toBeEnabled());
  await act(async () => complete(deployedTransition('wipe', WIPE_SETTINGS).bytes));
  expect(screen.queryByLabelText('Override separator')).not.toBeInTheDocument();
  workspace.client.getProgram.mockResolvedValueOnce(deployedTransition('different').bytes);
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  expect(await screen.findByRole('alert')).toHaveTextContent('The stored transition changed');
  expect(screen.getByRole('button', { name: 'Apply transition' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Retry settings' }));
  await screen.findByLabelText('Override separator');
  expect(screen.getByRole('button', { name: 'Apply transition' })).toBeEnabled();
});

it('explains when no transitions have been deployed and disables Apply', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  await act(async () => workspace.setPrograms([]));
  expect(screen.getByText('Save a transition to the controller in Develop to choose it here.')).toBeInTheDocument();
  expect(screen.getByLabelText('Transition program')).toBeDisabled();
  expect(screen.getByRole('button', { name: 'Apply transition' })).toBeDisabled();
});
