import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { DeviceService } from './device-service';
import { RuntimeContext } from './runtime';
import { createAppStore, deviceActions } from './store';
import { TransitionControls } from './Transitions';
import type { LightVMClient } from '../protocol.mjs';
import type { Description, TransitionState } from './types';

afterEach(() => { cleanup(); vi.useRealTimers(); vi.restoreAllMocks(); });

function setup() {
  const store = createAppStore(), devices = new DeviceService(store), compiler = new Compiler(store);
  const description = { device_id: 'controller', boot_id: 'boot', board: 'simulator', ab_transitions: true,
    transition_programs: true } as Description;
  let state = { revision: 1, active: true, progress: 35, effect: 'program', procedure: 'crossfade', seed: 0,
    driver: { kind: 'manual', assignment: 1 }, A: { active: true, layers: [{ id: 'base', procedure: 'rainbow' }] }, B: { active: false }, error: '' } as TransitionState;
  const client = {
    closed: false, transport: { label: 'HTTP' }, transition: vi.fn(async () => state),
    setProgress: vi.fn(async (progress: number) => { state = { ...state, progress, driver: { kind: 'manual', assignment: 3 } }; }),
    runTransition: vi.fn(async () => {}), holdTransition: vi.fn(async () => {}),
    activatePreset: vi.fn(async () => {}), selectTransition: vi.fn(async () => {}),
    assignTransitionSource: vi.fn(async (source: string) => { state = { ...state, driver: { kind: 'source', source, assignment: 7 } }; }),
    updateTransitionSource: vi.fn(async () => true),
  };
  devices.paths.attach('http', client as unknown as LightVMClient, description); devices.syncPaths(description);
  store.dispatch(deviceActions.configuration({ config: { schema: 'lightvm.config', devices: [
    { id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 67 } },
  ] }, revision: 1, savedRevision: 0 }));
  store.dispatch(deviceActions.presets([{ name: 'stars', size: 100, crc32: 1 }]));
  store.dispatch(deviceActions.procedures([
    { name: 'crossfade', size: 474, crc32: 1, profile: 'transition' },
    { name: 'wipe', size: 751, crc32: 2, profile: 'transition' },
    { name: 'rainbow', size: 1000, crc32: 3, profile: 'animation' },
  ]));
  render(<Provider store={store}><RuntimeContext.Provider value={{ compiler, devices }}><TransitionControls /></RuntimeContext.Provider></Provider>);
  return { client, store };
}

it('prepares B and sends cuts, timed runs and Hold to the selected output', async () => {
  const { client } = setup();
  await screen.findByText('1 layer ready');
  fireEvent.change(screen.getByLabelText('Scene B'), { target: { value: 'stars' } });
  fireEvent.click(screen.getByRole('button', { name: 'Load B' }));
  await waitFor(() => expect(client.activatePreset).toHaveBeenCalledWith('stars', 'lights', 'B'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Cut to A' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Cut to A' }));
  await waitFor(() => expect(client.setProgress).toHaveBeenCalledWith(0, 'lights'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Run to B' })).toBeEnabled());
  fireEvent.change(screen.getByLabelText('Duration (ms)'), { target: { value: '1200' } });
  fireEvent.click(screen.getByRole('button', { name: 'Run to B' }));
  await waitFor(() => expect(client.runTransition).toHaveBeenCalledWith(100, 1200, 'lights'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Hold' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Hold' }));
  await waitFor(() => expect(client.holdTransition).toHaveBeenCalledWith('lights'));
});

it('uses the current external assignment token for source updates', async () => {
  const { client } = setup();
  await screen.findByText('1 layer ready');
  fireEvent.click(screen.getByRole('button', { name: 'Assign source' }));
  await waitFor(() => expect(client.assignTransitionSource).toHaveBeenCalledWith('slider', 'lights'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Send source value' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Send source value' }));
  await waitFor(() => expect(client.updateTransitionSource).toHaveBeenCalledWith('slider', 7, 35, 'lights'));
});

it('coalesces slider input and preserves an effect draft across polling', async () => {
  const { client } = setup();
  await screen.findByText('1 layer ready'); vi.useFakeTimers();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(500); });
  expect(screen.getByLabelText('Transition program')).toHaveValue('wipe');
  fireEvent.change(screen.getByLabelText(/Progress ·/), { target: { value: '25' } });
  fireEvent.change(screen.getByLabelText(/Progress ·/), { target: { value: '75' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(100); });
  expect(client.setProgress).toHaveBeenCalledTimes(1);
  expect(client.setProgress).toHaveBeenCalledWith(75, 'lights');
});

it('recalls a stored Python transition by name and excludes animation programs', async () => {
  const { client } = setup();
  await screen.findByText('1 layer ready');
  expect(screen.queryByRole('option', { name: 'rainbow' })).not.toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('Transition program'), { target: { value: 'wipe' } });
  fireEvent.click(screen.getByRole('button', { name: 'Apply transition' }));
  await waitFor(() => expect(client.selectTransition).toHaveBeenCalledWith('wipe', 'lights'));
});

it('Hold cancels an unsent slider update', async () => {
  const { client } = setup();
  await screen.findByText('1 layer ready'); vi.useFakeTimers();
  fireEvent.change(screen.getByLabelText(/Progress ·/), { target: { value: '75' } });
  fireEvent.click(screen.getByRole('button', { name: 'Hold' }));
  await act(async () => { await vi.advanceTimersByTimeAsync(100); });
  expect(client.holdTransition).toHaveBeenCalledWith('lights');
  expect(client.setProgress).not.toHaveBeenCalled();
});
