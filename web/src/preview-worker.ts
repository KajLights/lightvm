import { PreviewEngine } from './preview-engine';
let engine: PreviewEngine;
let queue = Promise.resolve();
self.onmessage = ({ data }) => {
  queue = queue.then(async () => {
    try {
      if (data.type === 'open') {
        if (!engine) {
          const url = new URL('./preview/lightvm-preview.mjs', data.baseUrl).href;
          const { default: create } = await import(/* @vite-ignore */ url);
          engine = new PreviewEngine(await create());
        }
        const snapshot = engine.open(data.artifact, data.count, data.seed, data.settings);
        if (!snapshot.error) for (const [index, input] of (data.artifact.manifest.inputs ?? []).entries()) {
          const binding = data.inputs?.[input.name];
          if (binding && 'value' in binding) engine.input(index, Number(binding.value));
        }
        self.postMessage({ id: data.id, snapshot: engine.snapshot() });
      } else {
        if (!engine) throw new Error('Start the preview first.');
        if (data.type === 'frame') engine.frame(data.delta);
        if (data.type === 'input') engine.input(data.index, data.value);
        self.postMessage({ id: data.id, snapshot: data.type === 'advance' ? engine.advance(data.step) : engine.snapshot() });
      }
    } catch (error) { self.postMessage({ id: data.id, error: error instanceof Error ? error.message : String(error) }); }
  });
};
