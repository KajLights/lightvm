import { SDK_IMPORTS, SourceError, type LoweredSource, type SourceLocation } from './source';

// The saved workspace is the blocks source. Its compiler walks this structure
// directly; JavaScript is an optional teaching view, never a compilation input.
export interface Block { type: string; id?: string; fields?: Record<string, unknown>; inputs?: Record<string, { block?: Block; shadow?: Block }>; next?: { block: Block }; enabled?: boolean; disabled?: boolean; disabledReasons?: string[] }
export interface BlockDocument { blocks?: { languageVersion?: number; blocks?: Block[] } }
const field = (name: string, text = 'value') => ({ type: 'field_input', name, text });
const number = (name: string, value = 0) => ({ type: 'field_number', name, value });
const dropdown = (name: string, options: string[][]) => ({ type: 'field_dropdown', name, options });
const value = (name: string) => ({ type: 'input_value', name });
const statements = (name: string) => ({ type: 'input_statement', name });
const output = { output: null, colour: 210 };
const statement = { previousStatement: null, nextStatement: null, colour: 170 };
export const BLOCK_DEFINITIONS = [
  { type: 'lm_program', message0: '%1', args0: [dropdown('PROFILE', [['Animation', 'animation'], ['Transition A → B', 'transition']])], message1: 'when starting %1', args1: [statements('INIT')], message2: 'every frame %1', args2: [statements('RENDER')], colour: 280, tooltip: 'Starting runs once. Every frame draws your lights. Transitions use progress instead of time.' },
  { type: 'lm_number', message0: '%1', args0: [number('VALUE')], ...output },
  { type: 'lm_bool', message0: '%1', args0: [dropdown('VALUE', [['true', 'TRUE'], ['false', 'FALSE']])], ...output },
  { type: 'lm_runtime', message0: '%1', args0: [dropdown('PROPERTY', [['elapsed seconds', 'time'], ['seconds since last frame', 'delta'], ['transition progress (0–1)', 'progress'], ['number of pixels', 'count']])], ...output },
  { type: 'lm_rgb', message0: 'RGB red %1 green %2 blue %3', args0: [value('R'), value('G'), value('B')], ...output, colour: 30, inputsInline: false },
  { type: 'lm_hsv', message0: 'HSV hue %1 saturation %2 brightness %3', args0: [value('H'), value('S'), value('V')], ...output, colour: 30, inputsInline: false },
  { type: 'lm_colorwheel', message0: 'colour wheel (0–255) %1', args0: [value('VALUE')], ...output, colour: 30 },
  { type: 'lm_math', message0: '%1 %2 %3', args0: [value('A'), dropdown('OP', [['+', '+'], ['−', '-'], ['×', '*'], ['÷', '/'], ['modulo', '%'], ['minimum', 'min'], ['maximum', 'max']]), value('B')], ...output, inputsInline: true },
  { type: 'lm_compare', message0: '%1 %2 %3', args0: [value('A'), dropdown('OP', [['=', '=='], ['≠', '!='], ['<', '<'], ['≤', '<='], ['>', '>'], ['≥', '>=']]), value('B')], ...output, colour: 210, inputsInline: true },
  { type: 'lm_logic', message0: '%1 %2 %3', args0: [value('A'), dropdown('OP', [['and', 'and'], ['or', 'or']]), value('B')], ...output, inputsInline: true },
  { type: 'lm_not', message0: 'not %1', args0: [value('VALUE')], ...output },
  { type: 'lm_motion', message0: '%1 %2', args0: [dropdown('FUNCTION', [['repeat 0 → 1', 'repeat'], ['bounce 0 ↔ 1', 'pingpong'], ['smooth step', 'smoothstep'], ['clamp 0…1', 'clamp01'], ['sine', 'sin'], ['ease in', 'quadratic_in'], ['ease out', 'quadratic_out'], ['ease in & out', 'quadratic_in_out']]), value('VALUE')], ...output, colour: 230 },
  { type: 'lm_lerp', message0: 'between %1 and %2 at %3', args0: [value('A'), value('B'), value('T')], ...output, colour: 230, inputsInline: true },
  { type: 'lm_random', message0: 'random (0–1)', ...output, colour: 230 },
  { type: 'lm_get', message0: '%1 %2', args0: [dropdown('SCOPE', [['variable', 'local'], ['remembered', 'state']]), field('NAME')], ...output, colour: 330 },
  { type: 'lm_set', message0: 'set %1 %2 to %3', args0: [dropdown('SCOPE', [['variable', 'local'], ['remembered', 'state']]), field('NAME'), value('VALUE')], ...statement, colour: 330 },
  { type: 'lm_setting', message0: 'setting %1 default %2', args0: [field('NAME', 'speed'), value('VALUE')], ...statement, colour: 330, tooltip: 'Put in when starting. Read it with a remembered value block. Settings stay fixed while running.' },
  { type: 'lm_input', message0: 'live input %1 default %2', args0: [field('NAME', 'level'), number('DEFAULT', 0.5)], ...output, colour: 330, tooltip: 'Wire this input to a value, timer or external source using Preview settings & inputs.' },
  { type: 'lm_fill', message0: 'fill lights with %1', args0: [value('COLOR')], ...statement, colour: 30 },
  { type: 'lm_range', message0: 'fill pixels from %1 up to %2 with %3', args0: [value('START'), value('STOP'), value('COLOR')], ...statement, colour: 30 },
  { type: 'lm_pixel', message0: 'pixel %1 colour %2', args0: [value('INDEX'), value('COLOR')], ...statement, colour: 30 },
  { type: 'lm_clear', message0: 'clear lights to transparent', ...statement, colour: 30 },
  { type: 'lm_gradient', message0: 'gradient from %1 to %2', args0: [value('A'), value('B')], ...statement, colour: 30 },
  { type: 'lm_each', message0: 'for each pixel %1 %2', args0: [field('NAME', 'i'), statements('DO')], ...statement, colour: 120 },
  { type: 'lm_repeat', message0: 'repeat %1 times with counter %2 %3', args0: [value('COUNT'), field('NAME', 'i'), statements('DO')], ...statement, colour: 120 },
  { type: 'lm_while', message0: 'while %1 %2', args0: [value('TEST'), statements('DO')], ...statement, colour: 120 },
  { type: 'lm_if', message0: 'if %1 then %2 else %3', args0: [value('TEST'), statements('THEN'), statements('ELSE')], ...statement, colour: 210 },
  { type: 'lm_flow', message0: '%1', args0: [dropdown('FLOW', [['leave loop', 'break'], ['next iteration', 'continue'], ['finish this frame', 'return']])], ...statement, colour: 120 },
  { type: 'lm_mix', message0: 'crossfade A → B at %1', args0: [value('VALUE')], ...statement, colour: 280 },
  { type: 'lm_wipe', message0: 'wipe A → B at %1 %2', args0: [value('VALUE'), dropdown('REVERSE', [['from start', 'False'], ['from end', 'True']])], ...statement, colour: 280 },
  { type: 'lm_function', message0: 'function %1 with %2 %3 return %4', args0: [field('NAME', 'breathe'), field('PARAM', 'seconds'), statements('DO'), value('VALUE')], colour: 290, tooltip: 'A reusable calculation. Use its argument with a variable block. Functions cannot call themselves.' },
  { type: 'lm_sprite', message0: 'sprite %1 width %2 with %3', args0: [field('NAME', 'star'), number('WIDTH', 3), field('PARAM', 'position')], message1: 'when created %1', args1: [statements('INIT')], message2: 'each frame %1', args2: [statements('RENDER')], colour: 290, tooltip: 'Put fixed geometry and colour in when created. Remembered values belong to each sprite. Its clock starts at birth.' },
  { type: 'lm_batch', message0: 'remember batch %1 of sprite %2 capacity %3', args0: [field('NAME', 'stars'), field('SPRITE', 'star'), number('CAPACITY', 20)], ...statement, colour: 290 },
  { type: 'lm_spawn', message0: 'create sprite in batch %1 with %2', args0: [field('NAME', 'stars'), value('VALUE')], ...statement, colour: 290 },
  { type: 'lm_batch_render', message0: 'draw batch %1', args0: [field('NAME', 'stars')], ...statement, colour: 290 },
  { type: 'lm_sprite_transform', message0: 'sprite position %1 scale %2 origin %3', args0: [value('POSITION'), value('SCALE'), value('ORIGIN')], ...statement, colour: 290 },
  { type: 'lm_sprite_color', message0: 'sprite opacity %1 brightness %2 tint %3', args0: [value('ALPHA'), value('BRIGHTNESS'), value('TINT')], ...statement, colour: 290 },
  { type: 'lm_sprite_remove', message0: 'remove this sprite', ...statement, colour: 290 },
  { type: 'lm_artwork', message0: 'shared artwork %1 RGBA bytes %2', args0: [field('NAME', 'star_image'), field('DATA', '64,64,64,255,255,255,255,255,64,64,64,255')], colour: 30, tooltip: 'Comma-separated red, green, blue, alpha bytes (0–255) for each pixel. Artwork is immutable and shared by every sprite.' },
  { type: 'lm_artwork_draw', message0: 'draw shared artwork %1', args0: [field('NAME', 'star_image')], ...statement, colour: 30 },
  { type: 'lm_call', message0: '%1 with %2', args0: [field('NAME', 'breathe'), value('VALUE')], ...output, colour: 290 },
  { type: 'lm_array', message0: 'remember %1 as %2 array of length %3', args0: [field('NAME', 'values'), dropdown('TYPE', [['decimal', 'f'], ['integer', 'i']]), value('LENGTH')], ...statement, colour: 330 },
  { type: 'lm_array_get', message0: 'remembered array %1 at %2', args0: [field('NAME', 'values'), value('INDEX')], ...output, colour: 330 },
  { type: 'lm_array_set', message0: 'set remembered array %1 at %2 to %3', args0: [field('NAME', 'values'), value('INDEX'), value('VALUE')], ...statement, colour: 330 },
  { type: 'lm_image', message0: 'remember image %1 with %2 pixels', args0: [field('NAME', 'image'), value('LENGTH')], ...statement, colour: 30 },
  { type: 'lm_image_point', message0: 'paint image %1 at %2 colour %3', args0: [field('NAME', 'image'), value('INDEX'), value('COLOR')], ...statement, colour: 30 },
  { type: 'lm_image_fade', message0: 'fade image %1 by multiplier %2', args0: [field('NAME', 'image'), value('VALUE')], ...statement, colour: 30 },
  { type: 'lm_image_draw', message0: 'draw image %1 at %2 scale %3', args0: [field('NAME', 'image'), value('POSITION'), value('SCALE')], ...statement, colour: 30 },
  { type: 'lm_copy', message0: 'copy composition %1 from %2 up to %3', args0: [dropdown('SOURCE', [['A', 'A'], ['B', 'B']]), value('START'), value('STOP')], ...statement, colour: 280 },
];
const definitions = new Map(BLOCK_DEFINITIONS.map(d => [d.type, d]));
export function parseBlocks(source: string): BlockDocument {
  if (new TextEncoder().encode(source).length > 65536) throw new SourceError('Keep the saved blocks workspace below 64 KiB.');
  let doc: BlockDocument;
  try { doc = JSON.parse(source); } catch { throw new SourceError('This is not valid blocks JSON. Import a saved .blocks.json file.'); }
  if (!doc || typeof doc !== 'object' || !doc.blocks || !Array.isArray(doc.blocks.blocks)) throw new SourceError('Choose a saved LightVM blocks workspace.');
  let count = 0;
  const ids = new Set<string>();
  const visit = (b: Block, depth: number) => {
    const fail = (message: string): never => { throw new SourceError(message, { line: 1, column: 1, blockId: b?.id }); };
    if (!b || typeof b !== 'object' || !definitions.has(b.type)) fail(`Unknown block type “${b?.type}”.`);
    if (++count > 500 || depth > 80) fail('Use at most 500 blocks and 80 nesting levels.');
    if (b.id !== undefined) { if (typeof b.id !== 'string' || ids.has(b.id)) fail('Block IDs must be unique strings.'); ids.add(b.id); }
    const def = definitions.get(b.type)! as any;
    const args = [0, 1, 2].flatMap(i => def['args' + i] ?? []);
    for (const [name, v] of Object.entries(b.fields ?? {})) {
      const arg = args.find(a => a.name === name && a.type.startsWith('field_'));
      if (!arg) fail(`Unknown field “${name}”.`);
      if (arg.type === 'field_dropdown' && !arg.options.some((o: string[]) => o[1] === v)) fail(`Invalid choice for ${name}.`);
      if (arg.type === 'field_number' && (typeof v !== 'number' || !Number.isFinite(v))) fail('Number fields must be finite.');
      if (arg.type === 'field_input') {
        if (b.type === 'lm_artwork' && name === 'DATA') {
          if (typeof v !== 'string' || !/^\s*\d+(?:\s*,\s*\d+)*\s*$/.test(v)) fail('Artwork needs comma-separated RGBA bytes.');
          const bytes = String(v).split(',').map(Number);
          if (bytes.length % 4 || bytes.length > 4096 || bytes.some(n => !Number.isInteger(n) || n > 255)) fail('Use 1–1024 RGBA pixels with channel bytes from 0 to 255.');
        } else if (typeof v !== 'string' || !/^[A-Za-z_][A-Za-z0-9_]{0,31}$/.test(v)) fail('Names use letters, digits and underscores, beginning with a letter or underscore.');
      }
    }
    for (const [name, child] of Object.entries(b.inputs ?? {})) {
      if (!args.some(a => a.name === name && a.type.startsWith('input_')) || !child || typeof child !== 'object') fail(`Unknown input “${name}”.`);
      if (child.block) visit(child.block, depth + 1); if (child.shadow) visit(child.shadow, depth + 1);
    }
    if (b.next?.block) visit(b.next.block, depth + 1);
  };
  doc.blocks.blocks.forEach(b => visit(b, 0)); return doc;
}

