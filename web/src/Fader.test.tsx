import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { crc32 } from '../protocol.mjs';
import { App } from './App';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions } from './store';
import { FADER_SOURCE } from './transition-fader';
import { ANIMATION_DRAG_TYPE } from './Fader';
import { type CompositionLayer } from './program';
import type { Description, Program, TransitionState } from './types';
import { deployedTransition, WIPE_SETTINGS } from '../../tests/web-transitions';

afterEach(() => { cleanup(); localStorage.clear(); vi.useRealTimers(); vi.restoreAllMocks(); vi.unstubAllGlobals(); });

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
    activatePreset: vi.fn(async (name: string, _output: string, slot: 'A' | 'B') => { state = { ...state, revision: state.revision + 1, [slot]: { active: true, preset: name, layers: [{ id: 'main', program: 'rainbow', generation: ++generation }] } }; return { value: state.revision }; }),
    swapTransition: vi.fn(async () => { state = { ...state, A: state.B, B: state.A, progress: 100 - state.progress }; return state; }),
    setPower: vi.fn(async () => state),
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
  const view = render(<Provider store={store}><MemoryRouter initialEntries={['/controllers/controller/mixer']}>
    <RuntimeContext.Provider value={{ compiler, controllers }}><App /></RuntimeContext.Provider>
  </MemoryRouter></Provider>);
  document.querySelectorAll('details').forEach(element => { element.open = true; });
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
  expect(screen.queryByLabelText('Transition program')).not.toBeInTheDocument();
  await screen.findByText('Timed transition running');
  expect(screen.getByLabelText('Transition program')).toHaveValue('crossfade');
  expect(screen.getByRole('link', { name: 'Mixer' })).toHaveAttribute('aria-current', 'page');
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
  fireEvent.click(screen.getByRole('button', { name: 'Start timed loop' }));
  await act(async () => {});
  expect(client.setProgress).toHaveBeenCalledWith(0, 'lights');
  expect(client.runTransition).toHaveBeenCalledWith(100, 2000, 'lights', 10000);
  expect(screen.getByRole('slider')).toBeDisabled();
});

it('runs to an endpoint from the current position and validates timed intervals', async () => {
  const { client } = setup(); await screen.findByText('Timed transition running');
  fireEvent.change(screen.getByLabelText('Fade duration (ms)'), { target: { value: '1200' } });
  fireEvent.change(screen.getByLabelText('Repeat every (ms)'), { target: { value: '1000' } });
  expect(screen.getByRole('button', { name: 'Start timed loop' })).toBeDisabled();
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

it('ignores unrecognized drop data and does not send a composition mutation', async () => {
  const { client } = setup(); await screen.findByText('Timed transition running');
  fireEvent.drop(screen.getByRole('region', { name: 'Composition A' }), { dataTransfer: {
    types: [ANIMATION_DRAG_TYPE], getData: () => 'browser:missing',
  } });
  expect(client.configureComposition).not.toHaveBeenCalled(); expect(client.saveProgram).not.toHaveBeenCalled();
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
  vi.useFakeTimers();
  let workspace!: ReturnType<typeof setup>;
  await act(async () => { workspace = setup(); });
  expect(screen.getByLabelText('Transition program')).toHaveValue('crossfade');
  await act(async () => {
    fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  });
  fireEvent.click(screen.getByLabelText('Override reverse'));
  fireEvent.change(screen.getByLabelText('reverse'), { target: { value: 'true' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(500); });
  expect(workspace.client.transition).toHaveBeenCalledTimes(2);
  expect(screen.getByLabelText('Transition program')).toHaveValue('wipe');
  expect(screen.getByLabelText('reverse')).toHaveValue('true');
  workspace.setState({ ...workspace.getState(), program: 'wipe_with_colour', program_crc32: 5 });
  await act(async () => { await vi.advanceTimersByTimeAsync(500); });
  expect(screen.getByLabelText('Transition program')).toHaveValue('wipe_with_colour');
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
  expect(screen.getByText('Deploy a transition in Editor to choose it here.')).toBeInTheDocument();
  expect(screen.getByLabelText('Transition program')).toBeDisabled();
  expect(screen.getByRole('button', { name: 'Apply transition' })).toBeDisabled();
});


it('cues a device preset into B without moving the fader or disturbing A', async () => {
  const workspace = setup(); await claim();
  act(() => workspace.store.dispatch(controllerActions.presets([{ name: 'Evening', size: 100, crc32: 1 }])));
  const before = workspace.getState();
  fireEvent.click(screen.getByRole('button', { name: 'Load Evening into B' }));
  await waitFor(() => expect(workspace.client.activatePreset).toHaveBeenCalledWith('Evening', 'lights', 'B'));
  expect(workspace.getState()).toMatchObject({ A: before.A, progress: before.progress, driver: before.driver });
  expect(screen.getByRole('slider', { name: 'Transition fader' })).toBeEnabled();
});

it('keeps both buses intact when preset loading fails', async () => {
  const workspace = setup(); await claim();
  act(() => workspace.store.dispatch(controllerActions.presets([{ name: 'TooBig', size: 100, crc32: 1 }])));
  const before = workspace.getState(); workspace.client.activatePreset.mockRejectedValueOnce(new Error('Memory budget exceeded'));
  fireEvent.click(screen.getByRole('button', { name: 'Load TooBig into B' }));
  expect(await screen.findByRole('alert')).toHaveTextContent('Memory budget exceeded');
  expect(workspace.getState()).toEqual(before);
  expect(screen.getByRole('slider', { name: 'Transition fader' })).toBeEnabled();
});

it('cuts and swaps only at endpoints and routes power to the selected output', async () => {
  const workspace = setup(); await screen.findByText('Timed transition running');
  expect(screen.getByRole('button', { name: 'Swap A and B' })).toBeDisabled();
  fireEvent.click(screen.getByRole('button', { name: 'Cut to B' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Swap A and B' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Swap A and B' }));
  await waitFor(() => expect(workspace.client.swapTransition).toHaveBeenCalledWith('lights'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Fade to black' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Fade to black' }));
  await waitFor(() => expect(workspace.client.setPower).toHaveBeenCalledWith(false, 'lights'));
});

it('loads a dragged device animation while retaining the other bus and manual assignment', async () => {
  const workspace = setup(); await claim();
  act(() => workspace.setPrograms([{ name: 'color', crc32: 123, size: 100, type: 'animation' }]));
  const before = workspace.getState();
  drag(screen.getByRole('article', { name: 'color' }), screen.getByRole('region', { name: 'Composition B' }));
  await waitFor(() => expect(workspace.client.configureComposition).toHaveBeenCalledWith([
    { id: 'main', program: 'color', crc32: 123, settings: {}, opacity: 255, blend: 'over' },
  ], 'lights', 'B'));
  expect(workspace.getState()).toMatchObject({ A: before.A, progress: before.progress, driver: before.driver });
  expect(workspace.client.saveProgram).not.toHaveBeenCalled();
});
