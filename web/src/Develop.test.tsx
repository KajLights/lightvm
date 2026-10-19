import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react';
import { Provider } from 'react-redux';
import { MemoryRouter } from 'react-router-dom';
import { afterEach, expect, it, vi } from 'vitest';
import { Compiler } from './compiler';
import { ControllerService, download } from './controller-service';
import { Develop } from './Develop';
import { RuntimeContext } from './runtime';
import { createAppStore, controllerActions, editorActions } from './store';
import type { Description } from './types';
import type { LightVMClient } from '../protocol.mjs';
import rainbow from '../../examples/animations/rainbow.py?raw';
import { parseProgramFile } from './program-file';

vi.mock('./controller-service', async original => ({ ...await original<typeof import('./controller-service')>(), download: vi.fn() }));
afterEach(() => { cleanup(); localStorage.clear(); vi.restoreAllMocks(); vi.mocked(download).mockClear(); });
function workspace(profile = 'animation', connected = true) {
  const store = createAppStore(), compiler = new Compiler(store), controllers = new ControllerService(store);
  vi.spyOn(compiler, 'start').mockImplementation(() => {});
  vi.spyOn(controllers, 'readPrograms').mockResolvedValue();
  const client = { closed: false, transport: { label: 'USB' }, saveProgram: vi.fn(async () => {}), deleteProgram: vi.fn(async () => {}),
    selectTransition: vi.fn(async () => {}), runTransition: vi.fn(async () => {}), transition: vi.fn(async () => ({ A: { active: true }, B: { active: true }, progress: 0 })) };
  if (connected) {
    const description = { controller_id: 'pico', boot_id: 'boot', version: '1.0.0', transition_programs: true, module_limit: 32768 } as Description;
    controllers.paths.attach('usb', client as unknown as LightVMClient, description); controllers.syncPaths(description);
    store.dispatch(controllerActions.configuration({ config: { schema: 'lightvm.config', outputs: [{ id: 'lights', type: 'ws2812b', config: { pin: 'GP16', count: 200 } }] }, revision: 1, savedRevision: 1 }));
  }
  const manifest = { compiler_version: '1.0.0', requires: '1.0.0', schema: 'lightvm.program', profile, pixels: { format: 'rgba8-premul' }, settings: [] };
  compiler.artifact = { id: 0, source: rainbow, bytecode: new Uint8Array([1, 2]), manifest, diagnostics: [] };
  store.dispatch(editorActions.compiled({ revision: 0, bytes: 2, manifest, diagnostics: [] }));
  const view = render(<Provider store={store}><MemoryRouter><RuntimeContext.Provider value={{ compiler, controllers }}><Develop active /></RuntimeContext.Provider></MemoryRouter></Provider>);
  return { store, compiler, controllers, client, view };
}
it('uses canonical examples and keeps composition tools out of Editor', () => {
  workspace();
  expect(screen.getByLabelText('Python source')).toHaveValue(rainbow);
  expect(screen.getByLabelText('Load example')).toHaveValue('rainbow');
  expect(screen.queryByText('Animation library')).not.toBeInTheDocument();
  expect(screen.queryByText('Preset setup')).not.toBeInTheDocument();
  expect(screen.getByRole('list', { name: 'Editor workflow' }).textContent).toBe('EditPreviewSave locallyDeploy to device');
});
it('downloads an editable external file without writing a browser library or deploying', () => {
  const { client } = workspace('animation', false);
  fireEvent.click(screen.getByRole('button', { name: /Save locally/ }));
  const [text, filename] = vi.mocked(download).mock.calls[0];
  expect(filename).toBe('rainbow.lightvm.json');
  expect(parseProgramFile(text as string, filename)).toMatchObject({ source: rainbow, filename: 'rainbow.py', settings: {}, inputs: {} });
  expect(localStorage.length).toBe(0); expect(client.saveProgram).not.toHaveBeenCalled();
  expect(screen.getByText('File up to date')).toBeInTheDocument();
  fireEvent.change(screen.getByLabelText('Python source'), { target: { value: '# newer draft' } });
  expect(screen.getByText('Unsaved file changes')).toBeInTheDocument();
  expect(screen.getByRole('button', { name: /Deploy to device/ })).toBeDisabled();
});
it('imports a file with its settings and preserves session edits across renders', async () => {
  const { store } = workspace();
  const data = { schema: 'lightvm.source', filename: 'custom.py', name: 'custom', source: '# imported', settings: { speed: 2 }, inputs: {} };
  const file = { name: 'custom.lightvm.json', size: 300, text: async () => JSON.stringify(data) };
  fireEvent.change(screen.getByLabelText('Import source file'), { target: { files: [file] } });
  await waitFor(() => expect(store.getState().editor.source).toBe('# imported'));
  expect(screen.getByLabelText('Program name')).toHaveValue('custom');
  fireEvent.click(screen.getByRole('button', { name: /Save locally/ }));
  expect(JSON.parse(vi.mocked(download).mock.calls[0][0] as string)).toEqual(data);
});
it('deploys a named transition separately from preview or playback', async () => {
  const { client } = workspace('transition');
  fireEvent.change(screen.getByLabelText('Program name'), { target: { value: 'gentle_fade' } });
  fireEvent.click(screen.getByRole('button', { name: /Deploy to device/ }));
  await waitFor(() => expect(client.saveProgram).toHaveBeenCalledWith('gentle_fade', new Uint8Array([1, 2])));
  expect(client.selectTransition).not.toHaveBeenCalled(); expect(client.runTransition).not.toHaveBeenCalled();
});
it('previews a transition against both prepared buses using the scratch entry', async () => {
  const { client } = workspace('transition');
  fireEvent.click(screen.getByRole('button', { name: /Preview on device/ }));
  await waitFor(() => expect(client.runTransition).toHaveBeenCalledWith(100, 2000, 'lights'));
  expect(client.saveProgram).toHaveBeenCalledWith('_preview_transition', new Uint8Array([1, 2]));
  expect(client.selectTransition).toHaveBeenCalledWith('_preview_transition', 'lights', { settings: {} });
});
it('previews animations without deploying a named program', async () => {
  const { client, controllers } = workspace();
  const upload = vi.spyOn(controllers, 'upload').mockResolvedValue(true);
  fireEvent.click(screen.getByRole('button', { name: /Preview on device/ }));
  await waitFor(() => expect(upload).toHaveBeenCalledWith(new Uint8Array([1, 2]), rainbow, 0, {}, {}));
  expect(client.saveProgram).not.toHaveBeenCalled();
});
it('keeps source downloads available with code errors', () => {
  const { store } = workspace();
  act(() => store.dispatch(editorActions.edited({ source: 'invalid syntax' })));
  expect(screen.getByRole('button', { name: /Save locally/ })).toBeEnabled();
  expect(screen.getByRole('button', { name: /Preview on device/ })).toBeDisabled();
  expect(screen.getByRole('button', { name: /Deploy to device/ })).toBeDisabled();
});
