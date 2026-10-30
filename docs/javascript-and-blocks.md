# JavaScript and blocks

Choose **Python**, **JavaScript** or **Blocks** in Editor. Each language keeps its
own draft for this session. Examples are filtered to that language. All three
use the same settings, live-input recipes, code checks, preview, deployment and
bytecode downloads. Save locally before closing the browser.

For a first course, use [Learn with lights](teaching.md). It adds twelve matching
lessons, an offline strip/ring preview, frame and statement stepping, and source
locations for runtime problems. Blocks opens with a smaller beginner toolbox;
**Show all blocks** exposes the full set.

**Save locally** downloads a `.lightvm.json` document containing the original
source, filename, preview settings and input bindings. Import that file to
continue editing. Raw `.py`, `.js` and `.blocks.json` files can also be imported
or exported; raw source files do not contain preview settings or input recipes.
A blocks file contains the editable workspace, including connections, block IDs
and positions. The filename extension identifies its language.

## JavaScript: start small

```javascript
export function render(state, runtime, pixels) {
  pixels.fill(rgb(255, 80, 0));
}
```

Each render starts with transparent pixels. Add an `init` callback for settings
and persistent state, and use time to make things move:

```javascript
export function init(state, runtime) {
  state.speed = setting(0.2, { min: 0, max: 2 });
  state.position = 0;
}

export function render(state, runtime, pixels) {
  state.position += runtime.delta * state.speed;
  const brightness = smoothstep(pingpong(state.position));
  pixels.fill(hsv(0.08, 1, brightness));
}
```

The callbacks can also be expressed as an object:

```javascript
const breathe = seconds => smoothstep(pingpong(seconds / 2));

animation({
  render(state, runtime, pixels) {
    pixels.fill(hsv(0.08, 1, breathe(runtime.time)));
  }
});
```

Use `export const profile = "transition"` with exported callbacks, or
`transition({ init, render })`. Transitions use `runtime.progress` (0–1), without
animation time/delta. They can be scrubbed backwards and forwards in Mixer.

```javascript
export const profile = "transition";
export function render(state, runtime, pixels) {
  wipe(pixels, smoothstep(runtime.progress), { reverse: false });
  // Or: pixels.mix(runtime.progress);
}
```

## JavaScript language and API

Source is parsed as an ES2022 module with an explicitly supported subset. It is
never executed in the browser. This is a LightVM language, with finite binary32
numbers and fixed resource types; it is not a general browser JavaScript engine.

| Feature | Supported forms |
| --- | --- |
| Variables | `let`, `const`, lexical block scope; scalar module constants |
| Functions | Named functions, function expressions and arrow functions assigned to module constants; nonrecursive value helpers and drawing procedures, constant defaults |
| Callbacks | Exported `init` / `render` functions or const functions; `animation({...})` / `transition({...})` with methods or named callbacks |
| Control flow | `if` / `else`, ternaries, C-style `for`, `while`, `for…of` over managed typed arrays or whole-number `range(...)`, `break`, `continue`, early render return |
| Operators | `+ - * / %`, comparisons, `=== !==`, unary `+ - !`, explicit i32 `& \| ^ ~ << >>`, standalone `++ --`, assignments and `+= -= *= /=` |
| Logical operators | `&&` / `||` short circuit and return operand values; result branches must have the same LightVM type. Each operand is evaluated at most once. |
| Maths | `Math.sin/cos/sqrt/log/exp/pow/abs/min/max/floor/ceil/round/trunc/random`, `Math.PI`, `Math.E`; `Number`, `float` and `int` |
| Collections | `new Float32Array`, `Int32Array`, `Uint32Array`, `Uint8Array`, `Uint16Array` with a length and optional `{ fill }` or a literal array; `new FixedArray(length, { fill })`; indexing, element writes, `.length` |
| SDK descriptions | `new PixelArray`, `Sprite`, `SpriteBatch`, `Transform`, `ColorTransform`; `input` / `Input`, `ramp` / `Ramp` |

