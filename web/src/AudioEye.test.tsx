import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AudioEye } from './AudioEye';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions } from './store';
import { EYE_INPUTS, SILENCE } from './audio-features';
import type { Description, ProgramState } from './types';
import type { LightVMClient } from '../protocol.mjs';

const microphone = vi.hoisted(() => ({ frame: (_: any) => {}, start: vi.fn(async (_device: string) => {}), stop: vi.fn() }));
vi.mock('./audio-features', async original => ({ ...await original<typeof import('./audio-features')>(),
  MicrophoneAudio: class {
    constructor(frame: (value: any) => void) { microphone.frame = frame; }
    start(device: string) { return microphone.start(device); }
    stop() { microphone.stop(); }
  },
}));
afterEach(async () => { cleanup(); await act(async () => {}); localStorage.clear(); vi.clearAllMocks(); vi.restoreAllMocks(); });

function workspace(connected = false) {
  const store = createAppStore(), compiler = new Compiler(store), controllers = new ControllerService(store);
  const running = { output: 'lights', slot: 'B', layer: 'eye', generation: 7, paused: false, faulted: false,
    inputs_revision: 2, inputs: Object.fromEntries(EYE_INPUTS.map(name => [name, { value: 0, binding: { default: true }, fallback: false }])),
    manifest: { compiler_version: '1.0.0', requires: '1.0.0', schema: 'lightvm.program', profile: 'animation', pixels: { format: 'rgba8-premul' }, settings: [], inputs: EYE_INPUTS.map(name => ({ name, type: 'float', default: 0, minimum: 0, maximum: 1 })) } };
  const client = { closed: false, transport: { label: 'USB' }, manage: vi.fn(async () => {}),
    json: vi.fn(async (op: string, fields: any) => op === 'input_claim' ? { boot_id: 'boot', generation: 9 } : {
      ...running, inputs_revision: 3,
      inputs: Object.fromEntries(EYE_INPUTS.map(name => [name, { value: 0, binding: fields.inputs[name], fallback: false }])),
    }) };
  if (connected) {
    const description = { controller_id: 'pico', boot_id: 'boot', live_inputs: true } as unknown as Description;
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    store.dispatch(controllerActions.programState(running as unknown as ProgramState));
  }
  const view = render(<Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, controllers }}><AudioEye /></RuntimeContext.Provider></MemoryRouter></Provider>);
  return { ...view, store, client };
}
describe('audio Eye workspace', () => {
  it('allows local audio preview without a controller and maps the incoming measurements', async () => {
    workspace();
    expect(screen.getByRole('button', { name: 'Drive running Eye' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' }));
    await screen.findByText(/Listening · local audio/);
    act(() => microphone.frame({ ...SILENCE, level: 0.7, bass: 1 }));
    expect(screen.getByRole('img')).toHaveAccessibleName(/width 0.40, brightness 0.70/);
    fireEvent.change(screen.getByLabelText('Signal for brightness'), { target: { value: 'bass' } });
    expect(screen.getByRole('img')).toHaveAccessibleName(/brightness 1.00/);
    fireEvent.click(screen.getByRole('button', { name: 'Stop listening' }));
    expect(microphone.stop).toHaveBeenCalled(); expect(screen.getByText(/Microphone off/)).toBeInTheDocument();
  });
  it('reports denied microphone permission without getting stuck in startup', async () => {
    microphone.start.mockRejectedValueOnce(new Error('Permission denied'));
    workspace(); fireEvent.click(screen.getByRole('button', { name: 'Start listening' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Permission denied');
    expect(screen.getByRole('button', { name: 'Start listening' })).toBeEnabled();
  });
  it('drives the running Eye instance and restores its original wiring on release', async () => {
    const { client } = workspace(true);
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' })); await screen.findByText(/Listening · local audio/);
    act(() => microphone.frame({ ...SILENCE, level: 0.7, bass: 1 }));
    fireEvent.click(screen.getByRole('button', { name: 'Drive running Eye' }));
    await waitFor(() => expect(screen.getByRole('button', { name: 'Release Eye' })).toBeEnabled());
    expect(client.json).toHaveBeenCalledWith('program_inputs', expect.objectContaining({ output: 'lights', slot: 'B', layer: 'eye', expected_generation: 7, expected_inputs_revision: 2 }));
    await waitFor(() => expect(client.manage).toHaveBeenCalledWith('input_publish', expect.objectContaining({ values: expect.objectContaining({ 'audio.eye.brightness': 0.7, 'audio.eye.width': 0.4 }) })));
    fireEvent.click(screen.getByRole('button', { name: 'Release Eye' }));
    await waitFor(() => expect(client.json).toHaveBeenLastCalledWith('program_inputs', expect.objectContaining({ expected_inputs_revision: 3, inputs: Object.fromEntries(EYE_INPUTS.map(name => [name, { default: true }])) })));
    expect(microphone.stop).not.toHaveBeenCalled();
  });
  it('stops capture and restores wiring when the demo unmounts', async () => {
    const { client, unmount } = workspace(true);
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' })); await screen.findByText(/Listening · local audio/);
    fireEvent.click(screen.getByRole('button', { name: 'Drive running Eye' }));
    await waitFor(() => expect(screen.getByRole('button', { name: 'Release Eye' })).toBeEnabled());
    unmount();
    await waitFor(() => expect(client.json).toHaveBeenCalledTimes(3)); expect(microphone.stop).toHaveBeenCalled();
  });
});

it('tunes a narrow input window and sends the remapped value to the device', async () => {
  const { client } = workspace(true);
  fireEvent.click(screen.getByRole('button', { name: 'Start listening' })); await screen.findByText(/Listening · local audio/);
  fireEvent.change(screen.getByLabelText('brightness input minimum'), { target: { value: '40' } });
  fireEvent.change(screen.getByLabelText('brightness input maximum'), { target: { value: '60' } });
  act(() => microphone.frame({ ...SILENCE, level: 0.5 }));
  expect(screen.getByRole('img')).toHaveAccessibleName(/brightness 0.50/);
  act(() => microphone.frame({ ...SILENCE, level: 0.8 }));
  expect(screen.getByRole('img')).toHaveAccessibleName(/brightness 1.00/);
  fireEvent.click(screen.getByRole('button', { name: 'Drive running Eye' }));
  await waitFor(() => expect(client.manage).toHaveBeenCalledWith('input_publish', expect.objectContaining({ values: expect.objectContaining({ 'audio.eye.brightness': 1 }) })));
  fireEvent.change(screen.getByLabelText('brightness input maximum'), { target: { value: '20' } });
  expect(screen.getByLabelText('brightness input maximum')).toHaveValue(41);
});
