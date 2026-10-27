// Run the browser adapter in Node with repository-owned test commands.
import { readFileSync } from 'node:fs';
import { stripTypeScriptTypes } from 'node:module';
import create from '../build/editor-assets/preview/lightvm-preview.mjs';
const source = stripTypeScriptTypes(readFileSync(new URL('../web/src/preview-engine.ts', import.meta.url), 'utf8'), { mode: 'transform' });
const { PreviewEngine } = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
const vm = await create(), engine = new PreviewEngine(vm);
const request = JSON.parse(readFileSync(0, 'utf8')), results = [];
request.artifact.bytecode = new Uint8Array(request.artifact.bytecode);
function record(snapshot) {
  results.push({ ...snapshot, pixels: Buffer.from(snapshot.pixels).toString('hex') });
}
function finish() {
  for (let n = 0; n < 10000; n++) { const s = engine.advance(false); if (s.phase >= 2) return s; }
  throw new Error('Preview did not finish inside its work limit.');
}
for (const command of request.commands ?? [{ type: 'open' }, { type: 'finish' }]) {
  if (command.type === 'open') record(engine.open(request.artifact, request.count ?? 25, request.seed ?? 1, request.settings));
  if (command.type === 'finish') record(finish());
  if (command.type === 'frame') { engine.frame(command.delta); record(finish()); }
  if (command.type === 'step') record(engine.advance(true));
  if (command.type === 'input') engine.input(command.index, command.value);
  if (command.type === 'close') { engine.close(); record(engine.snapshot()); }
}
process.stdout.write(JSON.stringify(results));
