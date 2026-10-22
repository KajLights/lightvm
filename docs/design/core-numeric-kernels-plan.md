# Core numeric types, bulk kernels and native math

22 October 2026. **Status: S0–S5 implemented.** Stable numeric gathers, lazy
conditional kernels and native f32 math join the scalar fixed-point foundation.
Python, JavaScript and Blocks share the explicit numeric contract, with an
independent full-profile emitter fixture. See the
[S2–S4 validation record](numeric-s234-validation-2026-10-22.md) and
[S5 integration record](numeric-s5-validation-2026-10-22.md) for measured
attached-Pico scope. The complete multi-board release matrix (S6) remains open.

The [numeric contract](../numeric-types.md),
[operation catalogue](numeric-operations.json),
[emitter requirements](../bytecode-emitter.md) and
[workload specification](numeric-workloads.md) freeze the foundation scope.
The foundation began on LightVM `bdc1045` and was rebased conflict-free onto
validated Pico repair `a9c8611`; the foundation patch was unchanged. S1 links
`src/numeric.c` through the shared core and adds verified scalar execution,
fixed arrays, declaration expressions and Python authoring under feature floor
1.2.0, preserving the repair's memory, scheduling and ownership design.
The [S1 validation record](numeric-s1-validation-2026-10-22.md) covers independent
oracle comparisons, lifecycle/fault tests, host regressions, ASan/UBSan and Pico
build/SRAM/stack gates. S1 made no physical numeric-performance claim; S2–S4 adds
its own firmware identities and measurements without changing the repair archive.

Source baselines: LightVM `1d9b7ea`; sibling picolights
`f6480b4845b2ba3af4fbf0ffb1f398d362dda772`. Paths into picolights below assume
the existing sibling checkout. The separate
[Pico performance plan](pico-performance-plan.md) retains ownership of its
scheduler, preparation and physical-validation work.

## Objective and scope

Extend the typed core so ordinary animation code can use predictable fixed-point
arithmetic, indexed resources, conditional pixel calculations and a useful native
math library efficiently. Establish the contract and measurements in LightVM
before building another source-language compiler in a sibling repository.

The first delivery comprises an explicit Q16.16 value type, ordinary integer
bitwise operations, broader verified pixel kernels, and native math justified by
our existing effects. It preserves the current separation between animation code,
the execution engine and hardware output. The linked S0 contract now fixes the
numeric rules; the remaining kernel and integration work follows the stages below.

## Design principles

1. **Animation code owns the effect.** Native operations perform arithmetic,
   sampling and drawing. They do not choose colours, durations, motion paths,
   spawning rules or animation-specific scheduling. Keep polynomial easing,
   `repeat`, `pingpong` and interpolation as ordinary helpers where that is
   sufficient. See [motion](../motion.md).
2. **The core is independent of source languages.** Public names, units,
   instructions and tests describe mathematical and rendering operations.
   Frontends own syntax, scope, implicit conversions and language-specific
   behaviour. There are no source-language execution modes, special callback
   names or hidden numeric switches in the core.
3. **Types and effects are explicit.** Preserve f32 and i32 as useful types.
   Fixed-point is opt-in. The loader verifies operand types, resource access and
   kernel independence; compiler annotations alone grant no permissions.
4. **Optimization preserves the chosen contract.** Scalar and bulk execution
   agree on values, lazy branches, faults and observable writes. Reordering,
   reassociation and changing number representations require proof, not an
   assumption that the resulting pixels look close enough.
5. **Rendering is bounded and allocation-free.** Reserve resources during
   preparation. Charge every plan, scratch buffer and continuation against
   admitted memory or audited stacks. Bound work between scheduler returns.
6. **Ownership determines concurrency.** One job owns a VM's mutable state.
   Parallel pixel tasks share only stable inputs and disjoint destinations.
   Preserve frame publication, generation checks and retained-buffer lifetimes.
7. **Measure useful progress on the target.** Report render CPU time, completed
   contributions, displayed freshness, service gaps and memory. A host speedup,
   fewer dispatches or a faster output presentation counter is insufficient.
8. **Prefer one current design.** Follow [AGENTS.md](../../AGENTS.md): no obsolete
   interpreters, migration machinery or compatibility aliases for abandoned
   experiments. Keep the single application release contract.

## Requirements grounded in existing code

