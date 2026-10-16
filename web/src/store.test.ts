import { afterEach, expect, it, vi } from 'vitest';
import rainbow from '../../examples/animations/rainbow.py?raw';

afterEach(() => { localStorage.clear(); vi.resetModules(); });

it('recognizes a previously saved default without replacing its source', async () => {
  localStorage.setItem('lightvm.editor.draft.v1', JSON.stringify({ filename: 'animation.py', source: rainbow }));
  const { createAppStore } = await import('./store');
  expect(createAppStore().getState().editor).toMatchObject({ filename: 'rainbow.py', source: rainbow });
});

it('preserves a custom animation.py draft', async () => {
  const source = rainbow + '\n# my changes';
  localStorage.setItem('lightvm.editor.draft.v1', JSON.stringify({ filename: 'animation.py', source }));
  const { createAppStore } = await import('./store');
  expect(createAppStore().getState().editor).toMatchObject({ filename: 'animation.py', source });
});
