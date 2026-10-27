// @vitest-environment node
import { execFileSync } from 'node:child_process';
import { readFileSync, mkdirSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { lowerJavaScript } from './javascript';
import { lowerBlocks, type Block } from './blocks';
import type { LoweredSource } from './source';

const root = fileURLToPath(new URL('../../..', import.meta.url));
const py = process.env.LIGHTVM_PYTHON || 'python3';
const runtime = process.env.LIGHTVM_NUMERIC_RUNTIME;
const vectors = JSON.parse(readFileSync(root + '/tests/fixtures/numeric-vectors.json', 'utf8'));
const b = (type: string, fields = {}, values: Record<string, Block> = {}): Block => ({ type, fields, inputs: Object.fromEntries(Object.entries(values).map(([k, block]) => [k, { block }])) });
const num = (value: number) => b('lm_number', { VALUE: value });
const integer = (value: number) => b('lm_integer', { VALUE: value });
const fixed = (value: string) => b('lm_fixed', { VALUE: value });
const convert = (name: string, value: Block) => b('lm_convert', { FUNCTION: name }, { VALUE: value });
const binary = (op: string, a: Block, c: Block) => b(['&', '|', '^', '<<', '>>'].includes(op) ? 'lm_bitwise' : ['<', '=='].includes(op) ? 'lm_compare' : 'lm_math', { OP: op }, { A: a, B: c });
const blocksResult = (value: Block) => lowerBlocks(JSON.stringify({ blocks: { blocks: [b('lm_program', { PROFILE: 'animation' }, { INIT: b('lm_set', { SCOPE: 'state', NAME: 'result' }, { VALUE: value }) })] } }));
const jsResult = (value: string) => lowerJavaScript(`export function init(s,r) { s.result = ${value}; } export function render(s,r,p) {}`);
function run(sources: { python: string }[], options = {}) {
  return JSON.parse(execFileSync(py, ['tests/frontend_numeric.py'], { cwd: root, input: JSON.stringify(sources.map(s => ({ source: s.python, execute: Boolean(runtime), ...options }))), encoding: 'utf8', timeout: 30000 })) as any[];
}
function valid(sources: { python: string }[], options = {}) {
  const results = run(sources, options);
  results.forEach((r, i) => expect(r.diagnostics, sources[i].python).toEqual([]));
  return results;
}

describe('explicit numeric frontend contracts', () => {
  it('exposes normalized easing in Python, JavaScript, Blocks and the learning view', { timeout: 30000 }, () => {
    const curves = ['sine', 'circular', 'exponential'].flatMap(f => ['in', 'out', 'in_out'].map(d => `${f}_${d}`));
    const samples = [-0.5, 0, 2 ** -149, 0.125, 0.5, 0.875, 1 - 2 ** -24, 1, 1.5];
    const sources: { python: string }[] = [];
    for (const name of curves) for (const t of samples) {
      const blocks = blocksResult(b('lm_motion', { FUNCTION: name }, { VALUE: num(t) }));
      const camel = name.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());
      sources.push({ python: `from lightvm import profile,init,render\nfrom lightvm.easing import ${name}\nprofile("animation")\n@init\ndef setup(s,r):s.result=${name}(${Number.isInteger(t) ? t + ".0" : t})\n@render\ndef draw(s,r,p):pass\n` }, jsResult(`${camel}(${t})`), blocks, lowerJavaScript(blocks.javascript));
    }
    const results = valid(sources);
    results.forEach((r, i) => {
      expect(r.manifest.requires).toBe('1.3.0');
      if (runtime) {
        expect(r.runtime.status).toBe(1);
        expect(r.runtime.state).toEqual(results[i - i % 4].runtime.state);
        const t = samples[Math.floor(i / 4) % samples.length];
        if (t <= 0) expect(r.runtime.state).toEqual([0]);
        if (t >= 1) expect(r.runtime.state).toEqual([0x3f800000]);
      }
    });
    expect(run([jsResult('sineIn(fixed(0.5))')])[0].diagnostics.length).toBeGreaterThan(0);
    valid([jsResult('sineIn(float(fixed(0.5)))')]);
  });
  it('preserves every decimal digit through JS and Blocks lowering', () => {
    for (const token of ['0.0000076293945312500000001', '0.0000076293945312499999999', '-0.000022888183593750000001', '32767.9999847412109375']) {
      const blocks = blocksResult(fixed(token));
      const sources = [jsResult(`fixed(${token})`), blocks, lowerJavaScript(blocks.javascript)];
      sources.forEach(s => expect(s.python).toContain(`fixed(${token})`));
      const results = valid(sources);
      if (runtime) results.forEach(r => expect(r.runtime.state).toEqual(results[0].runtime.state));
    }
  });
  it('matches the reviewed literal words and rejects out-of-range rounding', () => {
    for (const v of vectors.literals) {
      const blocks = blocksResult(fixed(v.source));
      const sources = [jsResult(`fixed(${v.source})`), blocks, lowerJavaScript(blocks.javascript)];
      const results = run(sources);
      results.forEach(r => {
        expect(r.diagnostics.length > 0).toBe(Boolean(v.fault));
        if (runtime && !v.fault) expect(r.runtime.state).toEqual([parseInt(v.result, 16)]);
      });
    }
  });
  it('keeps exact signed i32 literals and explicit conversions', () => {
    for (const value of [-2147483647, -123456789, 2147483647]) {
      const blocks = blocksResult(integer(value));
      const results = valid([jsResult(`int(${value})`), blocks, lowerJavaScript(blocks.javascript)]);
      if (runtime) results.forEach(r => expect(r.runtime.state).toEqual([value >>> 0]));
    }
  });
  it('diagnoses implicit fixed mixing, numeric bitwise operands, and unsupported wire types', () => {
    for (const source of [jsResult('fixed(1) + 1'), jsResult('fixed(1) & int(3)'), jsResult('1 << 2'), jsResult('setting(fixed(1))')]) {
      expect(run([source])[0].diagnostics.length).toBeGreaterThan(0);
    }
    expect(() => lowerJavaScript('export function render(s,r,p){let x=int(1) >>> int(1);}')).toThrow(/>>>/);
    expect(() => blocksResult(fixed('0); import os'))).toThrow(/decimal/);
  });
  it('rejects an insufficient target for scalar fixed and new native math', () => {
    for (const source of [jsResult('fixed(1)'), blocksResult(fixed('1')), jsResult('Math.sqrt(4)')]) {
      expect(run([source], { target: { version: '1.1.0' } })[0].diagnostics.length).toBeGreaterThan(0);
    }
  });
  it('lowers bounded number and integer loops without changing their numeric meaning', () => {
    const body = 'p[i]=rgb(int(i)&int(31),0,0);';
    const range = lowerJavaScript(`export function render(s,r,p){for(let i=int(0);i<p.count;i++){${body}}}`);
    expect(range.python).toContain(' in range(');
    const [result] = valid([range]);
    expect(result.manifest.requires).toBe('1.3.0');
    expect(result.advisories.some((a: any) => a.message.includes('uses a native map'))).toBe(true);
    expect(result.advisories.some((a: any) => a.message.includes('Kernel plan estimate'))).toBe(true);
    for (const loop of ['for(let i=0;i<p.count;i++)', 'for(let i=int(0);i<p.count;i++)']) {
      const lowered = lowerJavaScript(`export function render(s,r,p){${loop}{${body}}}`);
      expect(lowered.python).toContain(' in range(');
      expect(valid([lowered])[0].advisories.some((a: any) => a.message.includes('uses a native map'))).toBe(true);
    }
    const alias = lowerJavaScript(`export function render(s,r,p){const count=p.count;for(let i=0;i<count;i++){p[i]=rgb(i,0,0);}}`);
    expect(alias.python).toContain(' in range(');
    expect(valid([alias])[0].advisories.some((a: any) => a.message.includes('uses a native map'))).toBe(true);
    for (const loop of ['for(let i=int(0);i<s.count;i++)', 'for(let i=int(0);i<2147483647;i++)', 'for(let i=0;i<int(2147483647);i++)']) {
      expect(lowerJavaScript(`export function render(s,r,p){${loop}{${body}}}`).python).not.toContain(' in range(');
    }
    expect(lowerJavaScript(`export function render(s,r,p){for(let i=int(0);i<p.count;i++){i++;${body}}}`).python).not.toContain(' in range(');
    expect(lowerJavaScript(`export function render(s,r,p){const count=int(2147483647);for(let i=0;i<count;i++){${body}}}`).python).not.toContain(' in range(');
  });
});