These are workload and design references, not instructions to transplant the
older class hierarchy, frame allocation or hardware access into the VM.

| Existing source | Requirement it demonstrates | Proposed response |
| --- | --- | --- |
| [Rainbow](../../../picolights/picolights/effects/animations/rainbow.py) and [TimedAnimation](../../../picolights/picolights/effects/helpers.py) | Capture progress once, reuse spatial scale, evaluate an independent colour for each pixel | Hoist stable inputs and execute typed pixel expressions in bulk |
| [Pattern](../../../picolights/picolights/effects/animations/pattern.py) | Fit or repeat a colour table with index conversion and wrapping | Checked numeric-array reads inside kernels; preserve modulo semantics |
| [Colors](../../../picolights/picolights/effects/animations/colors.py) | Prepare artwork once, then gather from it by index | Reuse prepared resources; avoid regenerating static tables per frame |
| [Colour helpers](../../../picolights/picolights/colors/_colors.py) | Sine, cosine, powers, signed wrapping and piecewise colour conversion | Native general math and lazy conditional kernels |
| [Colour blending](../../../picolights/picolights/colors/_blend.py) | Channel interpolation and mask/shift arithmetic | Explicit i32 bitwise operations and defined rounding |
| [Candle](../../../picolights/picolights/effects/animations/candle.py) | Damped state, logarithmic random steps, wrapping and interpolation | Fast math with state/RNG updates kept ordered |
| [Flame](../../../picolights/picolights/effects/animations/flame.py) | Gaussian sampling uses log, square root and trigonometry; rendering includes conditional writes | Native math; separate independent work from RNG and retained-pixel effects |
| [Current Stars](../../examples/animations/stars.py) | A capacity-limited sprite batch retains birth/lifetime, position, transform and tint information for short-lived three-pixel stars | Evaluate deterministic procedural reconstruction to remove per-star records and frame-dependent birth attempts; see the Stars optimisation proposal below |
| [Twinkle](../../../picolights/picolights/effects/animations/twinkle.py) | Fixed-size typed arrays, age calculations, conditional visibility and sparse writes | Preserve compact resources and use this as an ordered-control benchmark |
| [Starlight](../../../picolights/picolights/effects/animations/starlight.py) | Reads and modifies earlier pixel state with randomness | Negative optimization fixture: retain scalar order |
| [Matrix](../../../picolights/picolights/effects/animations/matrix.py) | Wrapped addressing and odd/even coordinate branches | Gather-based destination traversal where equivalence is proved; do not parallelize arbitrary scatter writes |
| [Current motion/easing](../../lightvm/motion.py), [easing](../../lightvm/easing.py) and [Sweep](../../examples/animations/sweep.py) | Normalized mathematical helpers and elapsed-time motion already fit the core model | Extend these contracts without native effect-specific shortcuts |

The inspected picolights effects use floating-point and integer arithmetic; they
do **not** establish an existing Q16.16 contract. Fixed-point is a new engineering
choice for deterministic fractional arithmetic in four-byte storage. Its speed
must be measured separately on RP2040 and RP2350.

Keep source provenance accurate. The sibling's `easing.py` carries third-party
attribution and a GPL notice. Use it to identify required mathematical coverage;
continue LightVM's independently written equation-based helpers rather than
copying that implementation. Preserve applicable attribution whenever source is
actually reused. Do not claim that existing effects already use new arithmetic.

## Current constraints

- [The core](../../src/bytecode_core.h) has i32, f32, bool, packed RGB8 and Q16.16.
  Addition/subtraction/multiplication of i32 wrap; integer division faults on its
  exceptional overflow. S1 adds exact fixed arithmetic and integer bitwise/checked
  shifts to scalar execution; S2 admits their verified use in pixel maps.
- [Portable execution](../../src/portable.c) and
  [map execution](../../src/portable_map.c) implement overlapping arithmetic
  switches for existing arithmetic. New numeric operations share one evaluator.
- [Map recognition](../../lightvm/portable.py) accepts up to 128 body
  instructions, forward conditional branches, stable numeric-array loads and one
  common final pixel store. Runtime validation independently checks that subset.
- Compact plans are decoded once during admission and charged to the VM heap.
  Bounded worker scratch, weighted chunks and checks between pixels keep render
  work resumable. Straight-line invariants retain ordered first evaluation.