export function lowerBlocks(source: string): LoweredSource & { javascript: string } {
  const doc = parseBlocks(source), lines: string[] = [], js: string[] = [], locations: SourceLocation[] = [];
  const roots = doc.blocks!.blocks!.filter(b => b.enabled !== false && !b.disabled && !b.disabledReasons?.length);
  const root = roots.find(b => b.type === 'lm_program'), functions = roots.filter(b => b.type === 'lm_function');
  const sprites = roots.filter(b => b.type === 'lm_sprite'), artwork = roots.filter(b => b.type === 'lm_artwork');
  const fail = (b: Block | undefined, message: string): never => { throw new SourceError(message, { line: 1, column: 1, blockId: b?.id }); };
  if (!root || roots.filter(b => !['lm_function', 'lm_sprite', 'lm_artwork'].includes(b.type)).length !== 1) fail(root, 'Use one Animation or Transition block, with optional functions, sprite definitions and shared artwork. Remove or disable loose blocks.');
  const fieldValue = (b: Block, name: string) => b.fields?.[name] ?? ([0, 1, 2].flatMap(i => (definitions.get(b.type) as any)['args' + i] ?? []).find(a => a.name === name)?.text);
  const f = (b: Block, name: string) => String(fieldValue(b, name));
  const child = (b: Block, name: string) => b.inputs?.[name]?.block ?? b.inputs?.[name]?.shadow;
  const inputs = new Map<string, { value: number; block: Block }>();
  let stage = '', loopDepth = 0;
  let declared = new Set<string>();
  const line = (py: string, javascript: string, b: Block, depth: number) => { lines.push('    '.repeat(depth) + py); js.push('  '.repeat(depth) + javascript); locations.push({ line: 1, column: 1, blockId: b.id }); };
  type Pair = [string, string];
  const expression = (b: Block | undefined): Pair => {
    if (!b) return fail(b, 'Connect a value block to every empty socket.');
    const v = (name: string) => expression(child(b, name));
    switch (b.type) {
      case 'lm_number': { const n = Number(b.fields?.VALUE ?? 0); return [Number.isInteger(n) ? `${n}.0` : String(n), String(n)]; }
      case 'lm_bool': return f(b, 'VALUE') === 'TRUE' ? ['True', 'true'] : ['False', 'false'];
      case 'lm_runtime': { const p = f(b, 'PROPERTY'); return p === 'count' ? (stage.endsWith('init') ? ['runtime.pixel_count', 'runtime.pixelCount'] : ['pixels.count', 'pixels.count']) : [`runtime.${p}`, `runtime.${p}`]; }
      case 'lm_rgb': case 'lm_hsv': { const name = b.type === 'lm_rgb' ? 'rgb' : 'hsv', a = (name === 'rgb' ? ['R', 'G', 'B'] : ['H', 'S', 'V']).map(v); return [`${name}(${a.map((p, i) => name === 'rgb' && child(b, ['R', 'G', 'B'][i])?.type === 'lm_number' ? String(Number(child(b, ['R', 'G', 'B'][i])?.fields?.VALUE ?? 0)) : p[0]).join(', ')})`, `${name}(${a.map(p => p[1]).join(', ')})`]; }
      case 'lm_call': { const a = v('VALUE'), name = f(b, 'NAME'); return [`fn_${name}(${a[0]})`, `${name}(${a[1]})`]; }
      case 'lm_array_get': { const i = v('INDEX'), name = f(b, 'NAME'); return [`state.${name}[int(${i[0]})]`, `state.${name}[${i[1]}]`]; }
      case 'lm_colorwheel': { const a = v('VALUE'); return [`colorwheel(${a[0]})`, `colorwheel(${a[1]})`]; }
      case 'lm_math': case 'lm_compare': case 'lm_logic': {
        const a = v('A'), c = v('B'), op = f(b, 'OP');
        if (['min', 'max'].includes(op)) return [`${op}(${a[0]}, ${c[0]})`, `Math.${op}(${a[1]}, ${c[1]})`];
        const jsOp = ({ '==': '===', '!=': '!==', and: '&&', or: '||' } as Record<string, string>)[op] ?? op;
        // Blocks modulo is Euclidean. The learning view spells this out.
        return [`(${a[0]} ${op} ${c[0]})`, op === '%' ? `(((${a[1]} % ${c[1]}) + ${c[1]}) % ${c[1]})` : `(${a[1]} ${jsOp} ${c[1]})`];
      }
      case 'lm_not': { const a = v('VALUE'); return [`(not ${a[0]})`, `(!${a[1]})`]; }
      case 'lm_motion': { const a = v('VALUE'), name = f(b, 'FUNCTION'), jsName = name.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase()); return [`${name === 'sin' ? 'math.sin' : name}(${a[0]})`, `${name === 'sin' ? 'Math.sin' : jsName}(${a[1]})`]; }
      case 'lm_lerp': { const a = ['A', 'B', 'T'].map(v); return [`lerp(${a.map(p => p[0]).join(', ')})`, `lerp(${a.map(p => p[1]).join(', ')})`]; }
      case 'lm_random': return ['random.random()', 'Math.random()'];
      case 'lm_get': { const name = f(b, 'NAME'), state = f(b, 'SCOPE') === 'state'; return [state ? `state.${name}` : `v_${name}`, state ? `state.${name}` : name]; }
      case 'lm_input': {
        const name = f(b, 'NAME'), value = Number(b.fields?.DEFAULT ?? 0.5), previous = inputs.get(name);
        if (previous && previous.value !== value) fail(b, `Use the same default for every “${name}” input block.`);
        inputs.set(name, { value, block: b }); return [`runtime.input(input_${name})`, `runtime.input(input_${name})`];
      }
      default: return fail(b, 'This socket needs a value block.');
    }
  };
  const chain = (first: Block | undefined, depth: number) => {
    const enclosing = declared; declared = new Set(enclosing);
    let count = 0;
    for (let b = first; b; b = b.next?.block) {
      if (b.enabled === false || b.disabled || b.disabledReasons?.length) continue; count++;
      const v = (name: string) => { const c = child(b!, name); if (!c) fail(b, `Connect a value to ${name.toLowerCase()}.`); return expression(c); };
      const emit = (py: string, javascript: string) => line(py, javascript, b!, depth);
      const body = (name: string) => chain(child(b!, name), depth + 1);
      switch (b.type) {
        case 'lm_batch': {
          if (stage !== 'init' || depth !== 1) fail(b, 'Create a sprite batch directly in the program’s “when starting” section.');
          const sprite = f(b, 'SPRITE'), capacity = Number(b.fields?.CAPACITY ?? 20);
          if (!sprites.some(s => f(s, 'NAME') === sprite)) fail(b, 'Add a sprite definition with this name.');
          if (!Number.isInteger(capacity) || capacity < 1 || capacity > 65535) fail(b, 'Batch capacity must be a whole number from 1 to 65535.');
          emit(`state.${f(b, 'NAME')} = SpriteBatch(sprite_${sprite}, capacity=${capacity})`, `state.${f(b, 'NAME')} = new SpriteBatch(sprite_${sprite}, ${capacity});`); break;
        }
        case 'lm_spawn': case 'lm_batch_render': {
          if (stage !== 'init' && stage !== 'render') fail(b, 'Create and draw sprites from the containing program.');
          if (b.type === 'lm_spawn') { const a = v('VALUE'); emit(`state.${f(b, 'NAME')}.spawn(${a[0]})`, `state.${f(b, 'NAME')}.spawn(${a[1]});`); }
          else { if (stage !== 'render') fail(b, 'Draw a batch in the program’s “every frame” section.'); emit(`state.${f(b, 'NAME')}.render(runtime, pixels)`, `state.${f(b, 'NAME')}.render(runtime, pixels);`); }
          break;
        }
        case 'lm_sprite_transform': case 'lm_sprite_color': case 'lm_sprite_remove': {
          if (!stage.startsWith('sprite_')) fail(b, 'Place this block inside a sprite definition.');
          if (b.type === 'lm_sprite_remove') emit('state.remove()', 'state.remove();');
          else if (b.type === 'lm_sprite_transform') {
            const p = v('POSITION'), s = v('SCALE'), o = v('ORIGIN');
            emit(`state.transform = Transform(translate=${p[0]}, scale=${s[0]}, origin=${o[0]})`, `state.transform = new Transform({ translate: ${p[1]}, scale: ${s[1]}, origin: ${o[1]} });`);
          } else {
            const a = v('ALPHA'), l = v('BRIGHTNESS'), c = v('TINT');
            emit(`state.color = ColorTransform(alpha=${a[0]}, brightness=${l[0]}, tint=${c[0]})`, `state.color = new ColorTransform({ alpha: ${a[1]}, brightness: ${l[1]}, tint: ${c[1]} });`);
          }
          break;
        }
        case 'lm_artwork_draw': {
          if (!artwork.some(a => f(a, 'NAME') === f(b, 'NAME'))) fail(b, 'Add shared artwork with this name.');
          if (!stage.endsWith('render')) fail(b, 'Draw shared artwork in an each-frame section.');
          emit(`pixels.draw(art_${f(b, 'NAME')})`, `pixels.draw(art_${f(b, 'NAME')});`); break;
        }
        case 'lm_array': case 'lm_image': {
          if (stage !== 'init') fail(b, 'Create remembered arrays and images in “when starting”.');
          const length = child(b, 'LENGTH');
          // Resource sizes are declaration expressions, known before render.
          let size: string;
          if (length?.type === 'lm_number' && Number.isInteger(Number(length.fields?.VALUE))) size = String(length.fields!.VALUE);
          else if (length?.type === 'lm_runtime' && f(length, 'PROPERTY') === 'count') size = 'runtime.pixel_count';
          else fail(b, 'Use a whole number or “number of pixels” for the initial length.');
          const name = f(b, 'NAME');
          if (b.type === 'lm_image') emit(`state.${name} = PixelArray(${size!})`, `state.${name} = new PixelArray(${size! === 'runtime.pixel_count' ? 'runtime.pixelCount' : size!});`);
          else emit(`state.${name} = array("${f(b, 'TYPE')}", [${f(b, 'TYPE') === 'f' ? '0.0' : '0'}] * ${size!})`, `state.${name} = new ${f(b, 'TYPE') === 'f' ? 'Float32Array' : 'Int32Array'}(${size! === 'runtime.pixel_count' ? 'runtime.pixelCount' : size!});`);
          break;
        }
        case 'lm_array_set': { const i = v('INDEX'), a = v('VALUE'), name = f(b, 'NAME'); emit(`state.${name}[int(${i[0]})] = ${a[0]}`, `state.${name}[${i[1]}] = ${a[1]};`); break; }
        case 'lm_image_point': { const i = v('INDEX'), c = v('COLOR'), name = f(b, 'NAME'); emit(`state.${name}.point(${i[0]}, ${c[0]})`, `state.${name}.point(${i[1]}, ${c[1]});`); break; }
        case 'lm_image_fade': { const a = v('VALUE'), name = f(b, 'NAME'); emit(`state.${name}.multiply_alpha(${a[0]})`, `state.${name}.multiplyAlpha(${a[1]});`); break; }
        case 'lm_image_draw': { const at = v('POSITION'), scale = v('SCALE'), name = f(b, 'NAME'); emit(`pixels.draw(state.${name}, transform=Transform(translate=${at[0]}, scale=${scale[0]}))`, `pixels.draw(state.${name}, { transform: new Transform({ translate: ${at[1]}, scale: ${scale[1]} }) });`); break; }
        case 'lm_fill': { const c = v('COLOR'); emit(`pixels.fill(${c[0]})`, `pixels.fill(${c[1]});`); break; }
        case 'lm_range': { const a = v('START'), z = v('STOP'), c = v('COLOR'); emit(`pixels.fill(${c[0]}, start=int(${a[0]}), stop=int(${z[0]}))`, `pixels.fill(${c[1]}, { start: ${a[1]}, stop: ${z[1]} });`); break; }
        case 'lm_pixel': { const i = v('INDEX'), c = v('COLOR'); emit(`pixels[int(${i[0]})] = ${c[0]}`, `pixels[${i[1]}] = ${c[1]};`); break; }
        case 'lm_clear': emit('pixels.clear()', 'pixels.clear();'); break;
        case 'lm_gradient': { const a = v('A'), c = v('B'); emit(`pixels.gradient(${a[0]}, ${c[0]})`, `pixels.gradient(${a[1]}, ${c[1]});`); break; }
        case 'lm_setting': case 'lm_set': {
          const a = v('VALUE'), name = f(b, 'NAME'), state = b.type === 'lm_setting' || f(b, 'SCOPE') === 'state';
          if (b.type === 'lm_setting' && stage !== 'init') fail(b, 'Put settings inside “when starting”.');
          const pyTarget = state ? `state.${name}` : `v_${name}`, jsTarget = state ? `state.${name}` : name;
          const declaration = !state && !declared.has(name) ? 'let ' : ''; declared.add(name);
          emit(`${pyTarget} = ${b.type === 'lm_setting' ? `setting(${a[0]})` : a[0]}`, `${declaration}${jsTarget} = ${b.type === 'lm_setting' ? `setting(${a[1]})` : a[1]};`); break;
        }
        case 'lm_each': case 'lm_repeat': {
          const name = f(b, 'NAME'), bound = b.type === 'lm_each' ? ['pixels.count', 'pixels.count'] : v('COUNT');
          emit(`for v_${name} in range(int(${bound[0]})):`, `for (let ${name} = 0; ${name} < ${bound[1]}; ${name}++) {`);
          loopDepth++; body('DO'); loopDepth--; js.push('  '.repeat(depth) + '}'); break;
        }
        case 'lm_while': { const a = v('TEST'); emit(`while ${a[0]}:`, `while (${a[1]}) {`); loopDepth++; body('DO'); loopDepth--; js.push('  '.repeat(depth) + '}'); break; }
        case 'lm_if': {
          const a = v('TEST'); emit(`if ${a[0]}:`, `if (${a[1]}) {`); body('THEN');
          if (child(b, 'ELSE')) { emit('else:', '} else {'); body('ELSE'); } js.push('  '.repeat(depth) + '}'); break;
        }
        case 'lm_flow': { const flow = f(b, 'FLOW'); if (flow !== 'return' && !loopDepth) fail(b, 'Place loop control inside a loop.'); emit(flow, flow + ';'); break; }
        case 'lm_mix': case 'lm_wipe': {
          if (f(root!, 'PROFILE') !== 'transition') fail(b, 'Use a Transition program for A/B drawing.');
          const a = v('VALUE');
          if (b.type === 'lm_mix') emit(`pixels.mix(${a[0]})`, `pixels.mix(${a[1]});`);
          else emit(`wipe(pixels, ${a[0]}, reverse=${f(b, 'REVERSE')})`, `wipe(pixels, ${a[1]}, { reverse: ${f(b, 'REVERSE') === 'True'} });`); break;
        }
        case 'lm_copy': { const a = v('START'), z = v('STOP'); emit(`pixels.copy_composition("${f(b, 'SOURCE')}", start=int(${a[0]}), stop=int(${z[0]}))`, `pixels.copyComposition("${f(b, 'SOURCE')}", { start: ${a[1]}, stop: ${z[1]} });`); break; }
        default: fail(b, 'Connect a drawing, variable or control block here.');
      }
    }
    if (!count) line('pass', '// Try adding a block here.', root!, depth);
    declared = enclosing;
  };
  for (const [items, kind] of [[artwork, 'artwork'], [sprites, 'sprite']] as const) {
    const names = new Set<string>();
    for (const item of items) { const name = f(item, 'NAME'); if (names.has(name)) fail(item, `Choose a unique ${kind} name instead of “${name}”.`); names.add(name); }
  }
  for (const art of artwork) {
    const bytes = f(art, 'DATA').split(',').map(Number).join(', ');
    line(`art_${f(art, 'NAME')} = PixelArray([${bytes}], format="rgba8", readonly=True)`, `const art_${f(art, 'NAME')} = new PixelArray([${bytes}], { format: "rgba8", readonly: true });`, art, 0);
  }
  const functionNames = new Set<string>();
  for (const fn of functions) {
    const name = f(fn, 'NAME'), param = f(fn, 'PARAM');
    if (functionNames.has(name)) fail(fn, `Choose a unique function name instead of “${name}”.`);
    functionNames.add(name); declared = new Set([param]);
    line(`def fn_${name}(v_${param}):`, `function ${name}(${param}) {`, fn, 0);
    chain(child(fn, 'DO'), 1); const result = expression(child(fn, 'VALUE'));
    line(`return ${result[0]}`, `return ${result[1]};`, fn, 1); js.push('}');
  }
  for (const sprite of sprites) {
    const name = f(sprite, 'NAME'), param = f(sprite, 'PARAM'), width = Number(sprite.fields?.WIDTH ?? 3);
    if (!Number.isInteger(width) || width < 1 || width > 65535) fail(sprite, 'Sprite width must be a whole number from 1 to 65535.');
    if (['state', 'runtime', 'pixels'].includes(param)) fail(sprite, 'Choose a creation argument other than state, runtime or pixels.');
    for (const callback of ['init', 'render']) {
      stage = 'sprite_' + callback; declared = new Set(callback === 'init' ? [param] : []);
      line(`def sprite_${name}_${callback}(state, runtime, ${callback === 'init' ? 'v_' + param : 'pixels'}):`, `function sprite_${name}_${callback}(state, runtime, ${callback === 'init' ? param : 'pixels'}) {`, sprite, 0);
      chain(child(sprite, callback.toUpperCase()), 1); js.push('}');
    }
    line(`sprite_${name} = Sprite(width=${width}, init=sprite_${name}_init, render=sprite_${name}_render)`, `const sprite_${name} = new Sprite(${width}, { init: sprite_${name}_init, render: sprite_${name}_render });`, sprite, 0);
  }
  for (stage of ['init', 'render']) {
    declared = new Set();
    line('@' + stage, `export function ${stage}(state, runtime${stage === 'render' ? ', pixels' : ''}) {`, root!, 0);
    lines.push(`def ${stage}_program(state, runtime${stage === 'render' ? ', pixels' : ''}):`); locations.push({ line: 1, column: 1, blockId: root!.id });
    chain(child(root!, stage.toUpperCase()), 1); js.push('}');
  }
  const declarations = [...inputs].map(([name, { value }]) => `input_${name} = Input("${name}", default=${Number.isInteger(value) ? value + '.0' : value})`);
  const prelude = [...SDK_IMPORTS.split('\n'), `profile("${f(root!, 'PROFILE')}")`, ...declarations];
  const jsPrelude = [`export const profile = "${f(root!, 'PROFILE')}";`, ...[...inputs].map(([name, { value }]) => `const input_${name} = input("${name}", { default: ${value} });`)];
  return { python: [...prelude, ...lines].join('\n') + '\n', locations: [...prelude.map(() => ({ line: 1, column: 1, blockId: root!.id })), ...locations], javascript: [...jsPrelude, ...js].join('\n') + '\n' };
}

