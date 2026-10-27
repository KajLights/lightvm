import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, expect, it, vi } from 'vitest';
import { AudioSource } from './AudioSource';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore } from './store';
import { manifest, analysisFrame } from './audio-test-data';
import type { Description } from './types';
import type { LightVMClient } from '../protocol.mjs';
const microphone = vi.hoisted(() => ({ frame: (_: any) => {}, start: vi.fn(async () => {}), stop: vi.fn(async () => {}), reset: vi.fn() }));
vi.mock('./audio-features', async original => ({ ...await original<typeof import('./audio-features')>(),
  loadAudioAssets: async () => ({ manifest, module: {} }),
  MicrophoneAudio: class { inputInfo = {}; constructor(frame: (value: any) => void) { microphone.frame = frame; } start = microphone.start; stop = microphone.stop; reset = microphone.reset; },
}));
afterEach(async () => { cleanup(); await act(async () => {}); localStorage.clear(); vi.clearAllMocks(); });
function workspace(connected = false) {
  const store = createAppStore(), compiler = new Compiler(store), controllers = new ControllerService(store);
  const client = { closed: false, transport: { label: 'USB' }, manage: vi.fn(async () => {}), publishAudio: vi.fn(async () => {}),
    json: vi.fn(async () => ({ boot_id: 'boot', generation: 9, challenge: '0000000000000001' })) };
  if (connected) { const description = { controller_id: 'pico', boot_id: 'boot', live_inputs: true } as unknown as Description;
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description); }
  const view = render(<Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, controllers }}><AudioSource /></RuntimeContext.Provider></MemoryRouter></Provider>);
  return { ...view, store, client };
}
async function listen() { await waitFor(() => expect(screen.getByRole('button', { name: 'Start listening' })).toBeEnabled()); fireEvent.click(screen.getByRole('button', { name: 'Start listening' })); await screen.findByText(/Listening · shared local audio/); }
it('compares two shared profiles locally without acquiring a controller provider', async () => {
  const { client } = workspace(); expect(screen.getByRole('button', { name: 'Send music to controller' })).toBeDisabled(); await listen();
  const one = analysisFrame(), two = analysisFrame(1n, 0.5, 2);
  act(() => microphone.frame({ live: [one, two], published: [one, two] }));
  expect(screen.getByLabelText('Level')).toHaveAttribute('value', '0.7');
  fireEvent.click(screen.getByRole('checkbox', { name: 'Compare both algorithms' }));
  expect(screen.getByRole('region', { name: 'Level rise baseline v1' })).toBeInTheDocument(); expect(client.json).not.toHaveBeenCalled();
  fireEvent.change(screen.getByLabelText('Audio algorithm'), { target: { value: '2' } }); expect(microphone.reset).toHaveBeenCalled();
});
it('reports microphone denial and permits retry', async () => {
  microphone.start.mockRejectedValueOnce(new Error('Permission denied')); workspace();
  await waitFor(() => expect(screen.getByRole('button', { name: 'Start listening' })).toBeEnabled()); fireEvent.click(screen.getByRole('button', { name: 'Start listening' }));
  expect(await screen.findByRole('alert')).toHaveTextContent('Permission denied'); expect(screen.getByRole('button', { name: 'Start listening' })).toBeEnabled();
});
it('sends a named structured provider without changing animation wiring or stopping local capture', async () => {
  const { client, unmount } = workspace(true); await listen(); const one = analysisFrame();
  act(() => microphone.frame({ live: [one], published: [one] })); fireEvent.click(screen.getByRole('button', { name: 'Send music to controller' }));
  await waitFor(() => expect(client.publishAudio).toHaveBeenCalled()); expect(client.json).toHaveBeenCalledExactlyOnceWith('audio_claim', expect.objectContaining({ name: 'music', analysis_profile: 1 }));
  fireEvent.click(screen.getByRole('button', { name: 'Stop sending' })); await waitFor(() => expect(client.manage).toHaveBeenCalledWith('audio_release', expect.anything()));
  expect(microphone.stop).not.toHaveBeenCalled(); unmount(); expect(microphone.stop).toHaveBeenCalled();
});
