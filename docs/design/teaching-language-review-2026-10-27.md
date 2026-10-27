# LightVM as a first programming environment

27 October 2026. Review and proposed teaching contract, based on source commit
`20d3d8a2f541ca7fa67f9541d44320a5394fa585` and a fresh native host build.
This document proposes work; it does not claim that proposed syntax, debugging
tools or optimisations are implemented.

Implementation follow-up: [completed changes, evidence and remaining external
gates](teaching-implementation.md). The findings below remain the historical
review of the earlier source state.

## Recommendation

Build a course around **predictable programs that produce visible results** on
25–100 LEDs. Preserve the current typed, bounded VM and its generic pixel maps.
Prioritise language correctness, useful errors and natural loop/function syntax
before further effect-specific FPS work. A learner should be able to add a
condition, extract a helper, reverse a chase or add another colour without
learning an optimiser's preferred spelling.

Use Python, JavaScript and Blocks as three ways into the same concepts. Learners
choose one; they do not need to learn all three simultaneously. Cross-language
examples should implement the same problem, with idiomatic source in each
language. Explicit fixed-point numbers, bit operations, sprite capacities and
bytecode limits belong in extension material.

The most urgent findings are:

1. Python `for` target semantics are incorrect, independently of optimisation.
2. Some accepted Python Boolean expressions emit bytecode rejected by the VM.
3. Natural JavaScript counting loops miss the bulk backend.
4. Procedures, ordinary Python range forms and branch-defined variables are
   missing from the beginner-friendly subset.
5. Blocks has only one-argument value functions and exposes advanced numeric
   machinery alongside beginner concepts. Runtime faults lack teaching context.

The resulting deliverables should be a small beginner example library, a shared
language conformance corpus, and a curriculum performance matrix at realistic
pixel counts. Keep the existing sophisticated examples as a later gallery.

## Evidence and limits

Reviewed the Python frontend, JavaScript lowering, independent Blocks lowering
and learning view, register allocation, bytecode instruction definitions,
loader checks, scalar execution, prepared pixel maps, drawing/resource APIs,
motion/easing, settings/live inputs, examples and relevant tests. Hardware
drivers and service implementations were considered through their existing
contracts; this is not a new audit of every network/storage path.

The [reproducible probe runner](../../tools/review_teaching.mjs) and its
[native companion](../../tools/review_teaching.py) record **56 authored cases**,
both compiler optimisation modes, and **252 native attempts** at 25, 50 and 100
pixels. Some attempts intentionally demonstrate rejection or faults. The
[complete evidence](../benchmarks/teaching-review-2026-10-27.json) includes
original and lowered sources, diagnostics, package sizes, manifest floors,
resource/register/instruction counts, output hashes, state and work counts.

All cases reaching native execution had equal scalar/optimised observations
after excluding execution costs. This includes cases where both paths reject or
fault, and **does not establish that source semantics are correct**. In
particular, both modes reproduce the Python loop bugs. Bands, rainbow and ring cursor
probes also produced equal pixel hashes across all three languages at all three
sizes; Python and JavaScript state cursors matched too.

Native probes use the production VM through `test_platform`, one worker,
deterministic randomness, time 1.25 s and delta 0.25 s. The harness also passes
those values to init, unlike production's zero-time initialization. These are
one-render probes, not frame-rate, long-duration, compositor-publication or
physical-device tests. A reduced 100,000-unit invocation budget makes the
intentional infinite loop quick to test; the default runtime limit is 10,000,000.
`work` describes the render; dispatch counts include init plus render.
The harness exposes scalar state, fixed arrays, images and output pixels; it
does not dump raw contents of every other numeric-array kind. Existing native
tests supply additional array coverage.

Validation additionally passed 56 frontend/native replay tests and six CTest
groups: platform, bulk numeric, semantic floors, easing, work accounting and
independent emitter. These existing tests provide broader coverage; they did
not previously catch the new source-level findings. No firmware was flashed.

To reproduce with an installed CMake, C compiler, Node 22.13+ and dependencies:

```sh
cmake -S . -B build/teaching-host -DLIGHTVM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/teaching-host --parallel
LIGHTVM_NUMERIC_RUNTIME="$PWD/build/teaching-host/test_platform" \
  node tools/review_teaching.mjs build/teaching-review.json
LIGHTVM_RECEIVER="$PWD/build/teaching-host/lightvm" \
  LIGHTVM_NUMERIC_RUNTIME="$PWD/build/teaching-host/test_platform" \
  npm test -- web/src/languages/frontends.test.ts web/src/languages/numeric-frontends.test.ts
ctest --test-dir build/teaching-host --output-on-failure \
  -R 'platform_contract|bulk_numeric_contract|numeric_work_contract|independent_emitter_contract|semantic_floors_contract|easing_contract'
```

The review machine needed temporary CMake/Ninja packages through `uv`, with its
cache in `/tmp/lightvm-review-uv`. Configure `Python3_EXECUTABLE` to a persistent
Python installation when using that route: the first configure selected a
temporary interpreter, causing five test launches to fail before running. All
six groups passed after correcting that build setting. The runner uses Node's
experimental TypeScript transformation only to load the actual frontends; it
never executes learner JavaScript.