const num = (n: number): Block => ({ type: 'lm_number', fields: { VALUE: n } });
const input = (b: Block) => ({ block: b });
const math = (a: Block, op: string, b: Block): Block => ({ type: 'lm_math', fields: { OP: op }, inputs: { A: input(a), B: input(b) } });
const runtime = (property: string): Block => ({ type: 'lm_runtime', fields: { PROPERTY: property } });
const get = (name: string): Block => ({ type: 'lm_get', fields: { SCOPE: 'local', NAME: name } });
export function blockExample(kind: 'rainbow' | 'pulse' | 'wipe' | 'stars' = 'rainbow'): string {
  if (kind === 'stars') {
    const sequence = (...blocks: Block[]): Block => { blocks.slice(0, -1).forEach((b, i) => { b.next = { block: blocks[i + 1] }; }); return blocks[0]; };
    const remembered = (name: string): Block => ({ type: 'lm_get', fields: { NAME: name, SCOPE: 'state' } });
    const set = (name: string, value: Block): Block => ({ type: 'lm_set', fields: { NAME: name, SCOPE: 'state' }, inputs: { VALUE: input(value) } });
    const white: Block = { type: 'lm_rgb', inputs: { R: input(num(255)), G: input(num(255)), B: input(num(255)) } };
    return JSON.stringify({ blocks: { languageVersion: 0, blocks: [
      { type: 'lm_artwork', fields: { NAME: 'star_image', DATA: '64,64,64,255,255,255,255,255,64,64,64,255' } },
      { type: 'lm_sprite', fields: { NAME: 'star', WIDTH: 3, PARAM: 'position' }, inputs: {
        INIT: input({ type: 'lm_sprite_transform', inputs: { POSITION: input(get('position')), SCALE: input(num(1)), ORIGIN: input(num(1)) } }),
        RENDER: input(sequence(
          { type: 'lm_if', inputs: { TEST: input({ type: 'lm_compare', fields: { OP: '>=' }, inputs: { A: input(runtime('time')), B: input(num(2)) } }), THEN: input(sequence({ type: 'lm_sprite_remove' }, { type: 'lm_flow', fields: { FLOW: 'return' } })) } },
          { type: 'lm_artwork_draw', fields: { NAME: 'star_image' } },
          { type: 'lm_sprite_color', inputs: { ALPHA: input(math(num(1), '-', math(runtime('time'), '/', num(2)))), BRIGHTNESS: input({ type: 'lm_input', fields: { NAME: 'brightness', DEFAULT: 1 } }), TINT: input(white) } },
        )),
      } },
      { type: 'lm_program', fields: { PROFILE: 'animation' }, inputs: {
        INIT: input(sequence({ type: 'lm_batch', fields: { NAME: 'stars', SPRITE: 'star', CAPACITY: 8 } }, set('next_birth', num(0)))),
        RENDER: input(sequence(
          { type: 'lm_if', inputs: { TEST: input({ type: 'lm_compare', fields: { OP: '>=' }, inputs: { A: input(runtime('time')), B: input(remembered('next_birth')) } }), THEN: input(sequence({ type: 'lm_spawn', fields: { NAME: 'stars' }, inputs: { VALUE: input(math(math(runtime('time'), '*', num(7)), '%', runtime('count'))) } }, set('next_birth', math(runtime('time'), '+', num(0.5))))) } },
          { type: 'lm_batch_render', fields: { NAME: 'stars' } },
        )),
      } },
    ] } });
  }
  let render: Block;
  if (kind === 'wipe') render = { type: 'lm_wipe', fields: { REVERSE: 'False' }, inputs: { VALUE: input(runtime('progress')) } };
  else if (kind === 'pulse') render = { type: 'lm_fill', inputs: { COLOR: input({ type: 'lm_hsv', inputs: { H: input(num(0.08)), S: input(num(1)), V: input({ type: 'lm_motion', fields: { FUNCTION: 'pingpong' }, inputs: { VALUE: input(math(runtime('time'), '/', num(2))) } }) } }) } };
  else render = { type: 'lm_each', fields: { NAME: 'i' }, inputs: { DO: input({ type: 'lm_pixel', inputs: { INDEX: input(get('i')), COLOR: input({ type: 'lm_colorwheel', inputs: { VALUE: input(math(math(math(get('i'), '/', runtime('count')), '*', num(255)), '+', math(runtime('time'), '*', num(25)))) } }) } }) } };
  return JSON.stringify({ blocks: { languageVersion: 0, blocks: [{ type: 'lm_program', fields: { PROFILE: kind === 'wipe' ? 'transition' : 'animation' }, inputs: { RENDER: input(render) } }] } });
}
