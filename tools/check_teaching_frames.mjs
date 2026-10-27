// Replay the published course through the actual browser VM, including starts,
// delayed frames, long clocks, live-input endpoints and intentional mistakes.
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import create from '../build/editor-assets/preview/lightvm-preview.mjs';
const source = stripTypeScriptTypes(readFileSync(new URL('../web/src/preview-engine.ts', import.meta.url), 'utf8'), { mode: 'transform' });
const { PreviewEngine } = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const vm = await create(), engine = new PreviewEngine(vm);
const { cases, counts } = JSON.parse(readFileSync(0, 'utf8'));
const failures = [], observed = new Map(), references = new Map();
let runs = 0, frames = 0, faults = 0, leaks = 0;
function finish() {
  for (let n = 0; n < 20000; n++) { const s = engine.advance(false); if (s.phase >= 2) return s; }
  throw new Error('The work limit did not stop a frame.');
}
for (const item of cases) {
  for (const count of item.variant === 'bug' ? [25] : counts) {
    runs++;
    const framesSeen = [];
    try {
      const artifact = { bytecode: new Uint8Array(item.bytecode), manifest: item.manifest, diagnostics: [] };
      let result = engine.open(artifact, count, 1);
      if (result.error) throw new Error(result.error.message);
      result = finish();
      const record = () => { frames++; if (result.phase === 3) faults++; framesSeen.push([result.phase, Buffer.from(result.pixels).toString('hex')]); };
      record();
      for (const delta of [0, .016, .033, .25, 1, .7]) {
        if (result.phase === 3) break;
        engine.frame(delta); result = finish(); record();
      }
      // Reach exact progress endpoints without a timer or controller.
      for (const progress of [0, 1]) for (const [index, input] of (item.manifest.inputs ?? []).entries()) {
        if (result.phase === 3) break;
        if (input.name === 'progress') { engine.input(index, progress); engine.frame(0); result = finish(); record(); }
      }
      if (result.phase !== 3) { engine.seconds = 1000000; engine.frame(0.25); result = finish(); record(); }
      // The search must terminate after the last available place is filled.
      if (item.lesson.startsWith('10-') && result.phase !== 3) for (let n = 0; n < count + 1; n++) {
        engine.frame(.033); result = finish(); record(); if (result.phase === 3) break;
      }
      const value = JSON.stringify(framesSeen), key = `${item.lesson}/${item.variant}/${count}`;
      if (item.variant !== 'bug' && result.phase !== 2) failures.push(`${item.file}/${count}: ${result.error?.message}`);
      if (observed.has(key) && observed.get(key) !== value) failures.push(`${item.file}/${count}: different frame results across languages or compiler modes`);
      observed.set(key, value);
      if (item.variant === 'solution' && count === 25) references.set(item.lesson, value);
      if (item.variant === 'bug') {
        const lesson = Number(item.lesson.slice(0, 2));
        if ([1, 3, 5, 8, 10, 12].includes(lesson) && result.phase !== 3) failures.push(`${item.file}: expected the deliberate mistake to stop at a runtime error`);
        if ([4, 6, 11].includes(lesson) && (result.phase !== 2 || value === references.get(item.lesson))) failures.push(`${item.file}: expected a visible, non-faulting mistake`);
      }
    } catch (error) { failures.push(`${item.file}/${count}: ${error.message}`); }
    finally { engine.close(); if (vm._preview_heap() !== 0) { leaks++; failures.push(`${item.file}/${count}: preview kept memory after close`); } }
  }
}
process.stdout.write(JSON.stringify({ runs, frames, faults, leaks, counts, deltas: [0, .016, .033, .25, 1, .7], long_clock_seconds: 1000000.25, failures }));
