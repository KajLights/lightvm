import { afterEach, expect, it } from 'vitest';
import { createAppStore } from './store';
import { Compiler } from './compiler';
import rainbow from '../../examples/animations/rainbow.py?raw';
afterEach(() => localStorage.clear());
it('keeps editor drafts in memory and ignores abandoned browser libraries', () => {
  localStorage.setItem('lightvm.editor.draft.v1', JSON.stringify({ source: '# obsolete' }));
  const store = createAppStore();
  expect(store.getState().editor.source).toBe(rainbow);
  new Compiler(store).edit('# session draft', 'custom.py');
  expect(store.getState().editor.source).toBe('# session draft');
  expect(createAppStore().getState().editor.source).toBe(rainbow);
  expect(localStorage.getItem('lightvm.editor.draft.v1')).toContain('# obsolete');
});