- The shared finite-f32 math set includes abs, rounding, sin/cos, sqrt, log/exp,
  pow and min/max. JavaScript and Blocks route supported math to this evaluator;
  complete fixed authoring parity is covered by S5.
- Packages admit at most 128 registers, 2,048 instructions, 16 resources and
  32 KiB. Retain these limits in the first delivery; use evidence before raising
  them. Native math should reduce instruction pressure rather than conceal it.
- Current inputs and settings use the existing scalar types. There is no need
  to extend their wire formats to introduce an internal fixed numeric type.

## Proposed numeric contract

### Representation and authoring

Use signed two's-complement Q16.16: `value = signed_raw / 65536`, with range
`[-32768, 32768 - 1/65536]`. Add a distinct verified type, four-byte register/state
storage and a four-byte fixed-array resource kind. Never treat a numeric value
as a resource handle or pointer.

S1 provides explicit `fixed(...)` construction and `FixedArray(count, fill=...)`
initialization descriptions in the Python SDK. Equivalent explicit
JavaScript/Blocks authoring joins the same contract in S5. These describe compiled operations;
there is no new runtime object system. Array sizes remain i32 and are resolved
during admission. Array handles remain static in this delivery.

Existing literals retain their current types. Mixed fixed/f32/i32 arithmetic
requires an explicit conversion; do not silently convert an existing animation.
Existing settings and input snapshots can be converted once at the relevant
initialization/frame boundary. Direct fixed-valued settings and live input wire
types are deferred; unsupported declarations must produce a clear diagnostic.

### Exact arithmetic rules

Freeze these rules as vectors and an independent integer reference model:

| Operation | Proposed behaviour |
| --- | --- |
| Fixed literal | Quantize its numeric literal directly to Q16.16, nearest with ties to even; no intermediate f32 rounding. Range-check the quantized result |
| i32 to fixed | Exact scaling by 65536; fault if outside the fixed range |
| f32 to fixed | Round the represented finite f32 value to nearest, ties to even; range-check the rounded result |
| fixed to i32 | Truncate toward zero |
| fixed to f32 | IEEE binary32 rounding to nearest, ties to even; raw low bits may be lost for large magnitudes |
| Add/subtract/negate | Wrap the raw result modulo 2^32, as the existing fixed-width integer arithmetic does |
| Multiply | Form the signed 64-bit raw product, divide by 65536 toward zero, then wrap the result to 32 bits |
| Divide | Form the signed 64-bit scaled numerator, divide toward zero; zero divisor or out-of-range quotient faults, matching checked integer-division overflow policy |
| Modulo | Floored remainder with the divisor's sign, calculated in wide arithmetic; zero divisor faults |
| Comparison/min/max | Compare signed raw values; comparisons return bool |
| Integer bitwise | AND/OR/XOR/NOT operate on all 32 i32 bits. Left shift wraps; right shift is arithmetic. Shift counts outside 0..31 fault |
| Raw-bit conversion | Explicit `fixed_bits` / `fixed_from_bits` preserve all 32 bits without numeric conversion; bitwise operators themselves accept i32 only |

The asymmetric division overflow rule is intentional and follows the existing
i32 contract; there is no implicit saturation. Clamp is an explicit operation.
Do not use implementation-defined signed shifts, overflowing signed C arithmetic
or host floating point to define raw results. In particular, multiplying negative
values must not accidentally use floor rounding through a signed right shift.

Examples to pin down include maximum + one raw unit, minimum negation, tiny
negative products, `-1.5 -> i32 == -1`, halfway conversion ties and
`-3.5 mod 3 == 2.5`. Include division's minimum/negative-one overflow and both
signs of the modulo divisor. These choices are the core's own numeric contract;
frontends needing other rules must lower them explicitly.

Constant folding, scalar execution, bulk execution and the independent oracle
must agree bit-for-bit. Numeric tests compare raw values, not rendered RGB alone.

## Broader bulk kernels

### K1: stable resource reads

First admit checked indexed reads from resident numeric arrays, including fixed
arrays and packed colour data in integer arrays. A resource may have been updated
before the loop; it must remain unchanged throughout that loop's execution.

The verifier must identify all read/write resources by validated identity, reject
source/destination aliases and unsupported views, and retain generation/ownership
checks across yields. No array-sized snapshot copies or frame-time allocations
are introduced. Initial scope excludes package-backed image reads, output
feedback and dynamic resource selection.

