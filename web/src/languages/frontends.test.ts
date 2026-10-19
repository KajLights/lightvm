// @vitest-environment node
import { execFileSync } from 'node:child_process';
import { readFileSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { lowerJavaScript } from './javascript';
import { blockExample, lowerBlocks, parseBlocks } from './blocks';
import type { LoweredSource } from './source';
const root = fileURLToPath(new URL('../../..', import.meta.url));
function compile(lowered: LoweredSource) {
  const result = JSON.parse(execFileSync(process.env.LIGHTVM_PYTHON || 'python3', ['-c', `
import sys, json
from dataclasses import asdict
from lightvm.compiler import compile_source
r = compile_source(sys.stdin.read())
print(json.dumps({'diagnostics': [asdict(d) for d in r.diagnostics], 'manifest': r.manifest, 'bytes': len(r.bytecode)}))
`], { cwd: root, input: lowered.python, encoding: 'utf8' }));
  expect(result.diagnostics, lowered.python).toEqual([]); expect(result.bytes).toBeGreaterThan(96); return result;
}
const render = (body: string) => `export function render(state, runtime, pixels) { ${body} }`;
describe('JavaScript to the shared LightVM backend', () => {
  for (const name of readdirSync(root + '/examples/javascript').filter(name => name.endsWith('.js'))) {
    it('compiles example ' + name, () => { compile(lowerJavaScript(readFileSync(root + '/examples/javascript/' + name, 'utf8'))); });
  }
  it('supports lexical locals, helpers, for/while, nested choices and updates', () => {
    compile(lowerJavaScript(`
const twice = x => x * 2;
export function render(state, runtime, pixels) {
  let total = 0;
  for (let i = 1; i < pixels.count; i += 2) {
    if (i === 3) continue;
    if (i > 9) break;
    total += twice(i);
  }
  let n = 3;
  while (n > 0) { total += n; n--; }
  pixels.fill(rgb(total, 0, 0));
}`));
  });
  it('supports settings, typed arrays, per-instance state, imports and for-of', () => {
    const result = compile(lowerJavaScript(`
import { rgb as color } from 'lightvm';
export const init = (state, runtime) => { state.values = new Float32Array([1, 2, 3]); state.total = 0; state.speed = setting(0.5, { min: 0, max: 3 }); };
export function render(state, runtime, pixels) {
  for (const value of state.values) state.total += value;
  state.values[0] += state.speed;
  pixels.fill(color(state.total, state.values.length, 0));
}`));
    expect(result.manifest.settings[0].name).toBe('speed');
  });
  it('accepts sprite callbacks, transforms and readonly artwork', () => {
    compile(lowerJavaScript(`
function starInit(s, runtime, at) { s.position = at; }
function starRender(s, runtime, pixels) { pixels.fill(rgb(255, 60, 0)); }
const star = new Sprite(3, { init: starInit, render: starRender });
export function init(state, runtime) { state.stars = new SpriteBatch(star, 3); }
export function render(state, runtime, pixels) {
  if (runtime.time === 0) state.stars.spawn(1);
  state.stars.render(runtime, pixels);
}`));
  });
  it('supports math functions and keeps diagnostics tied to the authored source', () => {
    const lowered = lowerJavaScript(render('pixels.fill(rgb(Math.floor(-1.5) + 4, Math.abs(-5), Math.round(1.6)));'));
    compile(lowered);
    expect(lowered.locations.at(-1)?.line).toBe(1);
    expect(lowered.locations.length).toBe(lowered.python.trimEnd().split('\n').length);
  });
  it.each([
    'fetch("https://example.com")', 'eval("pixels.clear()")', 'new Date()', 'await Promise.resolve()',
    'let a = 1; a = { value: 2 }', 'const a = 1; a++', 'for (let i = 0; i < 2; i++) {} i++',
    'pixels.fill(rgb(...[1,2,3]))', 'pixels["fill"](rgb(1,2,3))', 'let x = 2 ** 3',
  ])('rejects unsupported syntax without executing it: %s', body => {
    expect(() => lowerJavaScript(render(body))).toThrow();
  });
});
describe('independent blocks frontend', () => {
  it.each(['rainbow', 'pulse', 'wipe'] as const)('compiles the %s workspace directly', kind => {
    const source = blockExample(kind), result = lowerBlocks(source);
    const compiled = compile(result); expect(compiled.manifest.profile).toBe(kind === 'wipe' ? 'transition' : 'animation');
    expect(result.javascript).toContain('export function render');
    expect(parseBlocks(source).blocks?.blocks).toHaveLength(1);
  });
  it('reports the block responsible for missing sockets', () => {
    const doc = parseBlocks(blockExample('pulse')); const block = doc.blocks!.blocks![0].inputs!.RENDER.block!;
    block.id = 'broken'; block.inputs = {};
    expect(() => lowerBlocks(JSON.stringify(doc))).toThrow(expect.objectContaining({ location: expect.objectContaining({ blockId: 'broken' }) }));
  });
  it('rejects unknown blocks and malicious field code before generation', () => {
    expect(() => parseBlocks(JSON.stringify({ blocks: { blocks: [{ type: 'eval' }] } }))).toThrow();
    const doc = parseBlocks(blockExample()); doc.blocks!.blocks![0].fields!.PROFILE = 'animation")\nimport os';
    expect(() => lowerBlocks(JSON.stringify(doc))).toThrow();
  });
});

describe.skipIf(!process.env.LIGHTVM_RECEIVER)('native bytecode execution', () => {
  function replay(source: LoweredSource, options = {}) {
    return JSON.parse(execFileSync(process.env.LIGHTVM_PYTHON || 'python3', ['tests/frontend_replay.py'], {
      cwd: root, input: JSON.stringify({ source: source.python, ...options }), encoding: 'utf8', timeout: 10000,
    })) as number[][];
  }
  const solid = (r: number, g: number, b: number) => Array.from({ length: 5 }, () => [r, g, b]).flat();
  it('executes nested loops, continue updates, break and lexical shadowing', () => {
    expect(replay(lowerJavaScript(render(`
      let total = 0;
      for (let i = 0; i < 8; i++) {
        if (i === 2) continue;
        if (i === 6) break;
        { const total = 100; }
        total += i;
      }
      let n = 3;
      while (n > 0) { total += n; n--; }
      pixels.fill(rgb(total, 0, 0));
    `)))).toEqual([solid(19, 0, 0)]);
  });
  it('keeps JavaScript remainder signs and Math rounding semantics', () => {
    expect(replay(lowerJavaScript(render('pixels.fill(rgb(10 + -5 % 3, 10 + 5 % -3, 10 + -5 % -3));')))).toEqual([solid(8, 12, 8)]);
    expect(replay(lowerJavaScript(render('pixels.fill(rgb(10 + Math.floor(-1.5), Math.round(1.5), 10 + Math.ceil(-1.5)));')))).toEqual([solid(8, 2, 9)]);
  });
  it('short-circuits logical expressions and preserves operand values', () => {
    expect(replay(lowerJavaScript(render('const x = 0; const y = 5; pixels.fill(rgb(x || 12, y && 7, y || (1 / 0)));')))).toEqual([solid(12, 7, 5)]);
  });
  it('keeps state and typed array contents across renders', () => {
    const source = lowerJavaScript(`export function init(s,r) { s.data = new Float32Array([1,2]); s.total = 0; }
    export function render(s,r,p) { for (const value of s.data) s.total += value; s.data[0] += 1; p.fill(rgb(s.total, s.data[0], 0)); }`);
    expect(replay(source, { frames: [0, 0] })).toEqual([solid(7, 3, 0), solid(12, 4, 0)]);
  });
  it('runs blocks and JavaScript transitions with matching seeks', () => {
    const js = lowerJavaScript('export const profile = "transition"; export function render(s,r,p) { wipe(p, r.progress); }');
    const blocks = lowerBlocks(blockExample('wipe'));
    const expected = [solid(200, 100, 20), [20,40,220,20,40,220,200,100,20,200,100,20,200,100,20], solid(20,40,220), [20,40,220,200,100,20,200,100,20,200,100,20,200,100,20]];
    expect(replay(js, { transition: true })).toEqual(expected);
    expect(replay(blocks, { transition: true })).toEqual(expected);
  });
  it.each(['rainbow', 'pulse'] as const)('matches the blocks %s animation to its JavaScript learning view', kind => {
    const blocks = lowerBlocks(blockExample(kind));
    expect(replay(blocks, { frames: [0, 100, 1000] })).toEqual(replay(lowerJavaScript(blocks.javascript), { frames: [0, 100, 1000] }));
  });
});

it('compiles blocks functions, arrays, frozen colours and retained drawing', () => {
  const n = (value: number) => ({ type: 'lm_number', fields: { VALUE: value } });
  const c = (block: any) => ({ block });
  const rgb = { type: 'lm_rgb', inputs: { R: c(n(255)), G: c(n(10)), B: c(n(0)) } };
  const doc = { blocks: { blocks: [
    { type: 'lm_function', fields: { NAME: 'double', PARAM: 'x' }, inputs: { VALUE: c({ type: 'lm_math', fields: { OP: '*' }, inputs: { A: c({ type: 'lm_get', fields: { SCOPE: 'local', NAME: 'x' } }), B: c(n(2)) } }) } },
    { type: 'lm_program', fields: { PROFILE: 'animation' }, inputs: {
      INIT: c({ type: 'lm_setting', fields: { NAME: 'color' }, inputs: { VALUE: c(rgb) }, next: c({ type: 'lm_image', fields: { NAME: 'image' }, inputs: { LENGTH: c(n(5)) }, next: c({ type: 'lm_array', fields: { NAME: 'values', TYPE: 'f' }, inputs: { LENGTH: c(n(3)) } }) }) }),
      RENDER: c({ type: 'lm_array_set', fields: { NAME: 'values' }, inputs: { INDEX: c(n(0)), VALUE: c({ type: 'lm_call', fields: { NAME: 'double' }, inputs: { VALUE: c(n(0.5)) } }) }, next: c({ type: 'lm_image_point', fields: { NAME: 'image' }, inputs: { INDEX: c(n(1)), COLOR: c(rgb) }, next: c({ type: 'lm_image_draw', fields: { NAME: 'image' }, inputs: { POSITION: c(n(0)), SCALE: c(n(1)) } }) }) }),
    } },
  ] } };
  const result = lowerBlocks(JSON.stringify(doc)); compile(result); compile(lowerJavaScript(result.javascript));
});

it('uses integer settings to size resources and accepts numeric hash inputs', () => {
  compile(lowerJavaScript(`export function init(s,r) {
    s.count = setting(int(3), { min: int(1), max: r.pixelCount });
    s.image = new PixelArray(s.count);
  }
  export function render(s,r,p) { let i = 0; i++; p.fill(hsv(hash01(i, 3), 1, 1)); }`));
});
it('rejects lexical reads before initialization even with an outer binding', () => {
  for (const body of ['let x = 1; { let x = x + 1; }', 'let x = 1; { x++; let x = 2; }']) {
    expect(() => lowerJavaScript(render(body))).toThrow(/Initialize/);
  }
});
