import { afterEach, expect, it } from 'vitest';
import { createAppStore, editorActions, activityActions } from './store';
import type { Manifest } from './types';
import { Compiler } from './compiler';
import rainbow from '../../examples/animations/rainbow.py?raw';
afterEach(() => localStorage.clear());
it('keeps source mapped performance advice separate from upload errors', () => {
  const store = createAppStore();
  const note = { message: 'Move fixed geometry into the sprite init callback.', line: 8, column: 2, blockId: 'sprite-geometry' };
  store.dispatch(editorActions.compiled({ revision: 0, bytes: 100, manifest: {} as Manifest, diagnostics: [], advisories: [note] }));
  expect(store.getState().editor.artifact?.bytes).toBe(100);
  expect(store.getState().editor.diagnostics).toEqual([]);
  expect(store.getState().editor.advisories).toEqual([note]);
  store.dispatch(editorActions.edited({ source: '# changed' }));
  expect(store.getState().editor.advisories).toEqual([]);
});
it('keeps editor drafts in memory and ignores abandoned browser libraries', () => {
  localStorage.setItem('lightvm.editor.draft.v1', JSON.stringify({ source: '# obsolete' }));
  const store = createAppStore();
  expect(store.getState().editor.source).toBe(rainbow);
  new Compiler(store).edit('# session draft', 'custom.py');
  expect(store.getState().editor.source).toBe('# session draft');
  expect(createAppStore().getState().editor.source).toBe(rainbow);
  expect(localStorage.getItem('lightvm.editor.draft.v1')).toContain('# obsolete');
});

it('retains controller-owned operation identity through editor changes', () => {
  const store = createAppStore();
  store.dispatch(activityActions.scene({ boot_id: 'boot', id: 4, output: 'lights', stage: 'verify', units: 3, terminal: false, code: 0, value: 0, error: '', accepted_us: 1, activated_us: 0, first_presented_us: 0, fade_completed_us: 0, finished_us: 0, max_step_us: 0 }));
  store.dispatch(editorActions.edited({ source: '# another draft' }));
  expect(store.getState().activity.scene?.id).toBe(4);
  store.dispatch(activityActions.scene(null));
  expect(store.getState().activity.scene).toBeNull();
});
