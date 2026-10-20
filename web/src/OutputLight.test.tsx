import { act, cleanup, fireEvent, render, screen } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import type { LightVMClient } from '../protocol.mjs';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { OutputLight } from './OutputLight';
import { RuntimeContext } from './runtime';
import { controllerActions, createAppStore } from './store';
import type { Description, OutputLightState } from './types';

afterEach(() => { cleanup(); vi.useRealTimers(); });
function setup() {
  vi.useFakeTimers();
  const store = createAppStore(), controllers = new ControllerService(store), compiler = new Compiler(store);
  const description = { controller_id: 'controller', boot_id: 'boot', light_controls: true } as Description;
  let state: OutputLightState = { revision: 1, composition_revision: 9, power: true, brightness: 1, color: 0xffffff, color_control: true, preset: 'eye' };
  let release: (() => void) | undefined;
  const json = vi.fn(async (op: string, fields?: Record<string, unknown>) => {
    if (op === 'output_light_get') return structuredClone(state);
    await new Promise<void>(resolve => { release = resolve; });
    state = { ...state, ...fields, revision: state.revision + 1 } as OutputLightState;
    return structuredClone(state);
  });
  const client = { closed: false, transport: { label: 'USB' }, json };
  controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
  store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 50 } }] }, revision: 1, savedRevision: 1 }));
  const view = render(<Provider store={store}><RuntimeContext.Provider value={{ compiler, controllers }}><OutputLight /></RuntimeContext.Provider></Provider>);
  return { view, json, release: () => release?.() };
}

it('coalesces changes without stale replies reverting another pending control', async () => {
  const { json, release } = setup();
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  fireEvent.change(screen.getByLabelText('Light brightness'), { target: { value: '0.2' } });
  fireEvent.change(screen.getByLabelText('Light brightness'), { target: { value: '0.3' } });
  await act(async () => { await vi.advanceTimersByTimeAsync(60); });
  fireEvent.change(screen.getByLabelText('Light colour'), { target: { value: '#804020' } });
  expect(screen.getByLabelText('Light brightness')).toHaveValue('0.3');
  await act(async () => { release(); await vi.advanceTimersByTimeAsync(60); });
  expect(screen.getByLabelText('Light colour')).toHaveValue('#804020');
  const writes = json.mock.calls.filter(([op]) => op === 'output_light_set');
  expect(writes.map(([, fields]) => fields)).toEqual([
    { output: 'lights', expected_revision: 1, brightness: 0.3 },
    { output: 'lights', expected_revision: 2, color: 0x804020 },
  ]);
  await act(async () => { release(); await vi.advanceTimersByTimeAsync(0); });
  expect(json.mock.calls.every(([op]) => op === 'output_light_get' || op === 'output_light_set')).toBe(true);
});

it('uses composition revision for power and drops unsent edits when leaving', async () => {
  const { json, release, view } = setup();
  await act(async () => { await vi.advanceTimersByTimeAsync(0); });
  fireEvent.click(screen.getByLabelText('Light power'));
  await act(async () => { await vi.advanceTimersByTimeAsync(60); });
  expect(json).toHaveBeenCalledWith('output_light_set', { output: 'lights', expected_revision: 1, expected_composition_revision: 9, power: false });
  fireEvent.change(screen.getByLabelText('Light brightness'), { target: { value: '0.2' } });
  view.unmount();
  await act(async () => { release(); await vi.advanceTimersByTimeAsync(1000); });
  expect(json.mock.calls.filter(([op]) => op === 'output_light_set')).toHaveLength(1);
});
