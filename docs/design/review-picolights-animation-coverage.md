# Picolights animation coverage: bytecode and compiler backlog

Reviewed: 15 October 2026. LightVM `8e1d2ef`; picolights `f6480b4`.

The current bytecode implements a limited part of the proposed language/API and
does not yet cover the original picolights catalogue. **Persistent effect-owned
pixel/history buffers, bounded arrays, initialization, user functions and logical
simulation ticks are already architectural requirements.** Their absence is an
implementation gap, not a new architectural feature proposed by this review.
The opcode names below are suggested ways to implement those requirements.
Most missing Python control flow can compile to instructions already present.

This is an implementation backlog, not a claim that the original Python files
can compile unchanged. Port their behaviour into the stage-based contract;
resolve strings, assets and deployment configuration off-device.

## Scope and specification baseline

Reviewed all **24 non-empty custom animation modules**, including the startup
sequence and `MultiStripFluoresce`, plus their lifecycle, colour and easing
dependencies, the transition wrapper, and `julian1.py`, `julian2.py` and
`sfdl/xmas.py`. The vendored Adafruit `.mpy` catalogue is a separate dependency
catalogue; its full implementations were not reconstructed or certified here.

The language specification has two relevant levels:

- [Current bytecode profile](../control-api.md): implemented restricted
  Python, LBC1/2, i32/f32/bool/RGB8, scalar state, one rendering stage.
- [Language-neutral animation contract](architecture-decision-portable-bytecode.md):
  intended `init`/events/`step`/`prepare`/`shade`/kernel permissions, bounded
  arrays and calls, explicit history, randomness, geometry and logical ticks.
  Several signatures and formats deliberately remain unspecified.

[Composition](feature-proposal-animation-composition.md) and
[A/B transitions](feature-proposal-ab-transitions.md) are future contracts, not
existing bytecode capabilities. The standalone bytecode experiment is historical
evidence; this review uses `lightvm/bytecode.py` and `src/bytecode.c` as the current
implementation.

## Comparison against the proposed language/API

This comparison distinguishes three things: selected architectural requirements,
earlier proposed or explicitly deferred capabilities, and implementation choices
or picolights-specific compatibility details. The first version of this report
described all of them too broadly as “additions”; the distinction matters when
deciding what belongs in the runtime foundation.

### Persistent pixel/history buffers are already required

