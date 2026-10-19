import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions } from './store';
import { PresetTransition } from './PresetTransition';
import type { LightVMClient } from '../protocol.mjs';
import type { Description } from './types';
import { deployedTransition, WIPE_SETTINGS } from '../../tests/web-transitions';

afterEach(() => { cleanup(); vi.restoreAllMocks(); });

it('edits an existing controller recipe without changing layers and retains the read revision', async () => {
  const wipe = deployedTransition('wipe', WIPE_SETTINGS), store = createAppStore();
  const controllers = new ControllerService(store), compiler = new Compiler(store);
  const layers = [{ id: 'base', program: 'brush', crc32: 123, settings: { color: 255 }, opacity: 100, blend: 'screen' }];
  const entry = { program: 'wipe', crc32: wipe.program.crc32, duration_ms: 500, settings: { reverse: true } };
  const client = {
    closed: false, transport: { label: 'USB' }, describe: vi.fn(async () => ({ catalog_revision: 4 })),
    json: vi.fn(async () => ({ schema: 'lightvm.preset', layers, entry_transition: entry })),
    getProgram: vi.fn(async () => wipe.bytes), savePreset: vi.fn(async () => ({ value: 5 })),
  };
  controllers.paths.attach('usb', client as unknown as LightVMClient, { controller_id: 'pico' } as Description); controllers.syncPaths({ controller_id: 'pico' } as Description);
  vi.spyOn(controllers, 'readPresets').mockResolvedValue();
  store.dispatch(controllerActions.programs([wipe.program]));
  render(<Provider store={store}><RuntimeContext.Provider value={{ compiler, controllers }}>
    <PresetTransition name="blue" output="lights" busy={false} />
  </RuntimeContext.Provider></Provider>);
  const duration = await screen.findByLabelText('Controller preset transition duration (ms)');
  expect(duration).toHaveValue(500);
  expect(await screen.findByLabelText('reverse')).toHaveValue('true');
  fireEvent.change(duration, { target: { value: '750' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save preset transition' }));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('blue', layers, 'lights', { ...entry, duration_ms: 750 }, 4));
  fireEvent.change(screen.getByLabelText('Controller preset transition'), { target: { value: '' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save preset transition' }));
  await waitFor(() => expect(client.savePreset).toHaveBeenLastCalledWith('blue', layers, 'lights', undefined, 5));
});