Pattern and prepared Colors lookups are the first positive fixtures. Starlight
and a deliberately aliased read/write loop remain scalar. Immutable-source
gathers with different per-pixel indices are eligible; contiguity is not required.

### K2: lazy control flow and larger bodies

Admit an acyclic control-flow graph with validated forward branches and one
common final store to the current destination index. Every path must initialize
the value it stores; merge points require definite-assignment and liveness
analysis. Evaluate only the selected branch. A dead branch may contain an
invalid array index or math domain without faulting the active path.

Keep backward edges, nested loops, break/continue, arbitrary scatter, stateful RNG,
persistent scalar writes and source mutation on ordinary resumable bytecode.
Conditional skip-writes and multiple stores are also deferred. A compiler may
convert a source loop only when the one-store form is equivalent; it must not
turn a transparent/unwritten pixel into opaque black.

Target a maximum of 128 verified body instructions without increasing the
global register or package limits. This is an admission limit, not a larger
fixed stack array. Build a compact immutable execution plan during bounded
preparation, with only the required operands, native sites and branch metadata.
Charge its exact bytes to the VM heap and free it on cancellation/destruction.
Use bounded, audited per-worker scratch; never share mutable lane registers.

Reserve the plan/scratch requirements before activating the candidate. Do not
introduce a hidden low-memory execution mode: if a compiled kernel cannot be
admitted, reject the candidate and retain the active scene. A separately compiled
ordinary loop remains a valid program and the reference execution path.

### Work bounds, faults and parallelism

Choose pixels per chunk from the longest validated path and per-operation cost,
with the current maxima of 128 split, 64 unsplit transient and 32 persistent
destination pixels. Start with a conservative weighted-work ceiling equivalent
to the current 32-by-32 cheap-operation budget;
calibrate math weights on boards. A single expensive operation must itself have
a bounded implementation. Check time between pixels/chunks and measure overruns.

Transient output can execute on both cores when the executor is available.
Persistent destination images remain serial so faults retain writes in the same
logical order as ordinary bytecode. Publish only completed frames. Preserve
final-loop locals, zero/negative-trip behaviour and native continuations.

Do not speculate a load, division or math operation out of a conditional path.
Invariant evaluation must occur only if that expression would actually execute.
Do not move possible faults ahead of earlier observable writes. More complex
state promotion, reductions and generalized feedback need separate proof and
workload evidence.

## Native math scope

Put shared operation implementations behind both scalar and kernel dispatch.
Use a small declarative operation catalogue for type signatures, effects, feature
floors and kernel eligibility; generate or validate the C/Python tables from that
catalogue. Reuse frontend diagnostics rather than creating per-language runtimes.

| Delivery | Operations | Existing motivation |
| --- | --- | --- |
| Required basic math | f32 `abs`, `floor`, `ceil`, `trunc`, `cos`, `sqrt`, `log`, `exp`, `pow`; retain `sin` and existing min/max | Colour helpers, Flame, Candle, easing and indexing |
| Required fixed math | Exact conversions/comparisons/min/max and fixed `sqrt` | Fractional state, interpolation and deterministic numeric workloads |
| Compiled helpers | Clamp, interpolation, repeat/pingpong, polynomial and sine/circular/exponential easing | Existing motion/easing model; branches should no longer force scalar pixel loops |
| Stars optimisation proposal, subject to its own measurements | Pure indexed pseudorandom sampling; evaluate smooth procedural noise for spatial density and temporal brightness modulation | Existing Stars/Twinkle storage and birth scheduling provide a local workload; indexed sampling enables reconstruction, while smooth noise is a separate visual extension |
| Deferred until a local workload justifies them | Additional inverse trigonometry, geometric norms, arbitrary gradient-stop sampling and fixed transcendental approximations | Avoid expanding the first delivery into an unrelated function catalogue |

For fixed square root, use the integer square root of `raw * 65536`, rounded
down to one raw unit; negative inputs fault. For transcendental work in the first
delivery, a fixed program explicitly converts to f32 and converts back only when
needed. Do not promise that an all-fixed mathematical backend exists or is faster.

