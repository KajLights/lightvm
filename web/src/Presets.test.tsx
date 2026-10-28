import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { afterEach, expect, it, vi } from 'vitest';
import { Presets } from './Presets';
import { RuntimeContext } from './runtime';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { controllerActions, createAppStore } from './store';
import { crc32, type LightVMClient } from '../protocol.mjs';
import type { AudioProviderStatus, Description, SceneSources } from './types';
import fixtures from '../../tests/portable_fixtures.h?raw';

afterEach(() => { cleanup(); vi.restoreAllMocks(); });
function setup(sources: SceneSources = {}, providers: AudioProviderStatus[] = []) {
  const raw = fixtures.split('library_lbc[] = {')[1].split('};')[0];
  const bytes = Uint8Array.from(raw.match(/0x[0-9a-f]{2}/g)!, value => parseInt(value.slice(2), 16));
  const store = createAppStore(), controllers = new ControllerService(store), compiler = new Compiler(store);
  const description = { controller_id: 'test', boot_id: 'boot', presets: true, catalog_revision: 2, audio_subscriptions_supported: true } as Description;
  const layers = [{ id: 'main', program: 'sweep', crc32: crc32(bytes), settings: { duration: 2 }, opacity: 255, blend: 'over' }];
  const client = { closed: false, transport: { label: 'USB' }, describe: vi.fn(async () => ({ catalog_revision: 7 })),
    getProgram: vi.fn(async () => bytes), json: vi.fn(async (op: string): Promise<any> => op === 'audio_status' ? { providers } : op === 'input_list' ? [] : { layers, sources }), savePreset: vi.fn(async (..._args: unknown[]) => ({ value: 8 })) };
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
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('new-preset', [expect.objectContaining({ program: 'sweep', opacity: 255, blend: 'over', settings: {} })], 'lights', undefined, undefined, {}));
});
it('edits the entire saved recipe with the revision read when opening it', async () => {
  const { client, layers } = setup();
  fireEvent.change(screen.getByLabelText('Preset'), { target: { value: 'evening' } });
  fireEvent.click(screen.getByRole('button', { name: 'Open preset' }));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  fireEvent.change(screen.getByLabelText('Layer 1 opacity'), { target: { value: '128' } });
  fireEvent.click(screen.getByRole('button', { name: 'Save preset to device' }));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('evening', [{ ...layers[0], opacity: 128 }], 'lights', undefined, 7, {}));
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
it('retains source endpoints, pins and policies through an unrelated layer edit', async () => {
  const sources: SceneSources = { room: { type: 'lvaf', address: 'sense.example', port: 50000, source_id: '9000000000000007', analysis_profile: 2, allow_synthetic: true }, desk: { type: 'gateway', name: 'browser_1' } };
  const { client, layers } = setup(sources);
  fireEvent.change(screen.getByLabelText('Preset'), { target: { value: 'evening' } }); fireEvent.click(screen.getByText('Open preset'));
  await waitFor(() => expect(screen.getByRole('button', { name: 'Save preset to device' })).toBeEnabled());
  expect(screen.getByLabelText('Source 1 identity pin')).toHaveValue('9000000000000007');
  fireEvent.change(screen.getByLabelText('Layer 1 opacity'), { target: { value: '128' } }); fireEvent.click(screen.getByText('Save preset to device'));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('evening', [{ ...layers[0], opacity: 128 }], 'lights', undefined, 7, sources));
  expect(client.json.mock.calls.every(([op]) => ['audio_status', 'input_list', 'preset_get'].includes(op))).toBe(true);
});
it('validates a new source and explicitly pins a currently observed gateway identity', async () => {
  const provider = { type: 'gateway', address: 'desk', port: 0, expected_source_id: '0000000000000000', expected_profile: 1, allow_synthetic: false, source_id: '9000000000000007', status: 'connected', profile: 1, references: 1, bands: 16, band_edges: [2, 512], session: '0000000000000008', packets: 5, age_rejections: 0, rtt_us: 300 } satisfies AudioProviderStatus;
  const { client } = setup({}, [provider]);
  fireEvent.change(screen.getByLabelText('Preset name'), { target: { value: 'browser' } }); fireEvent.click(screen.getByText('+ Add layer'));
  fireEvent.click(screen.getByText('+ Add source')); expect(screen.getByText('Save preset to device')).toBeDisabled();
  fireEvent.change(screen.getByLabelText('Source 1 address'), { target: { value: 'sense.local' } }); expect(screen.getByRole('alert')).toHaveTextContent('.local');
  fireEvent.change(screen.getByLabelText('Source 1 provider'), { target: { value: 'gateway' } }); fireEvent.change(screen.getByLabelText('Source 1 gateway name'), { target: { value: 'desk' } });
  await screen.findByText(/connected · profile 1 · 16 spectrum bands/);
  fireEvent.click(screen.getByText('Pin observed identity 9000000000000007'));
  await waitFor(() => expect(screen.getByText('Save preset to device')).toBeEnabled()); fireEvent.click(screen.getByText('Save preset to device'));
  await waitFor(() => expect(client.savePreset).toHaveBeenCalledWith('browser', expect.anything(), 'lights', undefined, undefined, { audio: { type: 'gateway', name: 'desk', source_id: '9000000000000007' } }));
});
