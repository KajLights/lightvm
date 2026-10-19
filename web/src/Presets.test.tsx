import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import { Presets } from './Presets';
import { RuntimeContext } from './runtime';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { controllerActions, createAppStore } from './store';
import { crc32, type LightVMClient } from '../protocol.mjs';
import type { Description } from './types';
import fixtures from '../../tests/portable_fixtures.h?raw';

afterEach(() => { cleanup(); vi.restoreAllMocks(); });
function setup() {
  const raw = fixtures.split('library_lbc[] = {')[1].split('};')[0];
  const bytes = Uint8Array.from(raw.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value.slice(2), 16));
  const store = createAppStore(), controllers = new ControllerService(store), compiler = new Compiler(store);
  const description = { controller_id: 'test', boot_id: 'boot', presets: true, catalog_revision: 2 } as Description;
  const layers = [{ id: 'main', program: 'sweep', crc32: crc32(bytes), settings: { duration: 2 }, opacity: 255, blend: 'over' }];
  const client = { closed: false, transport: { label: 'USB' }, describe: vi.fn(async () => ({ catalog_revision: 7 })),
    getProgram: vi.fn(async () => bytes), json: vi.fn(async () => ({ layers })), savePreset: vi.fn(async (..._args: unknown[]) => ({ value: 8 })) };
  controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
  vi.spyOn(controllers, 'readPresets').mockResolvedValue();
  store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 20 } }] }, revision: 1, savedRevision: 1 }));
  store.dispatch(controllerActions.programs([{ name: 'sweep', crc32: crc32(bytes), size: bytes.length, type: 'animation' }]));
  store.dispatch(controllerActions.presets([{ name: 'evening', crc32: 1, size: 100 }]));
  render(<Provider store={store}><RuntimeContext.Provider value={{ controllers, compiler }}><Presets /></RuntimeContext.Provider></Provider>);
  return { client, layers, store };
}
it('creates a device preset from deployed animations without browser persistence', async () => {
  const { client } = setup();
  fireEvent.change(screen.getByLabelText('Preset name'), { target: { value: 'new-preset' } });
  fireEvent.click(screen.getByRole('button', { name: '+ Add layer' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  fireEvent.click(screen.getByRole('button', { name: 'Save preset to device' }));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('new-preset', [expect.objectContaining({ program: 'sweep', opacity: 255, blend: 'over', settings: {} })], 'lights', undefined, undefined));
});
it('edits the entire saved recipe with the revision read when opening it', async () => {
  const { client, layers } = setup();
  fireEvent.change(screen.getByLabelText('Preset'), { target: { value: 'evening' } });
  fireEvent.click(screen.getByRole('button', { name: 'Open preset' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  fireEvent.change(screen.getByLabelText('Layer 1 opacity'), { target: { value: '128' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save preset to device' }));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('evening', [{ ...layers[0], opacity: 128 }], 'lights', undefined, 7));
});
it('keeps the draft on a revision conflict', async () => {
  const { client } = setup();
  fireEvent.change(screen.getByLabelText('Preset'), { target: { value: 'evening' } });
  fireEvent.click(screen.getByRole('button', { name: 'Open preset' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  client.savePreset.mockRejectedValueOnce(new Error('Catalog revision changed'));
  fireEvent.change(screen.getByLabelText('Layer 1 opacity'), { target: { value: '100' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save preset to device' }));
  await act(async () => {});
  expect(screen.getByLabelText('Layer 1 opacity')).toHaveValue('100');
  expect(screen.getByText('Unsaved changes')).toBeInTheDocument();
});
it('refuses a replaced program before saving a preset', async () => {
  const { client } = setup();
  fireEvent.change(screen.getByLabelText('Preset name'), { target: { value: 'new-preset' } });
  fireEvent.click(screen.getByRole('button', { name: '+ Add layer' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  client.getProgram.mockResolvedValueOnce(new Uint8Array([1]));
  fireEvent.click(screen.getByRole('button', { name: 'Save preset to device' }));
  await act(async () => {});
  expect(client.savePreset).not.toHaveBeenCalled();
});
