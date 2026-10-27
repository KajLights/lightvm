#!/usr/bin/env node
// Reproducible exploratory probes, not assertions of the desired language contract.
// Emits authored and lowered sources, then checks the actual compiler/native VM.
import { readFileSync, writeFileSync, mkdirSync, mkdtempSync, rmSync } from 'node:fs';
import { resolve, join } from 'node:path';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { stripTypeScriptTypes } from 'node:module';

const root = fileURLToPath(new URL('..', import.meta.url));
mkdirSync(join(root, 'build'), { recursive: true });
const temporary = mkdtempSync(join(root, 'build/teaching-frontends-'));
try {
  for (const name of ['source', 'javascript', 'blocks']) {
    const source = readFileSync(join(root, `web/src/languages/${name}.ts`), 'utf8');
    const compiled = stripTypeScriptTypes(source, { mode: 'transform' });
    writeFileSync(join(temporary, name + '.mjs'), compiled.replaceAll("'./source'", "'./source.mjs'"));
  }
  const { lowerJavaScript } = await import(pathToFileURL(join(temporary, 'javascript.mjs')));
  const { lowerBlocks, blockExample } = await import(pathToFileURL(join(temporary, 'blocks.mjs')));
  const cases = [];
  function add(id, language, source) {
    try {
      const lowered = language === 'javascript' ? lowerJavaScript(source) : language === 'blocks' ? lowerBlocks(source) : { python: source };
      cases.push({ id, language, source, lowered: lowered.python });
    } catch (error) { cases.push({ id, language, source, frontend_error: error.message }); }
  }
  const prefix = 'import random\nfrom array import array\nfrom lightvm import profile, init, render, rgb, hsv, PixelArray\nfrom lightvm.motion import repeat, pingpong\nprofile("animation")\n';
  const py = (id, body, before = '') => add(id, 'python', prefix + before + '@render\ndef draw(state, runtime, pixels):\n' + body.split('\n').map(s => '    ' + s).join('\n') + '\n');
  const js = (id, body, before = '') => add(id, 'javascript', before + '\nexport function render(state, runtime, pixels) {\n' + body + '\n}\n');
  py('py-solid', 'pixels.fill(rgb(40, 0, 0))');
  js('js-solid', 'pixels.fill(rgb(40, 0, 0));');
  py('py-natural-loop', 'for i in range(pixels.count):\n    pixels[i] = hsv(i / pixels.count, 1.0, 0.2)');
  js('js-natural-loop', 'for (let i = 0; i < pixels.count; i++) { pixels[i] = hsv(i / pixels.count, 1, 0.2); }');
  js('js-integer-loop', 'for (let i = int(0); i < pixels.count; i++) { pixels[i] = hsv(i / pixels.count, 1, 0.2); }');
  py('py-branch-stores', 'for i in range(pixels.count):\n    if i < 10:\n        pixels[i] = rgb(40, 0, 0)\n    else:\n        pixels[i] = rgb(0, 0, 40)');
  js('js-branch-stores', 'for (let i = 0; i < pixels.count; i++) { if (i < 10) pixels[i] = rgb(40, 0, 0); else pixels[i] = rgb(0, 0, 40); }');
  py('py-common-store', 'for i in range(pixels.count):\n    colour = rgb(0, 0, 40)\n    if i < 10:\n        colour = rgb(40, 0, 0)\n    pixels[i] = colour');
  py('py-colour-helper', 'for i in range(pixels.count):\n    pixels[i] = band(i)', 'def band(i):\n    if i < 10:\n        return rgb(40, 0, 0)\n    return rgb(0, 0, 40)\n');
  js('js-colour-helper', 'for (let i = 0; i < pixels.count; i++) pixels[i] = band(i);', 'function band(i) { if (i < 10) return rgb(40, 0, 0); return rgb(0, 0, 40); }');
  py('py-procedure', 'paint(pixels)', 'def paint(pixels):\n    pixels.fill(rgb(40, 0, 0))\n');
  js('js-procedure', 'paint(pixels);', 'function paint(pixels) { pixels.fill(rgb(40, 0, 0)); }');
  py('py-range-start', 'for i in range(5, pixels.count):\n    pixels[i] = rgb(40, 0, 0)');
  py('py-range-step', 'for i in range(0, pixels.count, 2):\n    pixels[i] = rgb(40, 0, 0)');
  py('py-range-reverse', 'for i in range(pixels.count - 1, -1, -1):\n    pixels[i] = rgb(40, 0, 0)');
  js('js-range-step', 'for (let i = 0; i < pixels.count; i += 2) pixels[i] = rgb(40, 0, 0);');
  py('py-branch-defined-local', 'if runtime.time < 1.0:\n    level = 10\nelse:\n    level = 20\npixels.fill(rgb(level, 0, 0))');
  py('py-chained-comparison', 'if 0.0 <= runtime.time < 2.0:\n    pixels.fill(rgb(40, 0, 0))');
  py('py-module-constant', 'pixels.fill(rgb(LEVEL, 0, 0))', 'LEVEL = 40\n');
  py('py-int-to-float-assignment', 'level = 0\nlevel = runtime.delta\npixels.fill(hsv(0.0, 1.0, level))');
  py('py-tuple-assignment', 'a, b = 1, 2\npixels.fill(rgb(a, b, 0))');
  py('py-counter-after-loop', 'for i in range(3):\n    pixels[i] = rgb(40, 0, 0)\npixels[0] = rgb(i, 0, 0)');
  py('py-counter-reassigned', 'for i in range(3):\n    state.total += 1\n    i = 7\npixels[0] = rgb(state.total, 0, 0)', '@init\ndef setup(state, runtime):\n    state.total = 0\n');
  py('py-numeric-and', 'level = 2 and 3\npixels.fill(rgb(level, 0, 0))');
  py('py-numeric-or', 'level = 2 or 3\npixels.fill(rgb(level, 0, 0))');
  py('py-numeric-and-condition', 'if 2 and 3:\n    pixels.fill(rgb(40, 0, 0))');
  py('py-numeric-or-condition', 'if 2 or 3:\n    pixels.fill(rgb(40, 0, 0))');
  py('py-boolean-and', 'if runtime.time > 0.0 and runtime.time < 2.0:\n    pixels.fill(rgb(40, 0, 0))');
  js('js-effectful-logical', 'if (Math.random() > 0.5 && runtime.time > 0) pixels.fill(rgb(40, 0, 0));');
  py('py-finite-while', 'i = 0\nwhile i < pixels.count:\n    pixels[i] = rgb(40, 0, 0)\n    i += 1');
  py('py-infinite-while', 'while True:\n    pass');
  py('py-continue', 'for i in range(pixels.count):\n    if i % 2 == 0:\n        continue\n    pixels[i] = rgb(40, 0, 0)');
  py('py-nested-loop', 'for group in range(5):\n    for j in range(3):\n        pixels[group * 3 + j] = rgb(40, 0, 0)');
  py('py-index-count', 'pixels[pixels.count] = rgb(40, 0, 0)');
  py('py-negative-index', 'pixels[-1] = rgb(40, 0, 0)');
  py('py-negative-wrap', 'pixels[(0 - 1) % pixels.count] = rgb(40, 0, 0)');
  js('js-negative-wrap', 'pixels[(0 - 1) % pixels.count] = rgb(40, 0, 0);');
  py('py-ring-cursor', 'phase = repeat(runtime.time / 4.0)\nposition = int(phase * pixels.count)\npixels[position] = rgb(40, 8, 0)');
  js('js-ring-cursor', 'const phase = repeat(runtime.time / 4);\nconst position = Math.floor(phase * pixels.count);\npixels[position] = rgb(40, 8, 0);');
  py('py-state-cursor', 'state.position = (state.position + runtime.delta * 5.0) % pixels.count\npixels[int(state.position)] = rgb(40, 8, 0)', '@init\ndef setup(state, runtime):\n    state.position = 0.0\n');
  js('js-state-cursor', 'state.position = (state.position + runtime.delta * 5) % pixels.count;\npixels[Math.floor(state.position)] = rgb(40, 8, 0);', 'export function init(state, runtime) { state.position = 0; }');
  py('py-palette-gather', 'for i in range(pixels.count):\n    pixels[i] = rgb(state.levels[i % len(state.levels)], 0, 0)', '@init\ndef setup(state, runtime):\n    state.levels = array("i", [10, 20, 40])\n');
  py('py-stateful-random', 'for i in range(pixels.count):\n    pixels[i] = hsv(random.random(), 1.0, 0.2)');
  py('py-array-update', 'for i in range(pixels.count):\n    state.levels[i] = state.levels[i] * 0.9\n    pixels[i] = hsv(0.1, 1.0, state.levels[i])', '@init\ndef setup(state, runtime):\n    state.levels = array("f", [0.2] * runtime.pixel_count)\n');
  py('py-for-each-array', 'total = 0\nfor value in state.levels:\n    total += value\npixels.fill(rgb(total, 0, 0))', '@init\ndef setup(state, runtime):\n    state.levels = array("i", [1, 2, 3])\n');
  js('js-for-each-array', 'let total = int(0);\nfor (const value of state.levels) total += value;\npixels.fill(rgb(total, 0, 0));', 'export function init(state, runtime) { state.levels = new Int32Array([1, 2, 3]); }');
  for (const kind of ['rainbow', 'pulse', 'stars']) add('blocks-' + kind, 'blocks', blockExample(kind));
  const b = (type, fields = {}, values = {}) => ({ type, fields, inputs: Object.fromEntries(Object.entries(values).map(([k, v]) => [k, { block: v }])) });
  const n = value => b('lm_number', { VALUE: value });
  const get = name => b('lm_get', { NAME: name, SCOPE: 'local' });
  const count = b('lm_runtime', { PROPERTY: 'count' });
  const op = (a, operator, c) => b('lm_math', { OP: operator }, { A: a, B: c });
  const doc = (...roots) => JSON.stringify({ blocks: { languageVersion: 0, blocks: roots } });
  add('blocks-natural-loop', 'blocks', doc(b('lm_program', { PROFILE: 'animation' }, { RENDER: b('lm_each', { NAME: 'i' }, { DO: b('lm_pixel', {}, { INDEX: get('i'), COLOR: b('lm_hsv', {}, { H: op(get('i'), '/', count), S: n(1), V: n(0.2) }) }) }) })));
  const paint = (r, g, blue) => b('lm_pixel', {}, { INDEX: get('i'), COLOR: b('lm_rgb', {}, { R: n(r), G: n(g), B: n(blue) }) });
  const bands = b('lm_if', {}, {
    TEST: b('lm_compare', { OP: '<' }, { A: get('i'), B: n(10) }),
    THEN: paint(40, 0, 0), ELSE: paint(0, 0, 40),
  });
  add('blocks-branch-stores', 'blocks', doc(b('lm_program', { PROFILE: 'animation' }, { RENDER: b('lm_each', { NAME: 'i' }, { DO: bands }) })));
  add('blocks-ring-cursor', 'blocks', doc(b('lm_program', { PROFILE: 'animation' }, { RENDER: b('lm_pixel', {}, { INDEX: op(b('lm_motion', { FUNCTION: 'repeat' }, { VALUE: op(b('lm_runtime', { PROPERTY: 'time' }), '/', n(4)) }), '*', count), COLOR: b('lm_rgb', {}, { R: n(40), G: n(8), B: n(0) }) }) })));
  const bounce = b('lm_function', { NAME: 'bounce', PARAM: 'seconds' }, {
    VALUE: b('lm_motion', { FUNCTION: 'pingpong' }, { VALUE: op(get('seconds'), '/', n(2)) }),
  });
  const bounceCall = b('lm_call', { NAME: 'bounce' }, { VALUE: b('lm_runtime', { PROPERTY: 'time' }) });
  const bounceFill = b('lm_fill', {}, { COLOR: b('lm_hsv', {}, { H: n(0.1), S: n(1), V: bounceCall }) });
  add('blocks-bounce-helper', 'blocks', doc(bounce, b('lm_program', { PROFILE: 'animation' }, { RENDER: bounceFill })));
  for (const name of ['rainbow', 'trail', 'pulse']) add('example-js-' + name, 'javascript', readFileSync(join(root, `examples/javascript/${name}.js`), 'utf8'));
  const output = resolve(process.argv[2] ?? join(root, 'build/teaching-review.json'));
  execFileSync(process.env.LIGHTVM_PYTHON || 'python3', [join(root, 'tools/review_teaching.py'), output], { cwd: root, input: JSON.stringify(cases), stdio: ['pipe', 'inherit', 'inherit'] });
} finally { rmSync(temporary, { recursive: true, force: true }); }
