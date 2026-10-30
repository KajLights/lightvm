import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { LiveInputs } from './LiveInputs';
import { RuntimeContext } from './runtime';
import { controllerActions, createAppStore } from './store';
import type { Description, InputBindings, ProgramState } from './types';

afterEach(() => { cleanup();vi.useRealTimers(); });
function setup() {
  vi.useFakeTimers();
  const store = createAppStore(), controllers = new ControllerService(store), compiler = new Compiler(store);
  const description = { controller_id: 'controller', boot_id: 'boot', live_inputs: true } as Description;
  let state: ProgramState = { output: 'lights', slot: 'A', generation: 7, paused: false, faulted: false, inputs_revision: 0,
    manifest: { schema: 'lightvm.program', profile: 'animation', compiler_version: '0.1.0', requires: '0.1.0', pixels: { format: 'rgba8-premul' }, settings: [], inputs: [
      { name: 'position', type: 'float', default: 0, minimum: 0, maximum: 1, driver: { kind: 'ramp', duration: 2, mode: 'pingpong' } },
      { name: 'hue', type: 'float', default: 0, minimum: 0, maximum: 1, unit: 'turns' },
      { name: 'brightness', type: 'float', default: 1, minimum: 0, maximum: 1 },
    ] }, inputs: {
      position: { value: 0.25, fallback: false, binding: { driver: { kind: 'ramp', duration: 2, mode: 'pingpong' } } },
      hue: { value: 0, fallback: false, binding: { value: 0 } },brightness: { value: 1, fallback: false, binding: { value: 1 } },
    } };
  const json = vi.fn(async (op: string, fields?: { inputs: InputBindings }) => {
    if (op === 'input_list') return [];
    if (op === 'program_inputs') {
      state = structuredClone(state);state.inputs_revision!++;
      for (const [name, binding] of Object.entries(fields!.inputs)) state.inputs![name].binding = binding;
    }
    return structuredClone(state);
  });
  const client = { closed: false, transport: { label: 'USB' }, json, transition: vi.fn(async () => ({ A: { layers: [] }, B: { layers: [] } })) };
  controllers.paths.attach('usb', client as unknown as LightVMClient, description);controllers.syncPaths(description);
  store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 50 } }] }, revision: 1, savedRevision: 1 }));
  store.dispatch(controllerActions.programState(state));
  const capture = vi.fn();
  const view = render(<Provider store={store}><RuntimeContext.Provider value={{ compiler, controllers }}><LiveInputs onCapture={capture} /></RuntimeContext.Provider></Provider>);
  return { store, json, capture, view };
}

it('takes manual control without a reset and routes to the running generation', async () => {
  const { json, store } = setup();
  fireEvent.change(screen.getByLabelText('hue value'), { target: { value: '0.5' } });
  fireEvent.change(screen.getByLabelText('brightness value'), { target: { value: '0.7' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  expect(json).toHaveBeenCalledWith('program_inputs', { output: 'lights', slot: 'A', layer: undefined, target: undefined, inputs: { hue: { value: 0.5 }, brightness: { value: 0.7 } }, expected_generation: 7, expected_inputs_revision: 0 });
  expect(store.getState().controller.programState?.generation).toBe(7);
  expect(screen.getByLabelText('hue value')).toHaveValue('0.5');
  expect(json.mock.calls.some(([op]) => op === 'program_reset')).toBe(false);
});

it('preserves an edited timer duration across polling and applies it explicitly', async () => {
  const { json } = setup();
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  fireEvent.change(screen.getByLabelText('position timer duration'), { target: { value: '9' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(500); });
  expect(screen.getByLabelText('position timer duration')).toHaveValue(9);
  fireEvent.click(screen.getAllByRole('button', { name: 'Apply timer' })[0]);
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  const sent = json.mock.calls.find(([op]) => op === 'program_inputs')!;
  expect(sent[1]?.inputs.position).toEqual({ driver: { kind: 'ramp', duration: 9, mode: 'pingpong', easing: 'linear', from: 0, to: 1 } });
});

it('copies bindings for saving and drops pending slider input on unmount', async () => {
  const { json, capture, view } = setup();
  fireEvent.click(screen.getByRole('button', { name: 'Use these inputs in draft' }));
  expect(capture.mock.calls[0][0].position.driver.mode).toBe('pingpong');
  fireEvent.change(screen.getByLabelText('hue value'), { target: { value: '0.8' } });
  view.unmount();await act(async () => { await vi.advanceTimersByTimeAsync(100); });
  expect(json.mock.calls.some(([op]) => op === 'program_inputs')).toBe(false);
});