Specify pure f32 natives using finite binary32 inputs and results. Negative sqrt,
nonpositive log, zero to a negative power, negative bases with nonintegral
exponents, and nonfinite results fault. Define `pow(0, 0) = 1`. Underflow to a
finite zero is permitted; tests must cover subnormals and the selected target
math configuration. Frontends handle their documented rounding return types at
the source boundary. No process-global errno or floating environment is guest
state. All implementations must be reentrant on both rendering cores.

Scalar and bulk evaluation on the same build must be bit-identical. Across target
libm implementations, use per-function numeric error envelopes and exact domain
behaviour, not an unsupported universal bit-identity claim. S0 records the
reference-vector method and initial error targets; S4 must publish measured
maximum errors before its contract is accepted. An approximation cannot silently
replace a contracted operation merely to improve a benchmark.

Easing functions remain equations in the SDK. Do not add a native operation per
effect or curve. Ordinary `sin`/`cos` with normalized inputs can express periodic
waveforms; do not introduce a second timing unit or special global clock API.
Continue using captured elapsed time and the existing ramp driver for long-lived
phase generation. Never accumulate an unbounded clock in a Q16.16 register.

## Stars optimisation proposal: procedural reconstruction

**Status: proposed evaluation; no implementation or performance claim.** The
existing Stars effect and picolights Twinkle justify investigating procedural
randomness as a memory and scheduling optimisation. Stars retains sprite slots;
Twinkle explicitly retains arrays of birth times, positions and colours. Both
attempt at most one birth per frame using `probability * delta`. Reconstructing
events from a seed and elapsed time could remove all per-star records and make
the event schedule independent of frame cadence.

This is an intentional animation redesign, not a semantics-preserving compiler
rewrite of either existing effect. Keep the current Stars source as a benchmark
control while evaluating the proposal. Preserve the useful requirements: sparse
short-lived stars, configurable density and lifetime, fixed or varied colours,
fading, transparent composition and live brightness without restarting stars.
Specify overlap and population limits explicitly; do not silently inherit or
claim Twinkle's rejection-based position uniqueness or SpriteBatch slot limits.

### Reconstruct a bounded interval, not the complete past

Derive each candidate's properties from a seed, window ID, candidate ID and
property stream. Separate streams select its birth gap (`start_delta`), position,
colour and lifetime. Repeated evaluation must return the same values regardless
of frame rate, evaluation order, chunk boundaries or worker count.

An unrestricted recurrence `birth(k) = birth(k - 1) + start_delta(seed, k)` still
requires a prefix sum. A single global cursor containing an event ID and birth
time would avoid stored history, but catching up after a long pause could require
unbounded work. Prefer independently anchored time windows:

1. Choose a window duration `W`, maximum lifetime `L` and at most `C` candidates
   per window during preparation. Validate finite positive bounds and admit the
   maximum reconstruction cost before activating the animation.
2. Window `b` starts at `b * W`. Generate its candidates independently using
   `(seed, b, candidate, stream)`. Accumulate positive generated `start_delta`
   values from that anchor; stop at the window end or the candidate limit.
   Half-open windows give each birth one owner. No event depends on a preceding
   window's final event or RNG state.
3. For captured elapsed time `t`, reconstruct only windows intersecting
   `[max(0, t - L), t]`. There are at most `ceil(L / W) + 1` such windows, with
   at most `C * (ceil(L / W) + 1)` candidate evaluations. Draw only events whose
   generated lifetime includes `t`; candidates after `t` do not appear early.
4. Draw in a defined window/candidate order using bounded reusable scratch and
   shared artwork. Never materialize a growing event list or a SpriteBatch per
   window. Yield during reconstruction/drawing as required by the scheduler.

For example, `W = 0.5 s` and `L = 3 s` require at most seven windows regardless
of uptime or skipped frames. Persistent animation state is only the global seed,
time origin and settings; resumable loop cursors and temporary drawing state are
bounded scratch. Pixel buffers remain necessary. There are no persistent
per-star records or historical checkpoints.

Freeze generation settings for an animation instance so a later render can
reconstruct the same births; live brightness remains a separate output scaling
input. Define restart behaviour for generation-setting changes. Specify window
identity, counter wrap and time precision before implementation: the current f32
elapsed clock must not silently collapse window IDs at long uptimes. Use the
core's existing time units and a documented supported horizon or an independently
justified generic time representation, not an effect-specific clock primitive.

### Generic sampling and the role of smooth noise