describe.skipIf(!runtime)('reviewed numeric vectors through all frontends and native execution', () => {
  const signed = (word: string) => parseInt(word, 16) | 0;
  const q = (word: string) => convert('fixed_from_bits', integer(signed(word)));
  const jq = (word: string) => `fixedFromBits(int(${signed(word)}))`;
  it('matches every representable exact vector, including folding and scalar faults', { timeout: 30000 }, () => {
    const sources: LoweredSource[] = [], expected: any[] = [];
    for (const v of vectors.exact) {
      const [a, c] = v.args, name = v.op.split('.')[1];
      let block: Block, js: string;
      if (v.op.startsWith('i32.')) {
        if (name === 'not') { block = b('lm_bit_not', {}, { VALUE: integer(signed(a)) }); js = `~int(${signed(a)})`; }
        else { const op = ({ and: '&', or: '|', xor: '^', shl: '<<', shr: '>>' } as any)[name]; block = binary(op, integer(signed(a)), integer(signed(c))); js = `(int(${signed(a)}) ${op} int(${signed(c)}))`; }
      } else if (name === 'from_f32') {
        const buffer = new ArrayBuffer(4), view = new DataView(buffer); view.setUint32(0, parseInt(a, 16)); const value = view.getFloat32(0);
        if (!Number.isFinite(value)) continue;
        block = convert('fixed', binary('+', num(value), num(0))); js = `fixed(float(${Object.is(value, -0) ? '-0.0' : value}) + 0.0)`;
      } else if (name === 'from_i32') { block = convert('fixed', binary('+', integer(signed(a)), integer(0))); js = `fixed(int(${signed(a)}) + int(0))`; }
      else if (name === 'from_bits') { block = q(a); js = jq(a); }
      else if (name === 'neg') { block = b('lm_negate', {}, { VALUE: q(a) }); js = '-' + jq(a); }
      else if (['to_i32', 'to_f32', 'bits', 'sqrt'].includes(name)) {
        const fn = ({ to_i32: 'int', to_f32: 'float', bits: 'fixed_bits', sqrt: 'fixed_sqrt' } as any)[name];
        block = convert(fn, q(a)); js = `${fn.replace(/_([a-z])/g, (_: string, c: string) => c.toUpperCase())}(${jq(a)})`;
      } else {
        const op = ({ add: '+', sub: '-', mul: '*', div: '/', mod: '%', min: 'min', max: 'max', lt: '<', eq: '==' } as any)[name];
        block = binary(op, q(a), q(c)); js = ['min', 'max'].includes(name) ? `Math.${name}(${jq(a)},${jq(c)})` : `(${jq(a)} ${op === '==' ? '===' : op} ${jq(c)})`;
      }
      const blocks = blocksResult(block);
      // JS remainder has dividend sign; the Blocks learning view explicitly
      // lowers the core's divisor-sign modulo before comparing its raw vector.
      const variants = name === 'mod' ? [blocks, lowerJavaScript(blocks.javascript)] : [jsResult(js), blocks, lowerJavaScript(blocks.javascript)];
      variants.forEach(s => { sources.push(s); expected.push(v); });
    }
    for (const optimize of [false, true]) {
      const results = valid(sources, { optimize });
      results.forEach((r, i) => {
        expect(r.runtime.status, sources[i].python).toBe(expected[i].fault ? -1 : 1);
        if (!expected[i].fault) expect(r.runtime.state, sources[i].python).toEqual([parseInt(expected[i].result, 16)]);
      });
    }
  });
  it('preserves JS remainder sign without negating the minimum fixed value', () => {
    const result = valid([jsResult('fixed(-32768) % fixed(-3)'), jsResult('fixed(-3.5) % fixed(3)'), jsResult('int(-2147483648) % int(3)')]);
    expect(result.map(r => r.runtime.state[0])).toEqual([(-2 * 65536) >>> 0, (-.5 * 65536) >>> 0, (-2) >>> 0]);
    const floatCases = valid([jsResult('-1e-20 % 3'), jsResult('1e-20 % -3'), jsResult('-4 % 2'), jsResult('-0.0 % -3')]);
    const word = (n: number) => { const view = new DataView(new ArrayBuffer(4)); view.setFloat32(0, n); return view.getUint32(0); };
    expect(floatCases.map(r => r.runtime.state[0])).toEqual([-1e-20, 1e-20, -0, -0].map(word));
  });
  it('keeps unchosen domains lazy in the newly eligible integer loop', () => {
    const source = lowerJavaScript('export function render(s,r,p){for(let i=int(0);i<p.count;i++){const v=i<int(3)?fixedSqrt(fixed(4)):fixedSqrt(fixed(-1));p[i]=rgb(int(v),0,0);}}');
    expect(valid([source], { count: 3 })[0].runtime.status).toBe(1);
    expect(valid([source], { count: 4 })[0].runtime.status).toBe(-1);
  });
  it('matches native-math words, signed zeros and domain faults', () => {
    const sources: LoweredSource[] = [], expected: any[] = [];
    for (const v of vectors.native) {
      const values = v.args.map((word: string) => { const view = new DataView(new ArrayBuffer(4)); view.setUint32(0, parseInt(word, 16)); return view.getFloat32(0); });
      // NaN/Infinity cannot be authored as source literals. Independent native
      // invocation tests cover those invalid input words at the ABI boundary.
      if (!values.every(Number.isFinite)) continue;
      const exprs = values.map((n: number) => Object.is(n, -0) ? '-0.0' : String(n));
      const blocks = values.map((n: number) => Object.is(n, -0) ? b('lm_negate', {}, { VALUE: num(0) }) : num(n));
      const name = v.op.split('.')[1];
      const block = blocksResult(name === 'pow' ? b('lm_power', {}, { BASE: blocks[0], EXPONENT: blocks[1] }) :
        ['min', 'max'].includes(name) ? binary(name, blocks[0], blocks[1]) : b('lm_native_math', { FUNCTION: name === 'abs' ? 'fabs' : name }, { VALUE: blocks[0] }));
      [jsResult(`Math.${name}(${exprs.join(',')})`), block, lowerJavaScript(block.javascript)].forEach(s => { sources.push(s); expected.push(v); });
    }
    for (const optimize of [false, true]) valid(sources, { optimize }).forEach((r, i) => {
      expect(r.runtime.status, sources[i].python).toBe(expected[i].fault ? -1 : 1);
      if (!expected[i].fault) expect(r.runtime.state, sources[i].python).toEqual([parseInt(expected[i].result, 16)]);
    });
  });
  it('replays matched saved sources through scalar and split/yielding bulk execution', () => {
    const directory = root + '/tests/fixtures/frontend-numeric/';
    const blocks = lowerBlocks(readFileSync(directory + 'gather.blocks.json', 'utf8'));
    const sources = [{ python: readFileSync(directory + 'gather.py', 'utf8') }, lowerJavaScript(readFileSync(directory + 'gather.js', 'utf8')), blocks, lowerJavaScript(blocks.javascript)];
    for (const optimize of [false, true]) {
      for (const count of [1, 33, 200]) for (const [workers, tick] of [[1, 0], [2, 1000]]) {
        const results = valid(sources, { optimize, count, workers, tick, package: true });
        const pixels = Array.from({ length: count }, (_, i) => Buffer.from([i < 16 ? 4 : 1, 2, 7, 255]).toString('hex')).join('');
        results.forEach((r, i) => {
          expect(r.runtime.status, sources[i].python).toBe(1);
          expect(r.runtime.state).toEqual([196608]);
          expect(r.runtime.arrays).toEqual([Array(count).fill(98304)]);
          expect(r.runtime.pixels).toBe(pixels);
          expect(r.runtime.plan_bytes > 0, sources[i].python).toBe(optimize);
          if (process.env.LIGHTVM_FRONTEND_FIXTURES_OUT) {
            mkdirSync(process.env.LIGHTVM_FRONTEND_FIXTURES_OUT, { recursive: true });
            const filename = `${process.env.LIGHTVM_FRONTEND_FIXTURES_OUT}/${['python', 'javascript', 'blocks', 'learning'][i]}-${optimize ? 'bulk' : 'scalar'}`;
            writeFileSync(filename + '.lbc', Buffer.from(r.package, 'base64'));
            writeFileSync(filename + '.py', sources[i].python);
          }
        });
      }
    }
  });
  it('preserves array fill, indexing and length for every numeric kind', () => {
    for (const [kind, value, word] of [['q', fixed('1.5'), 98304], ['i', integer(-17), (-17) >>> 0], ['I', integer(2147483647), 2147483647], ['B', integer(255), 255], ['H', integer(65535), 65535], ['f', num(1.5), 0x3fc00000]] as const) {
      const array = b('lm_array', { NAME: 'table', TYPE: kind }, { LENGTH: integer(3), FILL: value });
      array.next = { block: b('lm_set', { NAME: 'result', SCOPE: 'state' }, { VALUE: b('lm_array_get', { NAME: 'table' }, { INDEX: integer(2) }) }) };
      array.next.block.next = { block: b('lm_set', { NAME: 'length', SCOPE: 'state' }, { VALUE: b('lm_array_length', { NAME: 'table' }) }) };
      const blocks = lowerBlocks(JSON.stringify({ blocks: { blocks: [b('lm_program', { PROFILE: 'animation' }, { INIT: array })] } }));
      valid([blocks, lowerJavaScript(blocks.javascript)]).forEach(r => { expect(r.runtime.status).toBe(1); expect(r.runtime.state).toEqual([word, 3]); });
    }
  });
  it('shares checked fixed array fill/copy semantics across yields', () => {
    const declarations = ['a', 'b'].map(NAME => b('lm_array', { NAME, TYPE: 'q' }, { LENGTH: integer(67) }));
    const operations = [b('lm_fixed_fill', { NAME: 'a' }, { START: integer(1), STOP: integer(66), VALUE: fixed('2.5') }), b('lm_fixed_copy', { NAME: 'b', SOURCE: 'a' })];
    declarations[0].next = { block: declarations[1] }; operations[0].next = { block: operations[1] };
    const blocks = lowerBlocks(JSON.stringify({ blocks: { blocks: [b('lm_program', { PROFILE: 'animation' }, { INIT: declarations[0], RENDER: operations[0] })] } }));
    valid([blocks, lowerJavaScript(blocks.javascript)], { tick: 1000, workers: 2 }).forEach(r => {
      expect(r.runtime.status).toBe(1);
      expect(r.runtime.arrays).toEqual([0, 1].map(() => [0, ...Array(65).fill(163840), 0]));
    });
    for (const source of ['s.a=new Uint8Array(3,{fill:int(256)});', 's.a=new Uint32Array(3,{fill:int(-1)});']) {
      const result = valid([lowerJavaScript(`export function init(s,r){${source}} export function render(s,r,p){}`)])[0];
      expect(result.runtime.status).toBe(-1);
    }
  });
});