Values have fixed types. Numeric literals are floats by default, including `1`;
`int(1)` explicitly declares an i32. RGB colours and booleans are separate types.
A variable cannot change from a colour to a number, or an array to a scalar.
Pixel/array indices and drawing ranges convert to integers at the API boundary.
Typed integer arrays retain the VM's checked stores rather than JavaScript's
wrapping byte-array stores. Arrays cannot resize. Allocate them in `init`.
The VM uses f32 precision rather than JavaScript's binary64. Native math preserves
the [specified zero signs and finite domains](numeric-types.md#native-f32-math);
NaN/Infinity and arbitrary objects are unsupported. New native functions need
firmware 0.1.0. Floor/ceil/trunc preserve an f32 result, including values outside
the i32 range. `Math.round(x)` retains the frontend's existing `floor(x + 0.5)`
lowering; it is not a new native operation. Blocks exposes the same operations
through its native-math dropdown and power block.

JavaScript `%` uses the dividend's sign. Use `repeat(x)` for positive wrapped
motion, `wrap(value, length)` for a moving value, or `wrapIndex(position, count)`
for a whole pixel position. Logical operands must be compatible scalar types. Guest strings are
supported only as static SDK descriptions (input names, modes, source A/B, etc.).
No DOM, network, dynamic imports, `eval`, promises, exceptions, classes,
destructuring, closures over locals or arbitrary allocation are available.
Unsupported forms give a source diagnostic rather than being executed.

SDK names are available directly, or through explicit named imports from
`lightvm`, `lightvm/motion`, `lightvm/easing` and `lightvm/transitions`.
Use options objects for named arguments:

```javascript
const phase = input("phase", {
  default: 0, min: 0, max: 1,
  driver: ramp({ duration: 20, mode: "repeat" })
});

export function render(state, runtime, pixels) {
  for (let i = 0; i < pixels.count; i++) {
    pixels[i] = hsv(repeat(runtime.input(phase) + i / pixels.count), 1, 1);
  }
}
```

Core names follow the [Python animation API](animation-profile.md), with
JavaScript spellings `runtime.pixelCount`, `copyComposition`, `setAlpha`,
`multiplyAlpha`, `setChannel`, `hasChannel`, `hueDegrees`, `picoColorwheel`,
`fitStart`, `fitEnd`, `quadraticIn`, `quadraticOut`, `quadraticInOut`,
`cubicIn`, `cubicOut` and `cubicInOut`. Arrays use `.length`; the output also
has `.count`. `pixels.fill(color, { start, stop })` uses half-open ranges.
`random.seed(seed)`, `random.integer(first, last)` (inclusive), and
`random.range(start, stop, step)` expose the same seeded generator as Python.

Sprites use named module helper callbacks and fixed capacities:

```javascript
function starStart(s, runtime, position) {
  s.transform = new Transform({ translate: position, origin: 1 });
}
function starDraw(s, runtime, pixels) { pixels.fill(rgb(255, 80, 0)); }
const star = new Sprite(3, { init: starStart, render: starDraw });

export function init(state, runtime) {
  state.stars = new SpriteBatch(star, 8);
}
export function render(state, runtime, pixels) {
  if (Math.random() < runtime.delta) state.stars.spawn(10);
  state.stars.render(runtime, pixels);
}
```

See the bundled Rainbow, Breathe, Comet and Wipe examples for live inputs,
helper functions, retained images and transitions.

## Explicit numeric values

Use `fixed(decimal)` for Q16.16. The compiler retains the original decimal
token, including digits beyond JavaScript Number precision, and rounds once to
the [fixed-point contract](numeric-types.md). `fixed(0.00000762939453125)` rounds
to zero; `fixed(0.0000076293945312500000001)` rounds to one raw unit. An expression
such as `fixed(runtime.delta)` converts its finite f32 value instead.

`int(q)` truncates fixed toward zero; `float(q)` converts to f32. `fixedBits(q)`
and `fixedFromBits(i)` preserve the raw 32-bit word; `fixedSqrt(q)` returns fixed.
Mixed fixed/float/integer arithmetic needs an explicit conversion. Fixed settings
and live-input wire types are unsupported; convert their values at the boundary.

Bitwise operations require i32 operands, such as `int(31) & int(7)`. Right shift
is signed and shift counts must be 0..31; invalid counts fault. `>>>`, automatic
JavaScript bitwise coercion and masked shift counts are unsupported. These are
the shared core's typed operations. JavaScript `%` retains dividend sign for
all supported numeric types; Python and Blocks modulo retain divisor sign.
The Blocks learning view explicitly lowers that difference.

```javascript
export function init(state, runtime) {
  state.levels = new FixedArray(runtime.pixelCount, { fill: fixed(0.25) });
  state.copy = new FixedArray(runtime.pixelCount);
}
export function render(state, runtime, pixels) {
  state.levels.fill(fixed(0.5), { start: int(0), stop: runtime.pixelCount });
  state.copy.copyFrom(state.levels);
  for (let i = int(0); i < pixels.count; i++) {
    const q = state.copy[i];
    pixels[i] = rgb(int(q * fixed(255)), i & int(31), 0);
  }
}
```

Fixed-array fills/copies are checked and resumable. Copies require equal lengths.
Integer array fills also check the destination range: Uint8 0..255, Uint16
0..65535 and Uint32 0..2147483647 within the shared signed i32 domain. They never
silently wrap narrow stores.

Ordinary `for (let i = 0; i < pixels.count; i++)` loops can use the bulk path,
as can immutable local count aliases. An explicit `int(0)` is not required for
this form. The frontend proves the count is stable and exactly representable,
while expressions still see the expected f32 counter value. Mutable bounds,
modified counters, break/continue or effects can keep the loop ordered.
Compiler advice reports selection or a reason for ordered execution, including
while loops, plus resource/plan estimates; it is not an FPS claim.

Blocks offers exact decimal fixed literals in a text field, signed integer
literals, conversion and bitwise blocks, all six numeric array kinds, array
length, typed array fill/copy, native math and power. Pixel/repeat loop counters are
i32 in both direct Blocks compilation and the optional JavaScript learning view.
This keeps eligible numeric gathers and conditional pixel work on the same bulk
path. Fixed/scalar bitwise features, extended maps and native math all belong to the
0.1.0 baseline. Target checks run before deployment.

## Blocks: explore by connecting

Start with the Rainbow, Breathe, Stars or Wipe example. Open a toolbox category and
drag blocks into the workspace. Value blocks fit into sockets; drawing and
control blocks connect vertically. **When starting** runs once. **Every frame**
runs for each animation frame or transition evaluation. Unpainted pixels remain
transparent. Clear means transparent; filling with black paints opaque black.

The palette includes:

- RGB, HSV, colour wheels, individual pixels, fills, ranges and gradients.
- Elapsed time, frame delta and transition progress; repeat, bounce, easing,
  interpolation and randomness.
- Numbers, arithmetic, comparisons, boolean logic, choices, per-pixel loops,
  counted loops, while loops and loop control.
- Local variables, remembered state, frozen settings and live inputs.
- Fixed numeric arrays, retained images, fading trails and transformed drawing.
- Sprite definitions with **when created** and **each frame** sections, bounded
  batches, creation arguments, transforms, colour/opacity and explicit removal.
- Shared immutable RGBA artwork, supplied as comma-separated channel bytes.
- Reusable calculations and drawing functions with up to three named inputs.
- Crossfades, wipes and explicit composition range copies.

Place remembered-state initialization, settings and resource creation in **When
starting**. A remembered field keeps its type throughout the program. Declare
local variables before reading them. A function's argument is a local variable;
functions must remain nonrecursive. Use a whole number or the pixel-count block
for array and image lengths. Blocks use float numbers by default and convert
indices to integers; modulo follows the positive wrapping convention for a
positive divisor. The JavaScript view spells out that wrapping operation.

Sprite definitions and shared artwork sit beside the program block. Create a
batch directly in the program's **When starting** section. A sprite definition
takes one creation argument, accessible with a local-variable block. Remembered
values inside its callbacks belong to that sprite. Set fixed geometry/style in
**when created**; its **each frame** clock measures time since birth. Remove a
sprite explicitly when its lifetime ends. Shared artwork accepts 1–1024 RGBA
pixels (four integer bytes from 0 to 255 per pixel). The Stars example demonstrates
creation, fading, expiry, slot reuse and a live brightness input.

Errors show a warning on the relevant block. **Show block** centres it in the
workspace. Incomplete sockets or loose blocks are diagnosed; disabled blocks
are not compiled. **Explore the JavaScript** shows a reading guide to the
program. It does not replace the editable workspace or drive its compilation.

Successful compilation can also show **Performance notes**. These are advisory;
they do not disable preview or upload. Notes link to the Python/JavaScript source
or the responsible block. Mixer **Layer performance** separates completed layer
updates from distinct presented contributions and identifies valid cached images.
Rates need two samples from the same generation; neither rate predicts maximum FPS.

## Compiler design and validation

The frontend contract is `LoweredSource`: a restricted intermediate Python
representation plus a source-location map. Python remains the existing authoring
frontend. Acorn parses JavaScript into a checked ESTree tree; the frontend maps
its syntax and SDK descriptions to the intermediate representation. The blocks
frontend independently validates and walks serialized block structures. It does
not pass through the JavaScript parser or generator.

All routes enter the same checked `PortableCompiler`, optimizer, register
allocator, resource admission and LBC1 emitter. The intermediate Python is parsed
as compiler input, never executed. Blocks carry block IDs through diagnostics;
JavaScript errors map back to the originating statement. Firmware and bytecode
formats are unchanged. `while` is an additional shared compiler primitive, using
the existing conditional jumps and resumable instruction budget. An infinite
loop never completes its frame; it does not execute during compilation.

The browser worker packages both frontends and loads the existing pinned
Pyodide backend. Blockly is packaged locally and loaded when opening Blocks.
Pyodide and CodeMirror are also packaged locally. Serving the complete editor
folder supports a first lesson without internet access.
Source is limited to 64 KiB; blocks additionally limit workspace size and nesting.
The existing VM code, register, resource and package limits apply to all languages.

`npm test` compiles frontend examples through the real Python backend. With
`LIGHTVM_RECEIVER` set to a current host build, it additionally checks emitted
bytecode in the native VM, including numerical semantics, persistent state,
loop control, animation equivalence and transition seeking. `npm run test:browser`
checks language switching, the real browser worker, preview, and editable blocks
file round trips alongside the existing controller workflow.

For the numeric contract, set `LIGHTVM_NUMERIC_RUNTIME` to the native
`test_platform` executable. `numeric-frontends.test.ts` compares raw words,
arrays, pixels, faults and scalar/bulk selection, including the Blocks learning
view. `tools/check_numeric_frontends.mjs` checks the built production worker in
Chrome/Pyodide and executes each emitted package in that native VM. The shared
compiler file manifest includes `numeric.py` for both packaging and worker fetch;
an isolated import test prevents either list from silently omitting a dependency.
