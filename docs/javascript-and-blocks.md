# JavaScript and blocks

Choose **Python**, **JavaScript** or **Blocks** in Editor. Each language keeps its
own draft for this session. Examples are filtered to that language. All three
use the same settings, live-input recipes, code checks, preview, deployment and
bytecode downloads. Save locally before closing the browser.

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
| Functions | Named functions, function expressions and arrow functions assigned to module constants; nonrecursive helpers returning a value on every path |
| Callbacks | Exported `init` / `render` functions or const functions; `animation({...})` / `transition({...})` with methods or named callbacks |
| Control flow | `if` / `else`, ternaries, C-style `for`, `while`, `for…of` over managed typed arrays, `break`, `continue`, early render return |
| Operators | `+ - * / %`, comparisons, `=== !==`, unary `+ - !`, standalone `++ --`, assignments and `+= -= *= /=` |
| Logical operators | `&&` / `||` short circuit and return operand values; result branches must have the same LightVM type. Store an effectful left operand in a local first. |
| Maths | `Math.sin/cos/abs/min/max/floor/ceil/round/trunc/random`, `Math.PI`, `Math.E`; `Number`, `float` and `int` |
| Collections | `new Float32Array`, `Int32Array`, `Uint32Array`, `Uint8Array`, `Uint16Array` with a length or literal array, indexing, element writes, `.length` |
| SDK descriptions | `new PixelArray`, `Sprite`, `SpriteBatch`, `Transform`, `ColorTransform`; `input` / `Input`, `ramp` / `Ramp` |

Values have fixed types. Numeric literals are floats by default, including `1`;
`int(1)` explicitly declares an i32. RGB colours and booleans are separate types.
A variable cannot change from a colour to a number, or an array to a scalar.
Pixel/array indices and drawing ranges convert to integers at the API boundary.
Typed integer arrays retain the VM's checked stores rather than JavaScript's
wrapping byte-array stores. Arrays cannot resize. Allocate them in `init`.
The VM uses f32 precision rather than JavaScript's binary64; signed zero,
NaN/Infinity and arbitrary objects are not part of the contract.

JavaScript `%` uses the dividend's sign. Use `repeat(x)` for positive wrapped
motion. Logical operands must be compatible scalar types. Guest strings are
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
function starStart(s, runtime, position) { s.position = position; }
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

## Blocks: explore by connecting

Start with the Rainbow, Breathe or Wipe example. Open a toolbox category and
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
- Reusable one-argument functions with a return value.
- Crossfades, wipes and explicit composition range copies.

Place remembered-state initialization, settings and resource creation in **When
starting**. A remembered field keeps its type throughout the program. Declare
local variables before reading them. A function's argument is a local variable;
functions must remain nonrecursive. Use a whole number or the pixel-count block
for array and image lengths. Blocks use float numbers by default and convert
indices to integers; modulo follows the positive wrapping convention for a
positive divisor. The JavaScript view spells out that wrapping operation.

Errors show a warning on the relevant block. **Show block** centres it in the
workspace. Incomplete sockets or loose blocks are diagnosed; disabled blocks
are not compiled. **Explore the JavaScript** shows a reading guide to the
program. It does not replace the editable workspace or drive its compilation.

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
Compiler initialization still needs network access for Pyodide on a first visit.
Source is limited to 64 KiB; blocks additionally limit workspace size and nesting.
The existing VM code, register, resource and package limits apply to all languages.

`npm test` compiles frontend examples through the real Python backend. With
`LIGHTVM_RECEIVER` set to a current host build, it additionally checks emitted
bytecode in the native VM, including numerical semantics, persistent state,
loop control, animation equivalence and transition seeking. `npm run test:browser`
checks language switching, the real browser worker, preview, and editable blocks
file round trips alongside the existing controller workflow.