The first candidate core primitive is pure indexed pseudorandom sampling, with a
provisional source spelling such as `random_at(seed, index, stream)`. Freeze word
widths, tuple encoding, output range, integer/range conversion and reproducibility
as test vectors. Choose and measure an integer mixing algorithm; do not assume a
stateful RNG call or an arbitrary hash has the required quality or cost. The
operation has no hidden seed mutation, allocation or dependence on invocation
order and must have bounded cost. Expose the same contract to every frontend.

Smooth procedural noise serves a different requirement: slowly varying star
density across space/time and gentle brightness variation within a star's fade
envelope. Evaluate that variation on the existing star artwork alongside the
indexed-random version. Correlated noise is not inherently more random, and is
not required to eliminate slots. If the visual comparison justifies it, propose
the smallest generic noise sampler with explicit domains, range, continuity,
determinism and cost; do not add an entire noise catalogue speculatively.

Birth scheduling, density mapping, fade envelopes and window reconstruction stay
in ordinary animation code. Pure sampling may become kernel-eligible after
validation, but event drawing can overlap pixels: stateless generation alone
does not permit parallel scatter or change compositing order. Keep the existing
stateful RNG contract and its ordering restrictions distinct from indexed
sampling.

### Evaluation and acceptance

- Specify expected birth rate, gap/lifetime distributions, spatial distribution,
  overlaps and maximum population. Window resets and candidate caps change the
  process; test boundary clustering, regular rhythms and truncated bursts rather
  than claiming an exact Poisson process or equivalence to the old frame test.
- Build an independent bounded reference event list for test intervals. Compare
  reconstructed event identities, times and pixels at the same timestamps under
  different frame cadences, skipped frames, long jumps, startup, window/expiry
  boundaries, settings restarts and counter/precision limits. Compare scalar/bulk
  sampling and different worker counts exactly under the chosen contract.
- Compare slot-based Stars with procedural Stars at matched artwork, population
  and lifetimes. For exact rendering comparisons, feed both renderers the same
  generated events. Separately evaluate the redesigned birth distribution and
  optional smooth-noise appearance; retain the unchanged scene as a control.
- Publish persistent state, peak scratch/preparation memory, render CPU cost,
  worst reconstruction slice and service latency on RP2040/RP2350. Demonstrate
  zero per-star persistent storage, no render allocations, and bounds independent
  of uptime; do not infer a CPU speedup from the memory saving. Benchmark sparse
  event drawing against any per-pixel alternative before choosing a traversal.

## Compiler and integration work

- Extend typing, literal encoding, conversions, array declarations, declaration
  expressions where applicable, constant folding, register allocation and
  diagnostics together. Audit every `type <= T_BOOL` assumption in the loader,
  math dispatch, map verifier and reuse proof. Do not accidentally allow fixed
  values into reference or input bindings.
- Preserve f32 expression order and disable contraction as today. Do not replace
  division with a reciprocal, reassociate fixed operations or assume their
  overflow/quantization can be ignored.
- Hoist stable pure work only with control-flow and fault-order proof. Never
  remove persistent state writes merely because they resemble temporaries.
  Broader unchanged-frame reuse is a separate optimization and remains
  conservative until independently verified.
- Report precise reasons for scalar execution: alias, source mutation, branch
  shape, unsupported operation, resource kind, instruction limit or loop-carried
  dependency. Report kernel plan/scratch estimates separately from pixel/state
  memory. These are diagnostics, not predictions of FPS.
- Keep the fixed type, math calls and supported kernels available through the
  existing Python, JavaScript and Blocks paths. Source defaults remain unchanged.
  Tests must execute emitted bytecode, not only inspect generated text.
- Document the bytecode type/operation signatures, resources, permissions,
  manifest requirements and reference fixtures for independent emitters. A small
  hand-authored bytecode fixture must pass admission and render without calling
  our Python compiler. Do not build another language frontend in this milestone.
- Use the existing `requires` application feature floor. Do not add a separately
  negotiated numeric, drawing or compiler ABI version. Additive instructions can
  retain the current container layout; an actual incompatible experimental
  encoding change requires coordinated recompilation and documented clean
  provisioning, with no old decoder kept solely for past experiments.

## Delivery sequence