The 15 October [animation contract](architecture-decision-portable-bytecode.md#flash-and-ram-ownership)
explicitly places “Pixel, particle, history and output buffers” in native
allocations with “retained lifetimes” (line 111). It also requires admission of
all retained pixel/simulation/output buffers and bounded array capacities.
The [stage contract](architecture-decision-portable-bytecode.md#animation-lifecycle-and-function-permissions)
gives `step` permission to advance “mutable state and explicit simulation/history
buffers” (line 153), then says history advances on logical ticks (lines 172–177).
The [shared API](architecture-decision-portable-bytecode.md#shared-api-and-composition)
lists bounded arrays, particle storage and explicit history (line 194).

These clauses establish persistent, readable and writable **effect state whose
elements can be pixels**. Native buffer mapping is also a foundational
[component requirement](architecture-request-component-runtime.md#native-buffers-and-data-flow),
including bounded views, formats, access rights, lifetimes and native transforms.
The source syntax and binary resource descriptors remain to be designed; the
capability itself has already been selected.

Three different buffer roles explain the apparent disagreement between documents:

| Buffer role | Lifetime and access | Current implementation |
|---|---|---|
| Effect-owned pixel/simulation history | Survives logical updates until explicit reset/release; writable during permitted simulation stages, stable while rendering reads it | Missing as an addressable array/buffer capability; persistent scalar registers are insufficient |
| Render working/scratch image | Produces the current contribution under its declared initialization and write rules | One checked RGB working view; cleared each invocation, write-only to bytecode |
| Committed/published image | Retained for consumers, snapshots, hold/failure policy; protected while in use | Native committed/output buffers exist, but bytecode cannot read or use them as history |

In [control.c](../../src/control.c), lines 93–95 bind the guest only to `working`;
line 107 clears it and line 126 copies successful output to `committed`.
[LbcInstance](../../src/bytecode.h) has one buffer view and scalar registers, with no
declared resource table. Thus the shortfall is **no guest-visible persistent pixel
resource and no element-load path**, not merely the presence of `memset`.

The 12 October [composition note](feature-proposal-animation-composition.md)
withdrew implicit retained paint coupled to binary presence/erase composition.
Its lines 38–41 still say filters should own explicit prior simulation state.
The older [control proposal](feature-proposal-control-api.md#addressable-rgb-pixel-control)
describes black-at-start RGB output and removed retention policies; that describes
the earlier output API, not a prohibition on the newer effect-owned history model.
The control proposal's opening explicitly defers future execution/stage semantics
to the 15 October decision. Both documents need clearer scoping where they still
describe scalar-only animation state.

Accordingly, **implement persistent pixel state without waiting for alpha
composition**. Keeping an initialized output frame is compatible with that model
if an effect renders from its own persistent history. Simply removing the output
clear would not supply the resource, readback, tick or ownership contract, and
would change existing LBC1/2 behaviour.

### Feature traceability

| Report item | Existing proposal / architectural basis | Classification and remaining work |
|---|---|---|
| Persistent pixels, typed arrays and history | [RAM ownership](architecture-decision-portable-bytecode.md#flash-and-ram-ownership), [stage permissions](architecture-decision-portable-bytecode.md#animation-lifecycle-and-function-permissions), [native buffers](architecture-request-component-runtime.md#native-buffers-and-data-flow) | **Selected requirement; missing implementation.** Define admitted resources, lifetime/reset, element reads/writes and stage access. `LOAD_ELEM`/`STORE_ELEM` are suggested encodings. |
| Init and state templates | [Lifecycle table](architecture-decision-portable-bytecode.md#animation-lifecycle-and-function-permissions) and RAM ownership | **Selected requirement; partially implemented.** Literal register initialization exists; executable init and non-scalar resources are missing. |
| Logical ticks and stable rendering | [Lifecycle and function permissions](architecture-decision-portable-bytecode.md#animation-lifecycle-and-function-permissions) | **Selected requirement; partial implementation.** Stage entry points exist, but integrated stepping follows presentation cadence. Implement a declared tick/overload policy and history/RNG updates independent of re-rendering. |
| Bounded functions and calls | [Portable execution](architecture-decision-portable-bytecode.md#portable-compilation-and-execution), [shared API](architecture-decision-portable-bytecode.md#shared-api-and-composition) | **Selected language requirement; missing implementation.** Helpers and bounded call storage are explicit. Inlining is an initial lowering strategy; `CALL`/`RET` are implementation choices, not the reason to support functions. |
| Shared typed IR, constant folding and register reuse | [Portable compilation](architecture-decision-portable-bytecode.md#portable-compilation-and-execution) | **Selected compiler responsibilities; partial implementation.** Direct AST emission and a few fusions do not provide the proposed reusable IR and allocation passes. |
| Loops, conditionals and bounded arrays in Python | [Python frontend proposal](feature-proposal-web-editor.md#python), plus stage permissions | **Already proposed language scope; narrow current subset.** Specific support for augmented assignment, tuple unpacking, chained comparisons and range forms is this report's concrete subset recommendation, not frozen syntax in the proposal. |
| HSV, gradients, palettes, interpolation and blending | [Shared colour API](architecture-decision-portable-bytecode.md#shared-api-and-composition) | **Selected library concepts; partially implemented.** RGB/HSV construction and small palettes exist. Channel extraction, inverse conversion and broader helpers need implementation; native opcode versus compiled helper remains a choice. |
| Oscillators, easing, envelopes and smoothing | [Time/signals API](architecture-decision-portable-bytecode.md#shared-api-and-composition) | **Selected library concepts; largely missing from bytecode authoring.** Sin/cos/log/sqrt and exp2/pow are concrete implementation recommendations derived from the originals, not individually mandated opcodes. |
| Owned RNG streams and stateless keyed samples | [Noise/randomness API](architecture-decision-portable-bytecode.md#shared-api-and-composition) and shade purity | **Selected requirement; partial implementation.** `HASH` supplies one stateless helper. Owned streams, seed/stream/tick conventions and their source bindings remain unimplemented. |
| Immutable tables and larger assets | [Flash ownership](architecture-decision-portable-bytecode.md#flash-and-ram-ownership) | **Selected resource category; partial implementation.** RGB palettes exist. Typed resources and wide lookup are suggested implementations; the architecture does not prescribe a 256-entry minimum or this report's encoding. |
| Fill/blend/conversion and native views | [Native buffer data flow](architecture-request-component-runtime.md#native-buffers-and-data-flow) | **Selected capability family; partial implementation.** Whole-output `FILL` exists. Arbitrary admitted views and transforms are missing. Range/copy/max opcodes are optional acceleration of those semantics. |
| Time and geometry binding | [Lifecycle](architecture-decision-portable-bytecode.md#animation-lifecycle-and-function-permissions), [geometry API](architecture-decision-portable-bytecode.md#shared-api-and-composition) | **Selected concepts; partial implementation.** Count/index and local f32 time exist. Synchronized epoch access, timestamp representation and each installation's geometry remain concrete API/binding work. |
| Source inputs, Clone and image gain | [Resource/view model](architecture-request-component-runtime.md#native-buffers-and-data-flow) and shared inputs/transforms | **Fits the selected model; exact APIs still open.** A stable cross-instance image binding, producer ordering, initial-image handoff and gain semantics are specializations to define, not already specified Clone imports. |
| Completion, hold, waits and sequences | [Declarative timelines](architecture-decision-portable-bytecode.md#shared-api-and-composition), [resumable tasks](feature-proposal-control-api.md#resumable-tasks-and-jobs) | **Already planned concepts; detailed animation completion API open.** Legacy cycle events and `.next` need a defined mapping. A flattened sequence can prove behaviour without completing the general task/timeline API. |
| Two-source reveal/crossfade | [A/B proposal](feature-proposal-ab-transitions.md#script-contract-and-examples) | **Already proposed; unimplemented.** RGB sources, U8 destination coverage, indexed/range writes and seekable progress are specified conceptually. Opcode signatures remain open. Use this as the target for reusable transitions; a legacy wrapper adapter is an interim compatibility path. |
| RGBW | [Control colour formats](feature-proposal-control-api.md#addressable-rgb-pixel-control), [device capabilities](feature-proposal-device-support.md#portable-wasm-device-api) | **Already proposed optional capability; unimplemented.** Required for full original Candle coverage, but not a baseline RGB8 guarantee. |
| General layer alpha/readback | [Composition note](feature-proposal-animation-composition.md#decisions-for-the-separate-alpha-work) | **Explicitly deferred design.** Alpha representation and inter-layer semantics remain open. This does not block effect-owned persistent buffers. |

Multiple concurrent components, shared services and live replacement also remain
selected [core requirements](architecture-request-component-runtime.md#core-requirements).
Porting all examples into single packages would not by itself establish complete
architectural compliance. The old globally serialized owner and variable-frame
update descriptions must be scoped to their profiles; the newer architecture
allows independent workers and requires an explicit logical simulation policy.

### Minimum persistent-buffer milestone

Implement the existing contract as one coherent vertical slice:

1. Declare an instance-owned pixel array with bounded, admitted capacity and
   initial values. Its contents survive updates and rendering until a documented
   reset/release; this is instance state, not flash persistence across reboot.
2. Allow init/step to read and write its elements through checked views. Allow
   rendering to read the stable state without advancing it. Give scratch/output
   views their own permissions; specify ordered in-place or previous/next updates.
3. Render or publish through native-owned storage, preserving consumer lifetimes
   and complete-frame visibility. Copy, rotate or share allocations according to
   access/ownership requirements; the architecture does not mandate a fresh
   allocation or unconditional full copy per frame.
4. Extend the manifest, verifier, resource accounting and version/capability checks
   together. A raw load/store opcode without those contracts is incomplete.
5. Verify retention directly: start black; tick 1 paints pixel 0 red; tick 2 paints
   pixel 1 blue. Both colours remain after tick 2 and repeated renders without a
   tick. Change render rate/traversal and slice size without changing history;
   verify reset, instance isolation, stale views and failed-frame publication.

This should precede effect-specific workarounds for Chase, Patchy, Flame and
Starlight. The same resource model then supports Twinkle's star arrays and
sequence image retention.

## What is already available

There are **33 opcodes**, IDs 0–32, in [bytecode.h](../../src/bytecode.h):

| Group | Existing instructions |
|---|---|
| Control and movement | `END`, `RETURN`, `MOV`, `JZ`, `JMP` |
| Integer arithmetic | `IADD`, `ISUB`, `IMUL`, `IDIV`, `IMOD`, `IMIN`, `IMAX` |
| Float arithmetic | `FADD`, `FSUB`, `FMUL`, `FDIV`, `FMOD`, `FMIN`, `FMAX` |
| Conversion/comparison | `ITOF`, `FTOI`, `ILT`, `FLT`, `EQ` |
| Colour and sampling | `RGB`, `HSV`, `PALETTE`, `HASH` |
| Pixel output | `STORE`, `FILL` |
| Fused operations | `HSV_RETURN`, `HUE_RETURN`, `IFMADD` |

`FTOI` truncates; it is not floor. `EQ` accepts matching bool and colour operands
in the VM, although the compiler currently routes comparisons through numeric
coercion and rejects them. Only `range(stop)` in a kernel is accepted by the
compiler. The VM's existing jumps can express much richer control flow.

The package supports 128 registers, 2,048 instructions and 1,024 palette colours,
but each individual `PALETTE` access encodes an **8-bit length**, limiting one
palette to 255 entries. There are eight live scalar parameters. The compiler
allocates new temporaries without liveness-based register reuse.

The renderer clears working RGB on **every invocation** in
[control.c](../../src/control.c), then commits the complete successful frame. There
is no pixel load, retained-paint API, mutable array, init callback, user-function
call or RGBW value. Scalar state persists; output pixels do not form simulation
history.

## The six samples are not six faithful original ports

At the time of this review, the replay suite compared against copies of the
earlier LightVM Python examples. Those copies and their tests have since been
removed. Current replay tests execute the six shared sources in `examples/animations/`.

| Example | Difference from the original picolights implementation |
|---|---|
| Color | Core fill behaviour matches once colour names/random defaults have been resolved. |
| Pattern | Repeat/stretch behaviour is represented, but the original parses run-length colour strings, can generate a random palette, and defaults to fitting. The current example uses a fixed palette and defaults to repeating. |
| Chase | The original writes the next pixel without clearing previous pixels and signals cycle completion. The current sample renders a single moving marker on black. |
| Wipe | The original advances by draw count and `step`, preserves untouched pixels and signals completion. The current sample redraws both ends from elapsed-time progress against black. |
| Rainbow | The original has a colour-derived starting hue, duration and `rainbowio.colorwheel` conversion. The sample uses normalized HSV, speed and a zero initial phase. Its unused original `color_count`/`color_n` settings should not acquire invented behaviour. |
| Twinkle | The original tracks births, positions and per-star colours in bounded arrays; spawns probabilistically from elapsed time; uses a sine fade and channel-wise background maxima. The sample uses deterministic per-pixel phases, cubic fading, fixed warm colours and black background. |

At the time of this review, a separate Rainbow port preserved the old WASM demo.
That duplicate has since been removed; `examples/animations/rainbow.py` is now the single
current picolights Rainbow example.

## Coverage matrix

“Existing operations” means a source adaptation can express the core visual
algorithm with today's instruction set. It does not promise unchanged Python
syntax, legacy timing, inherited pixels or completion notifications.

| Original module/effect | Required work for a faithful usable port |
|---|---|
| [Color](../../../picolights/picolights/effects/animations/color.py) | Existing `FILL`/shade. Resolve named/hex colours off-device; random defaults need an explicit seed/init policy. |
| [Pattern](../../../picolights/picolights/effects/animations/pattern.py) | Existing palettes, arithmetic and indexed writes. Compile run-length strings and fit/repeat options; support bounded generated palettes for random defaults, or specialize a chosen palette at installation. |
| [Ident](../../../picolights/picolights/effects/animations/ident.py) | Existing arithmetic and conditionals can implement the three divisibility bands, with red overriding green overriding blue. General stepped ranges are a compiler convenience. |
| [Locator](../../../picolights/picolights/effects/animations/locator.py) | Existing scalar time, conditions and fill/store. Lower floor and early returns; retain initial flash/blackout and timed marker semantics. |
| [Rainbow](../../../picolights/picolights/effects/animations/rainbow.py) | Colour-to-hue conversion, a compatible colourwheel helper/table, start hue, duration/scale and timing. Easing support belongs to the shared timed-animation binding. |
| [Colors](../../../picolights/picolights/effects/animations/colors.py) | Dynamic palette colour extraction, RGB-to-HSV and shortest-hue interpolation. A fixed palette can instead become precomputed HSV tables. Handle one-colour and degenerate segment lengths explicitly. |
| [Chase](../../../picolights/picolights/effects/animations/chase.py) | Tick/draw counter and completion. Redraw an accumulated prefix for a fixed colour/background; use explicit paint history if colours change or pre-existing pixels must survive. |
| [Wipe](../../../picolights/picolights/effects/animations/wipe.py) | Draw cadence, `step`, end completion and safe range writes. Scalar redraw covers a fixed background; an arbitrary inherited image requires a bound initial image/history. |
| [ColorWipe](../../../picolights/picolights/effects/animations/color_wipe.py) | Seeded choice of four directions, ceil, easing, completion and guaranteed final fill. Preserve the unrevealed source image through an explicit input/history binding. |
| [ZipOut](../../../picolights/picolights/effects/animations/zip_out.py) | Cubic/default or selected easing, ceil, two bounded ranges and completion/hold. Existing arithmetic implements the default curve; inherited pixels need an input image. |
| [Blend](../../../picolights/picolights/effects/animations/blend.py) | RGB-to-HSV for hue-offset colours; boolean conditions; cycle count and final fill; explicit history/input for untouched pixels. Despite its name, this is spatial painting, not a general alpha blend. |
| [Flashy](../../../picolights/picolights/effects/animations/flashy.py) | RGB channel scaling, easing and repeated phase. Its optional `epoc` uses synchronized nanosecond time; preserving that feature requires a native timeline binding and wide timestamp/difference semantics. |
| [Cylon / Eye](../../../picolights/picolights/effects/animations/cylon.py) | Bounded eyes/palette iteration, floor, cubic easing and **per-channel maximum** on overlaps. `BlendStrip` currently calls `naive_blend`, not averaging. Can render by looping over eyes per pixel, or use admitted scratch plus indexed splats. |
| [Matrix](../../../picolights/picolights/effects/animations/matrix.py) | Compile base64 RGB data/dimensions into immutable flash resources; add practical lookup beyond a 255-entry palette. Nested kernel loops and arithmetic can implement scrolling and column-serpentine indexing. Bind the 32×8 geometry and define wrapped-write order for oversized images. |
| [Candle](../../../picolights/picolights/effects/animations/candle.py) | Scalar stochastic state in `step`, logarithm for rotation, channel extraction/scaling/warmth, init seed and two indexed outputs. Full original coverage includes independent white-channel RGBW and its layout binding. |
| [Flame](../../../picolights/picolights/effects/animations/flame.py) | Persistent per-pixel RGB state, loops in `step`, seeded random choices, scalar height random walk and Gaussian sampling. Its Box–Muller path needs log/sqrt/cos, or a versioned equivalent normal sampler. |
| [Starlight](../../../picolights/picolights/effects/animations/starlight.py) | Mutable RGB history, step loops, RNG, abs, RGB-to-HSV and HSV-to-RGB. Read back the quantized RGB history on updates if reproducing its accumulated colour-rounding behaviour. |
| [Twinkle](../../../picolights/picolights/effects/animations/twinkle.py) | Bounded star arrays, init, step iteration, free-slot/position selection, random colours, sine easing, colour scaling and channel maxima. Render with background fill plus active-star stores. |
| [Patchy](../../../picolights/picolights/effects/animations/patchy.py) | Seeded random patch position/colour, retained paint array, bounded range writes, duration and completion. Painting only the newest patch onto the cleared output would be incorrect. |
| [Fluoresce](../../../picolights/picolights/effects/animations/fluoresce.py) | Scalar on/off state and randomized next-switch deadline in `step`; render the remembered state on every frame. Complete with the configured colour. No pixel array is intrinsically necessary for one strip. |
| [MultiStripFluoresce](../../../picolights/picolights/effects/animations/fluoresce.py) | Bounded per-segment state/deadlines, independent random streams, computed subranges and group completion. Can be flattened into one effect; it does not inherently require a general compositor. Preserve gaps through a defined background/input. |
| [Pause](../../../picolights/picolights/effects/animations/pause.py) | Sequence/timeline hold and completion while retaining the preceding image. An empty current kernel commits black; the management pause command alone does not implement an automatically completing sequence stage. |
| [Fade](../../../picolights/picolights/effects/animations/fade.py) | A timed gain on an existing image and completion. The original mutates the physical strip's brightness property; model it as an explicit native gain/transition stage if that property behaviour is required. |
| [Clone](../../../picolights/picolights/effects/animations/clone.py) | Read-only, host-bound source image with known length, offset and coherent publication. Needs producer/consumer scheduling and availability rules; a numeric guest pointer or arbitrary controller lookup is unsuitable. Preserve the untouched destination tail if matching the original. |
| [FlashyStartupSequence](../../../picolights/picolights/effects/animations/sequences.py) | Compose multi-strip fluoresce → 3-second hold → random Patchy → fixed-colour Patchy → solid, with completion and `auto_clear=False` history. A bounded state machine in one package is sufficient initially. |
| [Transition wrapper](../../../picolights/picolights/effects/transition.py) | An evolving target buffer plus retained old image, easing and four reveal masks. A native transition graph or explicit two-image adapter can express it. The wrapper's own target buffer also persists between sparse writes. |

## Implementation choices: opcodes and native-library operations

Names below are **proposed mnemonics**, not allocated binary IDs. Keep operation
signatures language-neutral. A versioned, statically checked native-operation
table could expose several helpers without consuming a separate opcode for each.
The capability requirements are traced above; this table proposes their concrete
implementation. P0 identifies the persistent-buffer foundation to implement first.

| Priority | Addition | Why and minimum semantics |
|---|---|---|
| P0 | `LOAD_ELEM`, `STORE_ELEM` | Implement the selected persistent-array/native-view contract, together with declarations, init and admission. Initially i32, f32, bool and colour; packed u8/u16 storage is useful for compact star state. IDs/descriptors are admitted capabilities, not numeric addresses. Enforce bounds, format, generation and stage permissions. Unlocks Twinkle, Starlight, Flame, Patchy and grouped state. |
| P1 | `LOAD_CONST` / extended table lookup | Typed immutable table/resource descriptors in flash with a length wider than 8 bits. Supports image assets, HSV/curve tables and palettes beyond 255 entries. A 32×8 RGB image already has 256 entries. Splitting it into existing palettes is a possible compiler workaround; a wide lookup makes general assets practical. |
| P1 | `COLOR_CHANNEL` | Extract R/G/B as integers from a colour value. Existing `RGB` only constructs colours; colour-to-int conversion is forbidden. This small primitive enables scaling, tint, max blending and a compiled RGB-to-HSV helper. Extend channel access with the RGBW capability. |
| P1 | `FSIN`, `FCOS`, `FLOG`, `FSQRT` (or native helpers) | Cover sine easing, Candle's exponential rotation and Flame's Gaussian sampling. Define domains, f32 results, fault behaviour, tolerances and bounded execution. Sin/cos may share an implementation; exact cross-board bits require more than simply calling each board's libm. |
| P1 | `RNG_NEXT` with an owned stream | Implements the selected owned-stream API; the opcode is one possible mechanism. Reproducible updates in init/step need seed, algorithm version and stream identity. Uniform/randint/choice can be library wrappers. Existing `HASH(counter, seed)` plus scalar counter is a prototype fallback, not the original random generator; the hash has a 65,521-input period for fixed seed. Never advance a shared stream in shade. |
| P2 optimization | `RGB_TO_HSV`, `COLOR_SCALE`, `COLOR_MAX`, `COLOR_MIX` | Common typed native helpers once rules are fixed. They are lowerable after channel extraction and helper-function support, so these are not all mandatory new opcodes. RGB-to-HSV and scale/max have direct catalogue demand; general mix serves the broader colour contract. |
| P2 optimization | `FILL_RANGE`, `COPY_RANGE`, `STORE_MAX` | Speed up patches, subsets, Clone/history rendering and Cylon splats. Checked load/store loops are enough for correctness. Bulk helpers must retain continuation state and process bounded chunks, as current `FILL` does. Define overlap/collision behaviour. |
| P2 encoding; P1 language support | `CALL`, `RET` | Implements the already selected bounded-function model. `RETURN` currently means “publish one shade pixel”; it cannot serve as a generic function return. Inline small nonrecursive helpers initially; add actual calls with admitted frames, depth and suspension state for the fuller function contract. |
| P2 for all easing options | `FEXP2` or `FPOW` helper | Exponential/elastic easing choices accepted by the old name-based API. Integer powers lower to multiplication; no general power opcode is needed for polynomial easing. Circular easing uses sqrt; bounce is piecewise arithmetic. |
| P3 for full output coverage | RGBW constructor/load/store/fill capability | Candle explicitly controls white independently. This requires a colour/buffer format plus metadata, snapshots, native conversion and driver support. Adding `RGBW` alone is insufficient while buffers and device configuration remain RGB-only. |

A separate current-output `PIXEL_LOAD` is optional if `LOAD_ELEM` can read the
appropriate output/scratch view. Readback of current scratch, persistent
simulation history and a bound external image must remain distinct capabilities.
Do not silently turn the last displayed frame into mutable guest history.

Neither general geometry opcodes nor effect-specific instructions are needed for
these algorithms. Matrix serpentine addressing, wraparound, gradients and the
default cubic easing all lower to arithmetic and branches. Geometry bindings are
still needed to keep wiring and placement outside the portable effect.

## Compiler changes

The main work is in [bytecode.py](../../lightvm/bytecode.py); the public compiler
wrapper, browser frontend and target capability checks must stay in agreement.

| Priority | Change | Lowering / examples |
|---|---|---|
| P1 | Full boolean/comparison subset | Add `<=`, `>=`, `!=`, chained comparisons, `not`, short-circuit `and`/`or`, and matching bool/colour equality. Use existing comparisons/jumps; evaluate chained middle operands once. Define restricted boolean semantics instead of accidentally adopting all Python truthiness. |
| P1 | Loops in permitted stages | Allow bounded loops in init/step/prepare/shade as well as kernels. Add `range(start, stop[, step])`, nested loops, `break` and `continue`. Existing jumps suffice; handle negative steps, zero-step errors and i32 overflow. Keep memory-write permissions separate from permission to loop. |
| P1 | Routine statements and branch joins | `+=`, `-=`, `*=`, etc.; early `return` from void stages/helpers; locals defined on all branches; definite-assignment analysis including zero-iteration loops. An early void-stage return can lower to `END`; helper returns need their own lowering. |
| P1 | Typed helpers and structured values | Inline nonrecursive helper functions initially; scalarize fixed tuples such as `(h,s,v)` and unpacked RGB channels. Resolve approved library imports/defaults/keywords statically. No dynamic tuples, classes or general import execution are needed for these ports. |
| P1 | Basic numerical library | `abs`, floor, ceil, clamp, lerp, fractional phase, positive wrap, floor division on floats, and constant integer powers. Many lower to existing instructions. `int(x)` is not a substitute for floor on negative input. Preserve declared overflow/non-finite behaviour and f32 rounding. |
| P0 | Explicit state/resource declarations | Implement the selected bounded-array/history contract alongside VM access. Declare arrays, immutable tables, history and scratch separately; support checked indexing and `len`. Distinguish fixed/install-time capacity from live values. Initialise admitted storage in `@init`; never allocate per frame. |
| P1 | Register allocation and typed IR | Reuse dead temporaries, fold constants with runtime i32/f32 semantics, eliminate dead code, and hoist only proven invariant expressions. Preserve role/type restrictions and separate rounding. A 120-addition straight-line probe already exceeds 128 registers despite having very little live state. |
| P1 | Off-device configuration/assets | Parse colour names/hex/CSV, Pattern's run lengths, eye palettes, easing names and Matrix base64/dimensions into typed declarations/resources. Random defaults need an explicit install-time choice or deterministic init stream, not an accidental compile-time random choice. |
| P2 | Slice/subset conveniences | Lower `fill_range` and bounded slice assignments into checked loops or bulk helpers; normalize permitted negative indexes explicitly. Do not inherit Python's slice clipping accidentally from raw VM bounds checks. |
| P2 | Compatibility library | Provide progress/cycle helpers, colourwheel variants, normalized easing and random wrappers. The old custom `random_color()` uses a superellipse colourwheel, while Rainbow uses `rainbowio.colorwheel`; treating both as `hsv(random,1,1)` changes colours. A 361-entry custom-wheel table is a practical compatibility option. |
| P2 | Metadata and diagnostics | Report array bytes, registers, code/table bytes, required native functions, render format and capacity limits. Diagnose invalid parameter combinations such as nonpositive duration/step and star capacity above pixel capacity. Current scalar parameter bounds cannot express every cross-field constraint. |

For float comparisons, specify NaN behaviour before lowering `<=` to a negated
`>`: they are not equivalent for NaN. Likewise, constant folding must reproduce
binary32 rounding rather than calculating the entire expression in host f64.

The existing `lightvm/easing.py` functions are not callable from the current
bytecode frontend. Wire approved mathematical helpers into compilation. Follow
the repository's [provenance requirement](feature-proposal-control-api.md#prior-art-and-provenance):
new easing implementations must be independently authored or use verified
permissive sources; do not mechanically translate the GPL-marked original file.

## Runtime and contract work that opcodes cannot replace

The architecture already requires the underlying resources, lifecycle and
scheduling. Remaining API choices below complete those requirements; they do not
make persistence or bounded execution optional catalogue-specific enhancements.

1. **Initialization and resource admission.** Add an init entry and manifest
   resource layout, allocation/initial-value rules, seed policy and reset
   semantics. Constants remain in flash; arrays and RNG state belong to the
   instance. Extend the verifier independently of compiler claims and version
   the package/capabilities when changing encodings or entry layouts.

2. **A defined simulation cadence.** The contract calls for logical ticks, but
   integrated `vm_tick` currently invokes `step` once per presented frame with
   actual elapsed time. Candle, Flame, Starlight, Patchy and original Chase/Wipe
   contain per-draw changes. Choose a declared compatibility update rate or an
   explicit time-based adaptation, with bounded catch-up/drop policy. Preserve
   first-frame, pause and completion rules deliberately. Re-rendering one tick
   must not advance RNG or paint history.

3. **Implement required history and define input-image binding.** Own retained
   RGB state for sparse paint and filters. Arrays can update in-place at logical ticks where ordering
   is specified; previous/next buffers are needed only where dependencies require
   them. Rendering reads a stable state snapshot. Seed an inherited image through
   a native binding at activation/sequence transitions; do not read physical
   driver output implicitly. Preserve current complete-frame fault publication;
   decide separately whether state faults require reset or rollback.

4. **Completion, hold and sequencing.** Represent completed/cycle events in a
   versioned runtime contract, or flatten the startup sequence into one state
   machine first. `END` only ends a stage invocation; it does not finish an
   animation. A sequence hold must preserve its input frame and still progress
   its deadline. Legacy `.next`, group completion and `auto_clear=False` need
   explicit policies.

5. **Bound source views and transforms.** Clone requires a stable source
   contribution, offset/length checks and ordering with the producer. Define
   missing/stopped sources and dependency cycles. Fade needs an image gain or
   explicit native brightness modulation. The transition wrapper needs old and
   target images with defined retention. These can use narrow native bindings
   before implementing the broader scene/compositor API; general alpha is not
   required for the original opaque standalone algorithms.

6. **Time and geometry bindings.** Flashy's synchronized epoch cannot travel
   through the current i32 parameter and absolute-f32-second interface intact.
   Keep wide absolute time native and expose bounded phase/duration differences,
   or define a wide timestamp type. Matrix's 32×8 column-serpentine map and
   Candle's six positions plus one-pixel offset are deployment/layout facts.
   Bind or declare them; validate the pixel count. Define large-image wrap and
   overwrite order for Matrix.

7. **Complete memory and scheduling budgets.** Add arrays, tables/cache needs,
   scratch, calls and any extra retained inputs to admission. Current displayed
   RGB payload is `9N`; a straightforward extension retaining those buffers adds
   `3N` for RGB history, or `4N` with packed-u32 colour elements. That is an
   implementation estimate, not a requirement to copy every buffer: ownership
   transfer or rotation may permit sharing where access and failure rules allow.
   Twinkle's old arrays use `9S` payload bytes for
   `S` stars (f32 birth + u16 position + RGB8); a simple three-word design uses
   `12S`, before an explicit active flag/metadata. Charge each chosen layout.
   Resume bulk work in bounded slices and let the scheduler service management
   between slices as the larger workloads arrive.

## Compatibility decisions and source defects

Use original sources as behavioural evidence, not as an infallible executable
oracle. Record intentional fixes separately from bytecode capability changes:

- Twinkle's position search considers dead stars' stored positions and has an
  unbounded retry loop. Use a bounded free-position policy and explicit active
  state; the original absolute-time zero sentinel is also unsuitable for births
  at relative program time zero. Preserve spawn/expiry order unless deliberately
  changing it.
- `sequences.py` imports a missing `.solid` module. `matrix.py` imports an absent
  `RainbowBuffer`. Some custom effects expect `.color`/colour kwargs that the
  current `PicoAnimation` base does not provide (for example Chase and the
  Fluoresce/ColorWipe paths). These are source integration defects, not missing
  bytecode instructions.
- Candle addresses positions 1 through 6, so a six-pixel binding is too short.
  ZipOut's right-hand subset starts at `last_pixel-length-1`; preserve or correct
  that asymmetry explicitly. ColorWipe's outward centre indexing needs bounds
  handling at full extent. Degenerate Colors gradients can divide by zero.
- Specify RNG algorithm/seed/consumption and colour conversion rounding before
  promising exact replay. The original RGB-to-HSV rounds hue in degrees; current
  HSV quantizes hue and saturation/value differently. A blanket one-channel
  tolerance is not justified for every original effect. Keep a compatibility
  helper when strict conversion behaviour matters.
- A faithful temporal port means matching a defined logical update sequence and
  random inputs. Original hardware loop cadence and physical brightness changes
  cannot be inferred from a class's name alone.

## Additional standalone examples

| Source | Coverage and extra requirements |
|---|---|
| [sfdl/xmas.py](../../../picolights/sfdl/xmas.py) | Moving palette, fixed ornaments/trunk and half-second cadence use existing palette/store operations plus a logical timer. Optional rainbow uses the colourwheel helper. GPIO setup belongs to native device bindings; the declared trigger is not used by the running animation. |
| [julian1.py](../../../picolights/julian1.py) | Rainbow/dash/demon/full_rambo/police/sweep/switch/super use seeded choice, palettes/colourwheel, fills/slices, bounded loops and sequences. Lower sleeps to resumable state/deadlines. `auto_write=True` makes intermediate writes observable: batching all writes into one frame changes those effects. Specify presentation/cadence for the port; fix invalid list/tuple slice shapes and float range bounds. |
| [julian2.py](../../../picolights/julian2.py) | Sparse multi-trail chase and reverse fill need paint history, helper lowering, explicit negative-index semantics and timed phases. Replace sleeps/show with native scheduling/publication; keep setup and wiring in the deployment. |

These demos do not require a new animation-specific instruction family. Faithful
intermediate presentation can require multiple scheduled frames, and the default
16,667 µs presentation interval cannot reproduce every 1–10 ms sleep literally.

## Recommended implementation order

1. **Architecture foundation:** implement admitted persistent pixel/typed-array
   resources, init/reset, checked load/store, stage permissions and declared
   logical tick scheduling. Include the minimal declaration/index/step-loop
   compiler support. Pass the retention milestone above; use accumulating Chase
   and a deterministic patch painter as simple integration cases. This replaces
   the earlier ordering that deferred persistent state until after simple ports.
2. **Compiler and shared library:** typed helpers, boolean/control-flow lowering,
   branch analysis, register reuse, colour channels, approved mathematical/easing
   helpers, immutable resource lookup and owned RNG streams. Port Color, Pattern,
   Ident, Locator, Wipe, Colors, Rainbow, local-time Flashy, Cylon and RGB Candle.
   Add bounded calls as required for the selected function model; inline small
   helpers where doing so preserves declared limits and semantics.
3. **Stateful catalogue:** port original Twinkle, Starlight, Flame, Patchy and
   multi-strip Fluoresce onto the same persistent-resource model. Verify their
   state as well as pixels under different presentation schedules.
4. **Existing integration proposals and remaining binding design:** initial-image
   handoff, source views, completion/hold, startup sequencing, Fade, Clone, the
   proposed A/B reveal/mix capability and synchronized Flashy. Finish the proposed
   RGBW capability for Candle and timed-task lowering for the standalone scripts.
   Keep legacy wrapper adaptations distinct from the reusable transition API.
5. **Measured optimization and broader compliance:** add bulk range/copy/max and
   native colour helpers where profiling justifies them. Full optional easing
   names need exp2/pow. Separately verify concurrent components, service progress,
   replacement and consumer ownership; catalogue coverage alone is insufficient.

Each port should retain a manifest of original controls, intentional renamings,
installation-only settings, behavioural fixes and numerical tolerances. Mark
RGB-only, local-clock or fixed-background variants as partial coverage until
their missing original options are implemented.

## Specification corrections and acceptance evidence

Update these alongside implementation, rather than treating proposal prose as
proof of current support:

- The portable-bytecode decision still says production uses WASM; current code
  and README have moved to LBC1/2.
- The composition note still claims `ctx.pixels` own-step readback; the current
  compiler and opcode set do not implement it.
- Scope the older control/device/editor statements about black-at-start RGB and
  scalar-only state to their original profiles. Cross-reference the selected
  persistent simulation/history resource contract explicitly; the withdrawn
  presence/erase composition design is not a withdrawal of persistent state.
- Reconcile older variable-frame/single-owner descriptions with the selected
  logical-tick and component-worker model, while preserving the earlier profile's
  documented behaviour. Finalize tick/overload policies rather than inferring a
  universal simulation rate from the existing output cadence.
- The current profile's first-FRAME wording needs precision: `frame_step` prepares
  an initial render and then invokes a step/render for the supplied delta, even
  on the first FRAME command. PLAY's initial publication takes a different path.
- Distinguish “legacy LightVM samples preserved” from “original picolights
  catalogue preserved” in documentation and test names.

Verification performed for this review:

- Follow-up architecture comparison traced the backlog to the 15 October
  component/bytecode contracts and the control, device, language frontend,
  composition and A/B proposals. Persistent buffers and bounded functions were
  reclassified as implementation obligations, and the priority order was revised.
- Read the original custom catalogue and dependencies against compiler,
  interpreter, admission checks, scheduler and the documents above.
- Compiler probes confirmed rejection of bool/colour equality, `<=`, `not`,
  channel reads, float floor division, abs, augmented assignment, branch-joined
  locals, non-kernel loops, multi-argument ranges, void early returns, pixel
  readback, ordinary helpers, init imports and a 256-colour palette. A repeated
  120-addition probe hit the register limit.
- Rebuilt `build/bytecode-host`. Its bytecode interpreter C test and all **13**
  compiler/control/pixel unittest methods passed. An initial run against the
  older `build/host` executable rejected LBC packages as invalid WASM; selecting
  the current bytecode build resolved that environment mismatch.
- No full CircuitPython execution or hardware replay of the original catalogue
  was performed. Existing sample tests establish the current baseline only.

For completion, build a replay harness around the original algorithms with fixed
clock samples, random draws and pixel-buffer semantics. Compare state as well as
RGB, including star lifetime/uniqueness, retained patches, overlap rules,
completion/hold and imported backgrounds. Cover short/odd strips, zero/negative
parameters where applicable, count-sized assets, palette boundaries, full star
pools, large elapsed times and source loss. At matching logical ticks, vary render
frequency, pixel traversal and interpreter slice size and require unchanged
simulation. Exercise admission/fault paths for the new resources and measure the
heaviest ports on the actual Pico/ESP32 targets before setting capacity claims.
