import { expect, it } from 'vitest';
import { parseProgramFile } from './program-file';
it('imports Python as a fresh source file', () => {
  expect(parseProgramFile('# source', 'my animation.py')).toEqual({ schema: 'lightvm.source', filename: 'my animation.py', name: 'my_animation', source: '# source', settings: {}, inputs: {} });
});
it('round trips source, settings and input recipes in an external file', () => {
  const data = { schema: 'lightvm.source', filename: 'eye.py', name: 'eye', source: '# source', settings: { width: 0.25 }, inputs: { position: { value: 0.5 } } };
  expect(parseProgramFile(JSON.stringify(data), 'eye.lightvm.json')).toEqual(data);
});
it('rejects malformed files and oversized source', () => {
  for (const data of [null, {}, { schema: 'lightvm.library' }, { schema: 'lightvm.source', source: 1 }]) expect(() => parseProgramFile(JSON.stringify(data), 'file.json')).toThrow();
  expect(() => parseProgramFile('x'.repeat(65537), 'file.py')).toThrow(/64 KiB/);
});

it('imports native JavaScript and complete blocks workspaces', async () => {
  const { blockExample } = await import('./languages/blocks');
  expect(parseProgramFile('animation({ render(s,r,p) {} });', 'first.js').filename).toBe('first.js');
  const source = blockExample();
  const blocks = parseProgramFile(source, 'first.blocks.json');
  expect(blocks.name).toBe('first'); expect(blocks.source).toBe(source);
  expect(parseProgramFile(JSON.stringify(blocks), 'first.lightvm.json')).toEqual(blocks);
  expect(() => parseProgramFile('{"blocks":{"blocks":[{"type":"unknown"}]}}', 'bad.blocks.json')).toThrow(/Unknown block/);
});