S0–S5 are complete for the specified numeric, kernel and frontend scope.
[S1 evidence](numeric-s1-validation-2026-10-22.md),
[S2–S4 evidence](numeric-s234-validation-2026-10-22.md) and
[S5 evidence](numeric-s5-validation-2026-10-22.md) are separate from the Pico repair
archive. S6's full board/workload release matrix remains open; the recorded
RP2040 measurements do not establish RP2350 or ESP32 physical results.
Optional procedural sampling remains its own evaluation.

| Stage | Deliverable and likely files | Completion gate |
| --- | --- | --- |
| S0 | Freeze numeric/operation contracts, catalogue and local workload definitions; add `docs/numeric-types.md`, bytecode-emitter documentation and benchmark specifications | Every operation has types, units, rounding, faults and test vectors; required/optional scope is explicit |
| S1 | Fixed scalar values/arrays and i32 bitwise operations in `src/bytecode_core.*`, `src/portable.*`, a shared numeric module, `lightvm/compiler_core.py`, `lightvm/portable.py` and SDK declarations | Exact oracle/scalar agreement, valid admission, forged-type rejection and no render allocation; basic fixed programs execute |
| S2 | Stable array reads and fixed expressions in `src/portable_map.*`; preparation/lifetime accounting; compiler recognition and liveness | Pattern/Colors-style gathers enter the bulk path and exactly match scalar execution; aliases and resource mutation remain ordered |
| S3 | Lazy forward branches, compact admitted plans, larger bodies and weighted chunks; compiler CFG proof and source diagnostics | Branch/long-body fixtures use bulk execution; dead-arm faults remain dormant; memory and per-chunk bounds pass |
| S4 | Required native math, shared evaluator integration and generic helper coverage in the SDK/frontends | All required functions run through scalar and bulk paths with documented error/domain results; Flame/Candle math and colour equations compile |
| S5 | Full frontend parity, external-emitter fixture, feature floors, documentation and integration regression coverage | No separate language runtime; all current examples and new fixtures pass; newer instructions are rejected by insufficient targets before replacing live state |
| S6 | Matched hardware measurements, stack/RAM gates and final workload review | Correctness, performance, responsiveness and memory gates below have recorded evidence; unresolved physical results remain explicitly pending |

S1 follows S0. S2 follows S1; S3 follows S2. S4 can be implemented after S1 but
must be revalidated against the completed S3 kernel path. S5 joins S1-S4. S6 is
the release gate. Land a working scalar operation and its validation before
enabling that operation in a kernel.

Evaluate the Stars proposal alongside this sequence: S0 defines its sampling,
timeline and workload contracts; after S1, prototype reconstruction and assess
shared native sampling with S4; S6 records its memory/performance results and a
separate decision on smooth noise. It is a locally justified optimisation
proposal, not an automatic expansion of the required math set or a blocker for
the foundational numeric/kernel delivery.

The new numeric module must be included through `cmake/lightvm_core.cmake` so
host, Pico and ESP32 use the same implementation. Update fixture generators,
bytecode verification and stack-path fingerprints in the same changes as the
affected native call graphs. Do not modify the separate performance investigation
or claim its open hardware gates are closed by this work.

## Validation and performance gates

### Correctness

1. Use an independent wide-integer model for Q16.16 and bitwise operations:
   boundary values plus deterministic randomized raw inputs, conversion ties,
   negative rescaling, division faults, signed modulo and invalid shifts.
2. Replay every eligible kernel with optimization on/off, one/two workers and
   slice boundaries around 0, 1, 31, 32, 33, 127, 128 and nonmultiple output
   sizes. Compare raw numeric state, output bytes and fault/publication behaviour.
3. Test nested forward branches, missing definitions, bad branch targets,
   deliberately false effect claims, wrong resource types, invalid indices in
   taken/dead branches, empty loops and faults late in persistent-image writes.
4. Exercise every preparation yield and allocation failure with new plans;
   cancel/replace during preparation and rendering. Verify generations, cleanup,
   prior-frame retention and no worker access after the lifetime fence.
5. Use independent mathematical reference vectors for native functions. Check
   domains, endpoint identities, periodicity, monotonic regions and measured
   numeric errors. Matching two copies of the same implementation is not enough.
6. Run existing native, compiler, frontend, composition, transition and input
   regression suites. Run ASan/UBSan on host and the existing strict Pico RAM/
   whole-path stack gates. Repeat only checks justified by subsequent changes.

