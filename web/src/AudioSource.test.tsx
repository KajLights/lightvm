import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, describe, expect, it, vi } from 'vitest';
import { AudioSource } from './AudioSource';
import { Compiler } from './compiler';
import { ControllerService } from './controller-service';
import { RuntimeContext } from './runtime';
import { createAppStore } from './store';
import { SILENCE } from './audio-features';
import type { Description } from './types';
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
  const client = { closed: false, transport: { label: 'USB' }, manage: vi.fn(async () => {}),
    json: vi.fn(async () => ({ boot_id: 'boot', generation: 9 })) };
  if (connected) {
    const description = { controller_id: 'pico', boot_id: 'boot', live_inputs: true } as unknown as Description;
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
  }
  const view = render(<Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, controllers }}><AudioSource /></RuntimeContext.Provider></MemoryRouter></Provider>);
  return { ...view, store, client };
}
describe('music source workspace', () => {
  it('monitors audio without a controller', async () => {
    workspace(); expect(screen.getByRole('button', { name: 'Send music to controller' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' })); await screen.findByText(/Listening · local audio/);
    act(() => microphone.frame({ ...SILENCE, level: 0.7 }));
    expect(screen.getByLabelText('Level')).toHaveAttribute('value', '0.7');
    fireEvent.click(screen.getByRole('button', { name: 'Stop listening' })); expect(microphone.stop).toHaveBeenCalled();
  });
  it('reports microphone denial and permits retry', async () => {
    microphone.start.mockRejectedValueOnce(new Error('Permission denied'));workspace();
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' }));
    expect(await screen.findByRole('alert')).toHaveTextContent('Permission denied');
    expect(screen.getByRole('button', { name: 'Start listening' })).toBeEnabled();
  });
  it('publishes music without an active animation and leaves wiring alone on release', async () => {
    const { client, unmount } = workspace(true);
    fireEvent.click(screen.getByRole('button', { name: 'Start listening' }));await screen.findByText(/Listening · local audio/);
    act(() => microphone.frame({ ...SILENCE, level: 0.7, bass: 1 }));
    fireEvent.click(screen.getByRole('button', { name: 'Send music to controller' }));
    await waitFor(() => expect(client.manage).toHaveBeenCalledWith('input_publish', expect.objectContaining({ values: expect.objectContaining({ 'music.level': 0.7, 'music.bass': 1 }) })));
    fireEvent.click(screen.getByRole('button', { name: 'Stop sending' }));
    expect(client.json).toHaveBeenCalledTimes(1);expect(microphone.stop).not.toHaveBeenCalled();
    unmount();expect(microphone.stop).toHaveBeenCalled();
  });
});