## Findings that affect teaching

### F1 — Fix Python iteration semantics before teaching loops

In [PortableCompiler.statements](../../lightvm/portable.py#L633), the source target is also the loop's induction
register. It is incremented after the body and retains the exclusive bound.
The following accepted program produces red=3; Python's result is red=2:

```python
for i in range(3):
    pixels[i] = rgb(40, 0, 0)
pixels[0] = rgb(i, 0, 0)
```

Similarly, assigning `i = 7` in a `range(3)` loop makes it execute once, whereas
Python still executes three iterations. Probes `py-counter-after-loop` and
`py-counter-reassigned` confirm both problems with optimisation on and off.
Python explicitly separates iteration from assignments to the loop target.
[Python for-statement reference](https://docs.python.org/3/reference/compound_stmts.html#the-for-statement)

Use a private induction value and assign the visible target at each iteration.
Preserve the last body assignment on exit, `break`, `continue`, nested reuse of
target names, and zero-trip behaviour. An empty loop must not fabricate a new
value for its target. Add definite-assignment diagnostics where a possibly
unbound variable cannot be represented. Pixel-map exit handling must implement
the source contract, including target liveness, rather than treating its private
cursor as the user variable. Update tests that encode the abandoned behaviour.

### F2 — Never report success for a package the loader will reject

`if 2 and 3: ...` and `if 2 or 3: ...` compile without diagnostics, then fail
native admission (`load=false`). [Boolean lowering](../../lightvm/portable.py#L361) creates a bool destination but
copies an unconverted final numeric operand into it. The VM's typed `MOV` check
correctly rejects that package. Using the expression as an RGB argument instead
fails compilation with the unhelpful message `incompatible types`.

Implement Python operand-valued short circuiting for compatible scalar types,
with lazy evaluation exactly once. Explicitly diagnose unsupported mixed result
types. Blocks should have Boolean-checked logic sockets and Boolean results.
JavaScript already implements operand-valued logic but rejects effectful left
operands; lower these through a temporary rather than asking the learner to do
so. Preserve evaluation order and RNG consumption. Python's documented `and`/
`or` return operands, not mandatory bools.
[Python Boolean reference](https://docs.python.org/3/library/stdtypes.html#boolean-operations-and-or-not)

### F3 — Natural JavaScript should get the same optimisation opportunity

[JavaScriptFrontend.rangeLoop](../../web/src/languages/javascript.ts#L188) only recognises explicit `int(0)` counters and a
narrow set of bounds. Ordinary `let i = 0` lowers to a float `while`; the shared
map recogniser only sees `range`. Even the bundled JS Rainbow remains scalar.
The performance advice also misses ordinary while-loop reasons because map
reports are populated by the Python `for` path.

| Same HSV loop | Bulk maps | Dispatches at 25 / 50 / 100 | Render work at 100 |
| --- | ---: | ---: | ---: |
| Python, optimised | 1 | 5 / 5 / 6 | 1,505 |
| Python, scalar | 0 | 231 / 456 / 906 | 1,905 |
| Natural JavaScript, either mode | 0 | 331 / 656 / 1,306 | 2,305 |
| JavaScript with `int(0)`, optimised | 1 | 5 / 5 / 6 | 1,505 |
| Blocks “for each pixel”, optimised | 1 | 5 / 5 / 6 | 1,505 |

These are deterministic execution counts, **not speedup factors or measured
FPS**. Native work, math cost and output transfer still matter.

Recognise provably bounded whole-valued JS counters without changing observable
f32 semantics. Pixel counts are bounded well below f32's exact-integer limit.
Keep a private integer cursor and supply the expected float value to expressions
where necessary. Do not capture a changing JS loop bound, reorder effects or
silently change overflow behaviour. Track immutable local aliases such as
`const count = pixels.count`. Use shared loop analysis before map selection;
there is no need to replace the entire compiler to obtain this benefit.

### F4 — Equivalent conditions should not require an optimiser lesson

A direct `if`/`else` with one `pixels[i]` assignment in each arm stays scalar:
816 dispatches at 100 pixels. Initialising a colour, changing it in the branch,
then doing a common store uses a map: 6 dispatches. A colour-returning helper
also maps with 6. All three produce the same image.

Extend [the map recogniser](../../lightvm/portable.py#L682) to recognise exclusive branch stores to the same proven destination/index and
normalise them to a common final store. Preserve lazy faults, colour evaluation
order and last-write behaviour. Later support a guarded store for `continue`
or a missing `else`; skipping a write must preserve transparency or existing
persistent image contents, not paint black. Never classify feedback, reductions
or source mutation as independent just because the resulting image looks simple.

### F5 — Support ordinary decomposition and everyday Python

The [shared helper lowering](../../lightvm/portable.py#L400) rejects `paint(pixels)` in both text languages if the helper draws and returns no
value. Returning a meaningless zero is a poor lesson in functions. Support void
procedures, `return` and fallthrough, alongside value functions. Infer effects
through inlining so drawing inside a procedure still gets analysed. No VM call
stack is required for this change.

Python also rejects `range(start, stop[, step])`, iteration over a managed array,
chained comparisons, ordinary module constants, and a name introduced in both
arms of an `if` and then read afterward. Those are useful next edits, not obscure
language features. Add these in a small defined subset, preserving Python
evaluation rules. Merge same-typed branch assignments; reject reads missing a
definition on a reachable path. Continue to reject dynamic allocation and
arbitrary objects explicitly.

### F6 — Explain state, boundaries and failures at the source level

Frames begin transparent; a sequence of pixel writes inside one render produces
one frame. A loop is not a delay or a visible animation. Locals are per call;
`state`/`ctx` values persist; settings require restart; live inputs do not.
These distinctions should be demonstrated deliberately in the first half of
the course.

Out-of-range and negative indexed accesses fault. Python `%` wraps a negative
offset positively for a positive count; JavaScript `%` can remain negative.
`pixels[(0 - 1) % pixels.count]` succeeds in Python and faults in JavaScript.
This is a language distinction to explain, not an excuse to change JavaScript
remainder semantics. `repeat()` gives consistent normalized circular motion.

[Runtime execution](../../src/animation.c#L71) currently collapses errors to `portable execution fault`.
The learner needs a location, operation and useful values: “pixel 25 is outside
0–24”, “divide by zero”, “brightness must be 0–1”, or “this loop used its frame
work budget”. Compile-time source maps already exist for JS and block IDs;
extend them through bytecode generation and optimisation. Record the failing
PC **before** incrementing it, including map-body/native continuation origins.
Keep debug metadata beside the source/package hash in the editor where possible;
avoid making every device carry full source text.

### F7 — Align the learning material with the shipped contract

The README still says multiple languages remain future work despite introducing
Python, JavaScript and Blocks at the top. The PixelArray guide refers to an
“API 4” for optimised loops while the current bytecode guide explicitly describes
release floors and no separate drawing API revision. Correct these statements
when publishing the teaching material. Also distinguish the bundled JS Comet's
simple `max(0, 1 - delta * rate)` fade from truly time-based exponential decay:
they differ under uneven frame intervals. Do not promise equal trails at every
frame rate merely because their decay expression mentions delta.

## Proposed beginner course

Assume no programming experience and one configured strip or ring. Recommend
twelve 45–60 minute sessions, adjustable to age and setting. Teacher setup covers
wiring, controller connection, a modest brightness limit, pixel count and physical
index zero. Learners should also have an offline strip/ring preview; that preview
is proposed work, not the existing physical Preview feature.

Use Predict → Run → Investigate → Modify → Make for each session. First trace a
small working example, change one idea, then make something personal. This
follows the Raspberry Pi Foundation's guidance to model debugging, read code
before writing and use a structured progression.
[Computing pedagogy](https://www.raspberrypi.org/teach/pedagogy)

Use visible outcomes to make variables and selection concrete; micro:bit's
teaching resources similarly connect stored values to tangible inputs and
outputs. The sequence and lighting projects below are our design proposal, not
a validated LightVM classroom study.
[Teaching variables](https://microbit.org/teach/topics/variables/)

| Lesson | Obvious problem and concept | Starter and natural enhancement | Evidence of understanding |
| --- | --- | --- | --- |
| 1. Give the lights instructions | Make one pixel orange and the last blue. Sequence, calls, zero-based indices. | Fill a background, overwrite two pixels; swap the order and predict which wins. Ring: label the seam physically. | Explain why `count - 1` is the last pixel and why order matters. |
| 2. Name what changes | Change a theme without editing six numbers. Variables, expressions, colour values. | Local `colour`, `first`, `last`; use half the strip. Add a second theme and change its brightness. | Identify assignment versus comparison; explain what value a name holds. |
| 3. Repeat a useful action | Paint any length without copying lines. Counted loops. | Paint a gradient using `i / count`. Change to stripes/every second pixel; compare with `fill`. | Trace indices for five pixels; explain why the loop produces one completed frame. |
| 4. Make a choice | Make a progress bar change colour near the end. `if`, `else`, comparisons, Boolean conditions. | Two-colour bands; add a warning band and an `and` condition. Ring: divide into sectors. | Predict output at an exact threshold; explain exclusive bounds. |
| 5. Make time visible | Change colour once a second without blocking the controller. Time, remainder, periodic behaviour. | Slow alternating beacon; change to a countdown or a smooth breathe. | Explain why `while True` and a delay inside render are wrong for this callback model. |
| 6. Remember movement | Move a dot at the same speed on fast and slow frames. Persistent state, initialization, delta. | `position += delta * speed`; wrap on a ring, bounce on a strip. Reverse speed and handle a delayed frame. | Distinguish local position from remembered position; state speed in pixels/second. |
| 7. Give an idea a name | Reuse a colour rule in two places. Parameters, return values, decomposition. | `band(index, split)` returns a colour; `draw_marker(pixels, position, colour)` is a procedure after F5. | Explain arguments versus local variables; modify the helper without copying its body. |
| 8. Make it adjustable | Change a design while it runs. Inputs, ranges, settings versus state. | A live progress/brightness value with a useful default; init setting for trail length. | Explain which changes need restart; clamp an input intentionally. |
| 9. Remember several values | Give pixels different histories. Fixed-size arrays and indexed access. | A three-entry palette, then a brightness array. Modify one element and draw all elements. | Explain index versus stored value and why an array is created once. |
| 10. Stop when done | Find the first available sparkle slot. `while`, compound conditions, `break`, `continue`. | Scan an occupancy array; limit by its length; fill all slots to exercise the “none free” case. | Demonstrate termination and boundary safety, then explain a broken loop. |
| 11. Give lights a life | Create fireflies/comets that fade and disappear. Randomness, lifetime, controlled accumulation. | Fixed spawn interval with seeded random positions; retained image for trails; ring seam crossing. | Replay with a seed; distinguish one random decision per event from one per rendered pixel. |
| 12. Make a small installation | Combine, test and explain a personal design. Planning, debugging, reuse. | Ring lighthouse; strip scanner with a trail; progress/celebration display; later music-reactive lantern. | Explain the loop, condition, variable and function; run at 25 and 100 pixels and make one deliberate enhancement. |

Lessons 1–6 form a useful short workshop. Lessons 7–12 deepen programming logic
instead of becoming an SDK tour. Noise, easing families, sprites, composition,
transitions and audio are optional follow-on modules. The current Sweep/Stars
examples are good eventual destinations, but too many concepts for first contact.

Each lesson package should contain a runnable start, a small solution, two
extensions, one deliberate bug, expected output at a frozen time, and a short
trace question. Supply one matching artifact per language. Use 25 and 100 as
first-class examples, plus 50; add 12/16/24 pixel ring checks to cover common
small geometries. Do not assume an even count or divisibility by three.

## Concrete syntax to teach

Use `state` consistently in course text; Python already permits that callback
argument name. Existing documentation can keep `ctx`. Keep the callback scaffold
visible but initially collapsed in an editor lesson view: “starting runs once;
render draws one frame.” Do not invent hidden `time`/`pixels` globals.

**Now** means implemented, with caveats in the findings. **Add** means a proposed
feature that must not appear as runnable material until its acceptance tests pass.
Blocks descriptions below are actual block labels or explicitly marked proposals,
not a new textual blocks language.

| Concept | Python | JavaScript | Blocks | Status / decision |
| --- | --- | --- | --- | --- |
| Render entry | `@render` / `def draw(state, runtime, pixels):` with profile/imports | `export function render(state, runtime, pixels) { ... }` | Animation → every frame | Now; choose one callback form first. |
| Local variable | `level = 0.2` | `let level = 0.2;` / `const colour = rgb(40, 8, 0);` inside callback | set variable `level` to `0.2` | Now. Use decimals for quantities that later receive decimals. |
| Persistent value | In init: `state.position = 0.0` | In init: `state.position = 0;` | when starting → set remembered `position` to `0` | Now; explain the lifetime explicitly. |
| Single pixel | `pixels[0] = rgb(40, 8, 0)` | `pixels[0] = rgb(40, 8, 0);` | pixel `0` colour RGB `40, 8, 0` | Now. RGB bytes 0–255; HSV hue turns, saturation/value 0–1. |
| Whole surface / range | `pixels.fill(colour, start=0, stop=10)` | `pixels.fill(colour, { start: 0, stop: 10 });` | fill pixels from `0` up to `10` with colour | Now; exclusive stop in all three. |
| Pixel loop | `for i in range(pixels.count):` | `for (let i = 0; i < pixels.count; i++)` | for each pixel `i` | Now; repair Python semantics and JS optimisation. Do not teach `int(0)` as the normal JS spelling. |
| Offset/step loop | `for i in range(1, pixels.count, 2):` | `for (let i = 1; i < pixels.count; i += 2)` | **Proposed:** count `i` from `1` up to `count` by `2` | Add Python and Blocks; document exclusive stop, including negative steps. |
| Decision | `if i < 10: ... else: ...` | `if (i < 10) { ... } else { ... }` | if / then / else | Now. Add Python same-typed branch-local merging. |
| Combined condition | `i >= 3 and i < 10` | `i >= 3 && i < 10` | compare ≥ / < joined with and | Now for Boolean operands; repair numeric forms, make effectful JS operands natural. |
| Chained comparison | `0 <= i < pixels.count` | `i >= 0 && i < pixels.count` | two comparisons joined with and | Add Python, evaluating the middle expression once. |
| Persistent update | `state.position += runtime.delta * speed` | `state.position += runtime.delta * speed;` | set remembered position to position + seconds since last frame × speed | Now with stable types. |
| Calculation function | `def band(index, split): ... return colour` | `function band(index, split) { ... return colour; }` | **Proposed:** function `band` with `index, split`, return colour | Text languages now; Blocks currently one argument only. |
| Drawing procedure | `def marker(pixels, at, colour): pixels[at] = colour` | `function marker(pixels, at, colour) { pixels[at] = colour; }` | **Proposed:** procedure `marker`, statement call | Add; distinguish a command from a value socket. |
| Counter-controlled while | `while i < len(state.values):` | `while (i < state.values.length)` | while comparison | Now; update `i`, and teach a termination reason. |
| Early exit | `break`, `continue`, render `return` | `break;`, `continue;`, render `return;` | leave loop / next iteration / finish this frame | Now; scalar execution is valid. |
| Array | Init: `array('f', [0.0] * runtime.pixel_count)` | Init: `new Float32Array(runtime.pixelCount)` | remember values as decimal array, length number of pixels | Now; size fixed, allocation only at init. |
| Array traversal | `for value in state.values:` | `for (const value of state.values)` | **Proposed:** for each value in remembered array | Add Python/Blocks; JS now. |
| Random position | `random.randrange(pixels.count)` | `random.range(pixels.count)` | **Proposed:** random integer from 0 up to pixel count | Text now; Blocks currently offers random 0–1. |
| Repeat/bounce | `repeat(runtime.time / 4.0)` / `pingpong(...)` | `repeat(runtime.time / 4)` / `pingpong(...)` | repeat 0 → 1 / bounce 0 ↔ 1 | Now; one shared helper meaning across languages. |
| Frozen setting | Init: `setting(5.0, minimum=0.0)` | Init: `setting(5, { min: 0 })` | setting speed default 5 | Now; add simple min/max Blocks fields. |
| Live control | Module `Input(...)`; `runtime.input(handle)` | Module `input(...)`; `runtime.input(handle)` | live input level default 0.5 | Now; Blocks numeric only, advanced descriptions later. |
| Assertion/debugging | `assert 0 <= i and i < pixels.count` | `assert(i >= 0 && i < pixels.count);` | **Proposed:** check condition / inspect value | Text assertion now; source-aware fault/trace UI proposed. |

### Two-colour bands: write the obvious program

These complete text examples work now and intentionally use the branch-store
style learners are likely to write. They should gain automatic map lowering in
F4 without changing course source. Avoid introducing a temporary just to satisfy
the current optimiser.

```python
from lightvm import profile, render, rgb
profile("animation")

@render
def draw(state, runtime, pixels):
    for i in range(pixels.count):
        if i < 10:
            pixels[i] = rgb(40, 0, 0)
        else:
            pixels[i] = rgb(0, 0, 40)
```

```javascript
export function render(state, runtime, pixels) {
  for (let i = 0; i < pixels.count; i++) {
    if (i < 10) {
      pixels[i] = rgb(40, 0, 0);
    } else {
      pixels[i] = rgb(0, 0, 40);
    }
  }
}
```

Blocks: Animation → every frame → for each pixel `i` → if (`i` < `10`)
→ then pixel `i` colour RGB(40,0,0), else pixel `i` colour RGB(0,0,40).

Predict pixel 9 and pixel 10; change ten to half the count; add a third band;
extract the rule as a function. A ring turns the same program into sectors.

### A ring cursor: isolate timing from drawing

These complete sources use an existing helper and work now:

```python
from lightvm import profile, render, rgb
from lightvm.motion import repeat
profile("animation")

@render
def draw(state, runtime, pixels):
    phase = repeat(runtime.time / 4.0)
    position = int(phase * pixels.count)
    pixels[position] = rgb(40, 8, 0)
```

```javascript
export function render(state, runtime, pixels) {
  const phase = repeat(runtime.time / 4);
  const position = Math.floor(phase * pixels.count);
  pixels[position] = rgb(40, 8, 0);
}
```

Blocks: Animation → every frame → pixel (repeat 0 → 1 of
(elapsed seconds ÷ 4) × number of pixels), colour RGB(40,8,0).
The pixel block converts its index to an integer.

This is four seconds per revolution, regardless of count. Negate time to reverse.
For a strip, use `pingpong(time / 4) * (count - 1)` for four seconds per leg.
Explain why a wrapping phase multiplies by `count` while an inclusive bouncing
phase multiplies by `count - 1`. Multiplying a phase that can equal 1 by `count`
and indexing it is a reproducible off-by-one bug.

With real f32 arithmetic, validate index endpoints at large clocks as well as
small example times. For production teaching helpers, specify and test rounding
behaviour near the seam; do not assume the mathematical interval alone proves
every floating-point multiplication safe.

### Functions worth teaching and optimising

Start with a user-written calculation whose definition is short enough to read:

```python
def band(index, split):
    if index < split:
        return rgb(40, 0, 0)
    return rgb(0, 0, 40)

# Inside render:
for i in range(pixels.count):
    pixels[i] = band(i, 10)
```

```javascript
function band(index, split) {
  if (index < split) return rgb(40, 0, 0);
  return rgb(0, 0, 40);
}

// Inside render:
for (let i = 0; i < pixels.count; i++) {
  pixels[i] = band(i, 10);
}
```

Blocks needs the proposed two-argument function block to express that exact
abstraction. Its current one-argument function can demonstrate `bounce(seconds)`
returning `pingpong(seconds / 2)`; that case is in the executable corpus.

Then introduce `repeat`, `pingpong`, `clamp01`, `lerp`, `smoothstep`, `fill` and
`gradient` as readable library functions. Show one ordinary implementation
before hiding it behind a name. Keep scalar helpers as inspectable guest code
and accelerate their typed results generically. Avoid a separate native opcode
for every named animation or easing equation.

Useful small additions are `wrap_index(index, count)` / `wrapIndex(index, count)`
and a matching Blocks value, with positive integer wrapping and an error for
nonpositive count. These are proposed helpers. They make reverse/multi-pixel
ring effects clear while preserving each language's `%` meaning. For a pixel
blob crossing the seam, the existing `draw(..., overflow="wrap")` is better than
resetting a clipped sprite. Expose overflow/sampling choices in Blocks image and
batch drawing; those Blocks currently omit the text API's options.

## Feature-by-feature disposition

| Feature family | Current contract and teaching effect | Recommendation |
| --- | --- | --- |
| Numeric literals/types | Python integer/float distinction; JS/Blocks numbers f32 by default; separate bool/colour/fixed/reference types. | Retain explicit resource types. Explain whole versus fractional quantities, add helpful assignment diagnostics; consider safe local i32→f32 inference only after semantic analysis, never implicit fixed coercion. |
| Arithmetic | Typed + − × /; Python integer `//`; modulo; no general Python `**` lowering. f32 finite results, wrapping i32. | Cover ordinary ranges first. Consider `**`→checked pow for common expressions; preserve intermediate rounding. Defer overflow/fixed details to extension notes. |
| Assignment and scope | Stable variable types; Python branch names discarded; JS lexical scopes and const checks. Tuple assignment unsupported. | Correct definite assignment and branch joins. Add simple tuple swap later if demanded by examples, with simultaneous RHS evaluation. |
| Comparisons and logic | Single comparisons, lazy branches, ternaries; numeric Python logic bug; chained comparison missing. | F2 and chained-comparison lowering first. Typed Blocks sockets prevent colour/logic mistakes. |
| Repetition | Python one-argument range, while, break/continue, nesting; JS broad for/for-of; Blocks count/each/while. | Fix F1, add range forms, common loop analysis. Leave ordered loops available and performant enough for 100 pixels. |
| Functions | Static nonrecursive inlining; return values required; user defaults/keywords unsupported; Blocks one argument. | Add procedures and multiple Blocks parameters, then scalar defaults/keywords. Diagnose recursion with the call chain. Preserve helper source locations after inlining. |
| Module names/imports | Restricted SDK/math/random imports; Python scalar module constants rejected; JS pure numeric constants supported but e.g. module `const RED = rgb(...)` rejected. | Add immutable scalar/colour constant expressions consistently. Retain explicit import allowlists. |
| Collections | Managed numeric arrays, fixed sizes, checked indexing/stores; Python list/tuple syntax only as initializer. No append, slice or general list object. | Keep fixed storage. Add direct managed-array iteration and a small immutable palette literal form; do not imply full Python lists or JS arrays. |
| State and allocation | Fields/resources directly in init, no allocation in render. | Keep this model and show it visually. Diagnose “create this remembered field in when starting” rather than compiler internals. |
| Pixels and colour | Checked indices, transparent new frame, opaque indexed colour writes, half-open fills. | Teach bounds and layer paint semantics. Do not silently clamp array indices. Include colour units and typed colour picker/preview in Blocks. |
| Drawing and retained images | Native fill/gradient/point/draw, transforms, alpha, periodic drawing; retained PixelArray. | Teach after ordinary loops. Add Blocks wrap options, and explicit explanations of clipping and fractional position. |
| Time | Stable per-call seconds/delta; zero-time initial frame in production; no sleep/catch-up callback. | Start absolute-time effects, then delta integration. Never teach “one step per frame” as a speed contract. Bound long-lived state phases. |
| Settings/live inputs | Init settings fixed; live snapshots stable per render; rich input recipes. | Keep both but name their distinct roles in the lesson UI. Start with a manual slider; audio/network setup optional. |
| Random/hash/noise | Seeded stateful RNG, deterministic hash and coherent noise. | Seeded randomness first; simple event timing; hash/noise later. Keep RNG order stable through optimisation. Add bounded random integer Blocks. |
| Easing/math | Broad checked native math and guest easing, consistent scalar/bulk evaluation. | Sufficient for a rich course. Teach only repeat/bounce/lerp/clamp initially; avoid approximate math or fixed-point as prerequisites. |
| Sprites | Fixed batches, per-sprite state, callbacks, shared artwork and transforms. | Extension after arrays/functions. Use 1–3 pixel sprites sized for short strips; diagnose full capacity visibly. |
| Composition/transitions | Layers, transparent output, A/B progress, native mixing. | Optional capstone module, after one standalone animation succeeds. |
| Assertions/errors/debugging | Assertions without messages; generic runtime faults; compile diagnostics/source locations. | Add failure reasons, a bounded trace/inspector, single-frame/step execution and selected-variable watch. Do not stream a print per pixel over controller transport. |
| Unsupported general features | Exceptions, dynamic objects/classes/closures, arbitrary imports, async/DOM, dynamic allocation. | Keep out of the core course. Offer explicit diagnostic explanations; do not build compatibility shims to simulate a full Python/JS runtime. |

## Bytecode and VM review

The 96-byte LBC1/layout-4 container uses 8-byte typed instructions, up to 128
physical registers, 2,048 instructions, 16 resources and 256 native sites within
32 KiB. The output consumes one resource. Settings and state have distinct roles;
references are validated resource identities, not guest pointers. Animation
packages have init/render entry functions; user helpers inline, and **there is
no guest CALL instruction**. Existing RETURN support is not an implemented
general call stack. See the [encoding reference](../bytecode-emitter.md).

| Bytecode family | What it already provides | Teaching-driven action |
| --- | --- | --- |
| END/RETURN, MOV | Invocation exit, typed transfers and register allocation. | Void helper support can inline with patched exits. Keep types checked; add compiler/emitter validation so F2 cannot escape compilation. |
| JZ/JMP, comparisons | Resumable branches, loops, early returns and lazy evaluation. | Enough for range steps, chained comparison, short circuit and branch joins. Prefer frontend/data-flow fixes over new control opcodes. |
| i32/f32 arithmetic and conversion | Small ordinary numeric expressions with checked exceptional operations. | Keep finite, exact documented semantics. Preserve ordering when hoisting, folding or fusing; do not replace divide with reciprocal multiply casually. |
| RGB/HSV, min/max/hash | Useful generic per-pixel computation. | Suitable primitives for lessons and extensions. Separate typed colour from integer packing in beginner material. |
| LOAD/STORE/LENGTH, resource fill/copy | Checked fixed-size collections and resumable bulk operations. | Retain bounds/generation checks; add high-level syntax without expanding the heap model. Consider generic numeric-array fill/copy API parity beyond FixedArray. |
| NATIVE | RNG, colour utilities, drawing, inputs, noise with typed signatures/effect permissions. | Keep effect classification explicit. Do not hoist or parallelise RNG/state writes. Improve fault detail at the call boundary. |
| PIXEL_MAP (39) | Prepared independent pixel evaluation, branches/gathers, invariant caching, weighted chunks and optional parallel execution. | Keep the architecture. Extend source recognition/common-store normalisation before adding another execution backend. Add guarded/ranged maps only with verifier changes and a real measured need. |
| Q16.16/bitwise/native math (40–71) | Explicit numerics and shared scalar/bulk evaluation with feature floors. | Already more than sufficient for beginner material. Make these advanced choices, not mandatory speed hacks. |

Map limits are 128 body instructions and a worst first-pixel path of 1,024 work
units; helpers may expand into those limits. Prepared plans consume heap even
when bytecode is small. Chunking preserves scheduler cooperation; complete
frames publish atomically, while persistent state is not rolled back on faults.
These are good properties to retain. Debug stepping should be an explicit mode
using source mappings, separate from ordinary optimised playback.

The current optimiser cannot map loops with multiple/conditional stores, nested
loops, scatter, feedback, persistent mutations, stateful RNG or arbitrary native
calls. Many exclusions are semantically necessary; **a scalar loop is not an
error**. An enhancement such as updating an array before rendering it should
remain straightforward. Consider two clear phases—update state, then render—when
that decomposition improves the lesson, not merely to make a performance badge
green.

For 25–100 pixels, optimise the whole experience: preparation time, first frame,
edit/restart latency, command responsiveness, memory and jitter as well as FPS.
Map setup/parallel dispatch can outweigh savings on tiny workloads; measure
thresholds on each target. A cost model can select scalar or bulk at compile or
admission planning time when both satisfy the contract. Do not add an implicit
allocation-failure fallback or keep old experimental bytecode paths.

Possible later bytecode work is narrow: a generic typed remainder operation if
JS's inlined remainder expansion proves costly; an explicit guarded/ranged map
representation if common-source lowering cannot express it; compact debug fault
metadata. A VM CALL stack, effect opcodes such as “rainbow”, JIT, dynamic heap and
approximate arithmetic are not justified by this curriculum.

## Prioritised implementation plan

| Priority / work package | Concrete change and likely files | Completion gate |
| --- | --- | --- |
| P0: semantic foundation | Fix F1/F2 and definite assignment in `lightvm/portable.py`; keep typed emitter/loader checks; add independent Python-semantic expectations. | Normal, empty, negative, reassigned, nested and early-exit loops; both Boolean outcomes, mixed-type errors and effect counts; every successful generated package admits on a supported target. |
| P1: natural lesson syntax | Range start/stop/step, void helpers, constants, array iteration, chained comparisons; Blocks multiple parameters/procedures and range blocks. | Lessons 1–10 compile in all three languages with direct syntax; no fake return values, no forced `int(0)` in ordinary JS lessons. |
| P1: useful feedback | Compile error catalogue; failing PC/op/reason and source/block mappings; deterministic host preview, frame-step and a bounded variable inspector. | A beginner can locate/fix an out-of-range index, changed type and runaway loop without bytecode knowledge; stop/reset remains responsive. |
| P1: natural code performance | Shared loop analysis, JS number-loop proof, same-index branch-store merging; advice for all loop shapes. | Natural JS loop and equivalent Blocks/Python use the same appropriate path; adding a pure helper/condition preserves output and map eligibility. |
| P2: curated learning surface | Lesson selector, small initial Blocks toolbox, typed sockets/name pickers, matching start/solution files; local save plus offline compiler assets. | Start from a blank lesson, reconnect/import and resume; no first-session dependency on a working CDN; current advanced toolbox remains available deliberately. |
| P2: extensions | Ring wrapping blocks/helper, seeded bounded random Blocks, generic array conveniences, frame-independent trail example. | Ring seam/reversal and dropped-frame checks; arrays/resources created only at init; seeded outcomes repeat. |
| P3: measured VM refinements | Small-count plan thresholds, invariant/copy propagation and code-size controls; investigate remainder expansion and ordered array loops. | Demonstrated latency/CPU or memory benefit on curriculum workloads, exact semantic parity, no service-latency regression. |

Start with P0 and a thin slice containing solid colour, bands, moving dot and
one reusable function. Then publish the first six lessons and observe real
beginners before expanding the toolbox. The full course is ready only when the
expected modifications pass as well as the pristine starters.

The project is an experimental PoC. Correct source semantics and update tests
and documents rather than retaining earlier loop behaviour behind a compatibility
flag. Recompile affected sources. If any proposed container/storage change makes
saved experimental state incompatible, document export/wipe/reprovision instead
of implementing a migration. Most recommendations require no format change.

## Acceptance workloads and measurement plan

Freeze a matrix of lesson source plus common edits, not just canonical effects:

| Workload | Learner's next edit | Required execution behaviour |
| --- | --- | --- |
| Solid / bands | Add else-if, use local count, move colour rule into a function. | Same pixels and source semantics; eligible pure loops stay optimisable. |
| Gradient / rainbow | Reverse, add stripe condition, change spacing, use three-colour palette. | Gathers/branches remain lazy and bounds-checked; no language-specific performance cliff. |
| Dot / ring lighthouse | Negative speed, two dots, wider beam crossing seam. | Correct index zero seam and signed motion; arbitrary supported count. |
| Progress / timer | Exactly 0/1, paused time, restart, live-input changes. | No off-by-one store; defined initial frame; settings versus live input clear. |
| Fireflies | Double density, seed random choices, fill all slots. | Bounded memory and event rate; deterministic RNG; termination when no slot free. |
| Comet / brightness array | Longer trail, faster motion, a delayed frame. | Explicit retention; decay based on elapsed time. Use `exp(-rate * delta)` for multiplicative decay when teaching time-independent attenuation; acknowledge byte rounding and that swept paths need sampling. |
| Search / while | Remove increment or use <= length. | Bounded failure with actionable location; prior complete display preserved, controller still usable. |
| Palette / neighbour effect | Update brightness in place or use two buffers. | Ordered dependence preserved; explain why in-place and double-buffered simulations differ. |

Run at 25/50/100 pixels plus 1, 2, 12, 16, 24, 31, 32, 33, 63, 64 and 65 to
catch endpoints, rings and chunk boundaries. Use irregular deltas (0, 16, 33,
250, 1,000 ms), exact period boundaries, negative motion, odd counts, and long
clocks. Check matching output/state/faults between scalar and optimised builds
on the same target. Cross-language equality applies to matched algorithms;
intentional numeric differences, like negative `%`, need separate expectations.

For accepted Python constructs, add a small independent reference trace using
CPython for loop/assignment/short-circuit semantics and a recording pixel stub.
Do not use CPython's unrestricted numbers as an oracle for LightVM's defined
i32/f32 arithmetic. For Blocks, verify direct compilation and the generated
learning view describe the same behaviour; currently the view is a reading aid,
not the compilation input. Include source maps and saved block round-trips.

Measure the attached device first, then RP2040/RP2350/ESP32 variants as available.
Record firmware/source hashes, compiler mode, clock/profile, count, output type,
brightness, worker count, settings, seed and timestamps. Compare matched runs;
record render CPU/elapsed time, p50/p95/p99 frame intervals, presented rate,
command latency, prepare/first-frame time, package size, heap/plan bytes and
stack peak. Avoid treating a static cached contribution as a fresh calculation
or equating bytecode dispatch reduction with FPS improvement.

Proposed product targets, subject to those measurements: core examples and one
ordinary enhancement sustain 60 presented frames/s at 100 pixels on the chosen
baseline controller; reserve headroom for controls/output and aim for p95 render
CPU below half the 16.7 ms frame interval. Where output hardware sets a lower
limit, record that explicitly. Advanced compositions can have a separately stated
30 Hz target. Track edit-to-first-frame and interactive-stop latency separately;
do not claim these budgets are achieved from this host review.

Finally, run a small observed beginner pilot. Ask participants to predict a
five-pixel trace, reverse a dot, add a band, extract a helper and repair a bounds
error. Record where they need help. A higher FPS number cannot compensate for a
language rule or error message that repeatedly prevents those tasks.