### Workload matrix

| Family | Cases and purpose |
| --- | --- |
| Existing controls | Current Color, Rainbow, Stars and Sweep sources; protect existing execution and output behaviour |
| Table gathers | Repeating/fitted Pattern and prepared Colors; vary table length and output count |
| Conditional math | Piecewise colour/easing, clipped bands and a pure 33-128 instruction body; exercise branch and size limits |
| Fractional arithmetic | Fixed ramps, interpolation, damping and wrapping; compare equivalent Q scalar/bulk programs exactly, compare f32 separately |
| Native math | Colour-wheel equations, Gaussian-transform arithmetic and damped candle calculations; isolate RNG from the pure math when benchmarking it |
| Ordered controls | Starlight-style feedback, stateful random updates, sparse Twinkle writes and aliased arrays; verify they are not wrongly parallelized |
| Procedural Stars proposal | Reconstruct bounded time windows without per-star records; compare indexed sampling and optional smooth-noise modulation against matched slot-based rendering and unchanged Stars |
| [Triggered lightning proposal](feature-proposal-control-api.md#triggered-lightning-effect-proposal) | Reconstruct a bounded flash sequence from one active burst seed/start time; reuse indexed sampling and envelopes. Inject recorded triggers for math/render tests; guest event delivery remains a separate dependency |

Use original LightVM fixtures expressing these operations; source references
establish motivation, not an obligation to preserve incidental bugs or allocation
patterns. For Gaussian tests provide inputs strictly inside the logarithm's
domain. For stochastic scenes use recorded random inputs or fixed seeds with
identical call order and timestamps; compare event schedules rather than relying
on an uncontrolled live population.

Measure host correctness and board performance separately. Use RP2040 Pico W
and RP2350 Pico 2 W at recorded clocks with 200, 500 and 1024 pixels where memory
admits them, one/two workers, and a representative mixed scene. Record firmware,
compiler, source and bytecode hashes, build flags, wiring, seeds and timestamps.
ESP32 builds and portable arithmetic tests are required; ESP32 throughput claims
need a separate board result.

Proposed performance acceptance targets, to be measured rather than asserted:

- New array/branch kernels reduce median render CPU time by at least 15% against
  ordinary scalar execution on RP2040 in at least two representative workloads.
  Publish every workload, including small loops where setup costs dominate.
- Existing CPU-bound workloads have no unexplained median render-cost regression
  above 5% in repeated matched runs. Use at least five alternating baseline/
  candidate windows, a fixed warmup and raw samples; report variability.
- Fixed-point has **no required universal speed advantage** over f32. Report its
  cost by operation and target; keep representation selection explicit.
- No render-time allocation, no heap growth across repeated frames, and no
  memory leak across candidate failure/cancellation. Publish exact plan/scratch
  bytes and peak old-scene/candidate/transition memory, not only steady-state heap.
- Meet existing stack reservations and service targets. Report p95/p99 control
  latency, largest native chunk, epoch and foreground gaps; investigate a
  regression above 5% or a missed 10 ms service target. Do not describe the 4 ms
  epoch admission target as a hard bound.
- Report distinct layer completions/displayed contributions and CPU render cost
  separately from wire-limited FPS. A single 200-pixel WS2812 output can already
  approach its output ceiling; unchanged FPS there does not establish no gain.

If a gate fails, retain the data and revise the responsible implementation or
compiler selection rule. Do not silently relax correctness, enlarge all heaps,
change numeric precision or omit the slow workload to obtain a passing result.
Physical acceptance is pending until measured; host results cannot close it.

## Boundary after the first delivery

The core is ready for another compiler when S0-S5 are complete and S6 has a
documented disposition for the intended deployment boards. An independent
emitter should then need only the operation contract, packaging rules and
conformance fixtures, not imports from compiler internals.

General function values/call stacks, dynamic references/allocation, vector live
inputs and event delivery, spatial coordinate resources, arbitrary gradient-stop
palettes, HDR output and broader render-reuse proofs remain separate proposals.
Procedural sampling/noise now has a specific local justification and evaluation
plan in the Stars proposal above; it remains subject to its own acceptance
decision. These additions are not prerequisites for delivering the numeric/kernel
foundation. Any later addition needs an explicit generic contract, a local
workload and its own resource/performance evidence.
