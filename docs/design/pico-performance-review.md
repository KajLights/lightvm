# Pico performance review and optimisation priorities

20 October 2026

The multicore work exposed a mismatch between output throughput and animation
progress. LightVM could present the latest available composition frequently while
making very little progress on its expensive layer. The most valuable next work
is to avoid unchanged rendering, improve scheduling of unfinished frames, and
reduce the interpreter and drawing work needed for sprites.

Both cores remain useful. The original measurements demonstrate gains for maps,
transformed images and balanced layers. The regressions show where the current
execution model needs stronger scheduling and resource contracts.

This review covers the investigation in **Check Pico core utilization**, the
implementation at `48b98d0`, and its corrections at `59b7c52`. Hardware figures
come from the recorded Pico W tests; the compiler experiments below were run
against a rebuilt host executable. The review adds no firmware changes.

## What happened

Two distinct failures followed the multicore deployment:

| Failure | Immediate cause | What the initial validation missed |
| --- | --- | --- |
| HA preset commands froze animation, USB and MQTT heartbeat | Large parser frames remained live through nested preset activation, exceeding the main stack budget | USB commands did not exercise the HA callback stack |
| Stars advanced at about 5 FPS while output presentations exceeded 100 FPS | Broad backpressure, hidden scene work, unequal native chunks and frequent scheduling overhead | The suite lacked a mixed fast-background and slow-sprite workload |

The [hardware report](../multicore-rp2040-validation.md) and its
[stars measurements](../benchmarks/pico-w-stars-regression-2026-10-20.json)
give this comparison for the unchanged saved preset:

| Firmware | Stars completions per second | Output presentations per second | USB status p95 |
| --- | ---: | ---: | ---: |
| Original single core | 22.91 | 22.99 | 33.07 ms |
| Regressed multicore | 5.39 | 103.20 | 13.27 ms |
| Corrected multicore | 26.48 | 99.68 | 10.03 ms |

The stars population is stochastic, so these are observed rates. The separate
fixed twelve-sprite test is the stronger remaining regression signal: **29.26
became 26.72 sprite completions per second**, an 8.7% reduction, while status
p95 improved from 17.35 to 9.78 ms. The correction restores usable performance
without establishing a universal speedup.

Removing the old refresh cap explains most of the simple rainbow refresh gain:
the new single-core configuration already reached 149.19 FPS, versus 149.50 with
two cores. Multicore reduced rendering cost and helped heavier scenes, but the
roughly 60-to-150 FPS rainbow result should not be attributed entirely to it.

## Why stars slowed down

### Backpressure reached beyond the buffer it protected

The first implementation collected no child rendering jobs when a transition
had a completed frame waiting for output. A prepared composition likewise
stopped all its layers. These rules protected buffers, but also blocked unfinished
work whose storage was independent of the waiting output.

The fast background could finish, trigger composition and presentation, and
repeatedly put the scene into those waiting states. Stars needed many slices to
finish one frame. The result favoured presentation of an old stars contribution
over completion of a new one.

The fix separates those lifetimes: children can progress while a transition waits,
and an already-started slow layer can finish while a prepared composition waits.
Committed contributions still remain pinned while a composition reads them.
This distinction belongs in the architecture contract: **backpressure should
follow ownership of a particular buffer**.

See [job collection](../../src/app.c), [composition](../../src/composition.c)
and [transition scheduling](../../src/transition.c).

### A completed fade kept generating invisible work

After recalling stars, the outgoing rainbow continued rendering in A despite
the fade being fully at B. The regressed run completed about 52 invisible rainbow
frames per second. Its completion could also dirty the transition.

This behaviour already existed in the original firmware; removing the cap and
allowing layers to progress at different rates amplified its cost. The correction
stops autonomous A rendering for a completed preset recall. Manual A/B takeover
resumes the existing instances.

The same idea cannot be applied indiscriminately to all invisible layers:
stateful callbacks, RNG and later transitions may depend on whether they continued
running. Automatic preset recall and manual mixing need explicit policies.

### Nominally equal slices did unequal work

The executor runs a pair of jobs and joins both before proceeding. Native drawing
originally selected a large, 128-pixel chunk based on whether the operation was
eligible for parallel execution. When both CPUs already held VM jobs, nested
pixel work correctly ran serially, but retained that large chunk.

One CPU could therefore finish its short stars slice and wait while the other
continued a much larger background operation. The 500 µs setting was a target
checked between operations, not a preemption guarantee.

The fix uses 32-pixel serial chunks when splitting is unavailable. That corrects
the mismatch, but the executor still pairs jobs in collection order and waits for
the slower member. Rotating layer order provides fairness of turns, not equal
CPU time or equal frame freshness.

See [executor](../../src/render_executor.c), [VM batching](../../src/vm.c),
[draw chunks](../../src/portable_draw.c) and [map chunks](../../src/portable_map.c).

### Uncapped scheduling made avoidable work more expensive

The background fills 200 pixels and then transforms their brightness. It has no
time dependence, yet the corrected run still rendered it about **105 times per
second**, at about **3.14 ms of accumulated execution time per frame**. That is
roughly 330 ms of measured execution per second spent regenerating an unchanged
background. It is not a cycle-accurate CPU utilisation measurement.

Packing the fill colour once and reusing colour-transform results for repeated
pixels helped substantially. A larger 2000 µs Pico W slice then amortised the
cost of repeatedly returning through scheduling, composition and service work.
Pico 2 W retains 500 µs. These are cooperative quanta, not refresh caps.

The incremental runs rose from 5.39 to 10.14 FPS after pipeline/hidden-scene
changes, to 14.56 after chunk changes, and to 23.45 with colour reuse and a
2000 µs slice. They establish that several costs contributed; stochastic
populations and sequential testing prevent treating each increment as an isolated
causal speedup.

## What the HA freeze says about the kernel

The recorded compiler frames were 6328 bytes for the HA message callback,
2592 for its enclosing tick, and 5400 for manifest parsing. Those three alone
consume 14,320 bytes, before the intervening calls and rendering serviced during
preparation. The main stack is 16 KiB.

Parsing now returns through a separate non-inlined helper before dispatch enters
activation. The callback frame fell to 608 bytes, and the previously freezing
command plus 62-command stress runs passed. The
[stack gate](../../tools/check_pico_ha_stack.py) rejects the old callback.

The parser code predates the multicore change, and the core-0 stack reservation
remained 16 KiB. The investigation strongly supports overlapping stack lifetimes
as the cause; it does not establish the exact earlier revision at which every
possible HA activation path first became unsafe. Calling this simply a
second-core race would misidentify the evidence.

There is a broader architectural opportunity. HA parses JSON, formats an internal
JSON command, and dispatch parses it again. Preparation can then call
`runtime_checkpoint`, which services rendering while the management call chain is
still live. Individual functions can look modest while their overlapping
lifetimes are expensive.

Introduce a compact typed command representation after transport validation.
USB, HTTP and MQTT can share the same internal operation without repeatedly
serialising and parsing it. Longer operations should advance as bounded state
machines from the coordinator, with validation and resource admission completed
before the live scene changes. This reduces stack nesting and makes preparation
time schedulable.

The present gate covers three HA functions, not the complete call graph. Extend
stack accounting to complete command paths and add a core-0 watermark alongside
the worker watermark. Include interrupt reserve and the render work reachable
from checkpoints. A larger stack alone would consume scarce RAM without removing
the lifetime problem.

## Optimisation priorities

### 1 Measure visible progress and where time goes

Expose separate rates for output presentations and each layer's completed
contributions. Also measure the age of the contribution actually being displayed:
a layer can complete a frame that has not yet reached the LEDs.

Instrument time spent executing, waiting for a paired job, waiting for a pinned
buffer, composing and packing, and servicing commands. Count native operation
types and chunk durations, and report the longest coordinator epoch. The current
`render_worker_us` counter counts dispatched worker execution; it does not prove
both cores are productively occupied.

Existing `render_total_us`, `native_draw_us`, `dispatch_count` and `slice_count`
are a useful starting point. Native time includes more than pixel arithmetic,
including sprite storage operations. Slice timings are elapsed execution
measurements, so they should not be presented as an exact CPU cycle breakdown.

**Acceptance:** a mixed scene can explain a slow layer even when output FPS is
high. Report p50/p95/p99 frame age and control latency, with counter sampling
overhead measured.

### 2 Reuse contributions when their dependencies have not changed

The solid background is the clearest first case. Retain its completed contribution
and rerender only when its colour, brightness, settings or relevant resource
generation changes.

The compiler can describe whether rendering reads time, delta, RNG, mutable state,
feedback pixels or live inputs. Start with the narrow case of a pure render
depending only on immutable resources and captured inputs. Timed input ramps
must still wake rendering when their effective value changes. Broad existing
effect permissions are not sufficient proof of purity.

Validate scheduling metadata against bytecode, or otherwise derive the proof
from verified operations. Skipping callbacks must preserve observable state;
a paused stateful animation is not equivalent to a cached pure background.

This can remove repeated VM, clearing, transforming and downstream dirty work.
Keeping the existing contribution needs little additional pixel storage.

**Acceptance:** an unchanged background has no repeated render invocations;
brightness and colour changes still appear promptly, including timed inputs.
Stars retains full autonomous progress and manual stepping remains deterministic.

### 3 Reduce sprite bookkeeping and offer retained artwork

The current compiler lowers callbacks and traversal to ordinary bytecode. For
each active sprite it clears a local canvas, obtains callback clocks, draws the
artwork, updates and reads style fields, constructs drawing style and composites
the canvas. This is flexible, but expensive for a three-pixel image.

Two host experiments quantify an immediately available improvement:

| Fixed twelve-sprite variant | VM dispatches per manual frame | Reduction |
| --- | ---: | ---: |
| Current benchmark source | 381 | — |
| Set fixed geometry at spawn | 321 | 15.7% |
| Set fixed geometry and constant colour style at spawn | 249 | 34.6% |

All ten tested frames were byte-identical for all three variants. Each artifact
remained 1681 bytes: the benefit comes from executing setup once, not shrinking
the package.

For the real stars source, moving only the fixed transform into `star_init`
reduced dispatches from **97,410 to 86,985**, or **10.7%**, across 300 frames.
The comparison used seed 7, 16 ms steps, the saved preset settings and brightness
changes at frames 100 and 200. Every frame matched byte-for-byte. Fading alpha
must remain dynamic.

The experiments and results are in
[the compiler inspection record](../benchmarks/pico-compiler-review-2026-10-20.json).
They establish eliminated interpreter work, not Pico FPS gains. The local
reproduction scripts are in `build/performance-review/`.

Next compiler work should eliminate unused callback clock results where legal,
remove redundant default writes, forward style values without repeated field
round trips, and hoist immutable values. `batch_delta` updates state, so removal
requires proving that its history cannot be observed.

A retained image sprite API could express “draw this shared artwork with this
transform and colour” without rebuilding a scratch canvas each frame. Keep the
canvas path for callbacks that draw multiple primitives or rely on group opacity.
Any automatic fusion must preserve the existing premultiplication, intermediate
rounding, overlap order and fault semantics.

**Acceptance:** run pixel and state comparisons for overlapping, clipped,
fractional, fading and removed sprites, then measure dispatch counts and Pico
layer throughput at fixed populations. The existing kernel already clips to the
transformed footprint; adding clipping again is not the missing optimisation.

### 4 Schedule by unfinished work and bound the whole epoch

Retain core 0's ownership of commands and mutation. First add timing and priority
for old unfinished frames, with fairness for other layers and outputs. Choose
native chunk sizes using measured operation cost and available parallelism:
32 transformed pixels and 32 fill pixels need not cost the same.

The whole coordinator epoch needs a service deadline. A 2 ms limit per VM slice
does not bound the interval before I/O is serviced when many layers are eligible.
The current epoch can visit every eligible VM, and checkpoints are deferred
until it ends.

If measurements show significant pair waiting, replace fixed pairs with a small
bounded batch whose ready jobs can be claimed by either core. Prepare stable
inputs on core 0, maintain exclusive VM ownership, and join all work before
commands, storage or destruction. Do not add worker-side recursive dispatch or
unbounded queues.

**Acceptance:** recover the fixed-sprite deficit without worsening agreed control
latency, while retaining gains for maps and images. Demonstrate improvements in
pair-wait time and visible contribution age, not just aggregate job counts.

### 5 Specialise common drawing and presentation paths

The next candidates are:

- Fuse a uniform fill followed by a uniform colour transform into one packed
  fill when intermediate pixels are unobservable. Compute the same rounded
  transformed colour once.
- Add exact integer-translation, unit-scale image paths that avoid general
  interpolation and division. Preserve fractional and wrapped sampling where
  required; validate negative scale and boundary behaviour.
- Use span copies at completed transition endpoints. The current transition
  still traverses pixels and copies B into its own output buffer. A complete
  presenter bypass is possible later but needs an explicit ownership handoff.
- Consider caching static lower-layer composites and tracking changed spans after
  contribution reuse is established. Moving or disappearing sprites require
  redrawing their old footprint too.

Do not assume dirty spans reduce WS2812 transmission time: the full chain still
needs transmission. Their benefit is reduced rendering/composition. Full buffer
copies are also relatively small at 200 pixels; measure before adding more
buffers or complex ownership.

**Acceptance:** exact reference pixels across formats, alpha and rounding cases;
lower native time on the relevant Pico workloads; unchanged buffer-lifetime
contracts.

### 6 Make scene preparation an admitted asynchronous operation

The recorded heavy replacement took **11.072 seconds on one core and 6.966 on
two** while the old scene continued rendering. Increasing the benchmark timeout
documented that limitation but did not improve it.

Split loading, verification, resource admission, initialisation and initial
rendering into bounded coordinator steps. Give preparation a fair share of CPU
and return an operation identity so the editor can display progress and completion.
Keep the old scene until the candidate is ready; define replacement, cancellation
and failure behaviour explicitly.

Cache verified immutable program metadata when repeated activation makes it
worthwhile, keyed by program generation or content hash. Account for the overlap
between old and candidate scenes. The full Pico W profile has only 2148 bytes
left after reservations, although its 16 KiB native operating reserve is already
included in that calculation; it is not a report of actual free heap.

**Acceptance:** bounded command service gaps during the worst replacement,
atomic activation, correct cancellation and measured peak RAM. Include repeated
HA switches while a previous preparation is pending.

### 7 Explain performance in the compiler and editor

Python, JavaScript and Blocks converge on the shared Python compiler, so common
optimisations can benefit all three. Their source APIs are not identical today;
sprite authoring is presently a Python feature.

Add an advisory compile report separate from errors, covering:

- Which pixel loops became native maps, and why other loops remained scalar.
- Time/input/state dependencies and eligibility for contribution reuse.
- Per-frame versus per-spawn work, with suggestions for immutable sprite style.
- Resource memory at the selected pixel count and sprite capacity, including
  composition and preparation overhead where known.

Map explanations back to JavaScript source and Blocks through the existing
location maps. Runtime feedback should show “stars updates per second” and
“output presentations per second” separately. “All code checks passed” currently
says nothing about execution cost.

Broaden pure-map recognition incrementally: useful next cases include ranges and
safe conditional expressions. Preserve lazy evaluation, fault order and floating
point behaviour; computing both sides of a branch can introduce faults. Avoid
presenting a compile-time FPS prediction as a measurement.

For Blocks, prefer explicit controls such as artwork, position, opacity, lifetime
and “when created” versus “each frame”. Emit the same general rendering operations,
with static and dynamic work separated. This exposes efficient structure without
requiring users to understand CPU cores or VM slices.

### 8 Spend board memory on measured hot paths

Readonly artwork currently stays in the program package:
[`pbc_prepare`](../../src/portable.c) does not allocate its pixel payload, and
[`pbc_image_read`](../../src/portable_draw.c) reads samples through the package
reader. This saves memory, but the stars artwork is only twelve bytes and is
sampled repeatedly. Selective residency for small, frequently sampled immutable
images could avoid that repeated access. Charge the cache explicitly and keep
large images package-backed when that is the better tradeoff.

The VM instruction cache is 256 bytes on Pico W and 1024 on Pico 2 W. Measure
misses and hot-loop sizes before increasing every instance's allocation.
`pbc_map_run` also decodes its body and rebuilds dependency metadata on each
chunk. A small verified execution plan prepared at admission could amortise that
work, with frame-dependent captured values refreshed separately.

For Pico 2 W, keep the measured DCP benefit and benchmark remaining arithmetic
and conversion costs separately. For Pico W, test selective placement of genuinely
hot native helpers in RAM only after identifying them. The two boards should share
semantics while choosing different cache and chunk budgets. Gains from changing
those budgets remain hypotheses until matched hardware runs confirm them.

## Validation that would have caught this sooner

The existing tests provided real evidence for deterministic frames, flash
coordination, concurrency and balanced throughput. Their gap was representativeness:
uniform maps, images and scalar layers did not cover the saved mixed scene, and
USB control did not cover HA's stack.

Use a small permanent matrix covering:

| Dimension | Required cases |
| --- | --- |
| Workload | Pure map; general image; fixed sprites at several populations; static background plus slow sprites; asymmetric CPU layers |
| Scene lifecycle | Direct activation; active fade; completed recall; interrupted recall; manual A/B takeover; replacement while busy |
| Control path | USB plus authenticated MQTT/HA, with a defined idle-network baseline and a defined traffic load |
| Platform | Pico W and Pico 2 W, with matched one/two-worker builds and explicit slice/cache settings |
| Correctness | Pixel hashes, state/RNG evolution, contribution age, control latency, stack/heap peaks and reset stability |

Give each workload a fresh runtime: applying identical hardware configuration
preserves scenes, which previously carried A/B state into a later benchmark.
Record every participating program hash and settings, not only the foreground
program. Aggregate render medians across dissimilar layers can hide the expensive
one; retain separate per-layer measurements.

The fixed static sprite workload is valuable for repeatability, but should be
paired with a seeded animated workload so future static caching cannot conceal
dynamic sprite costs. Repeat runs in varied order and report spread. The original
and corrected image measurements were made with uncontrolled network traffic,
and the follow-up correction was built but not hardware-retested on Pico 2 W.

## Recommended sequence

Start with contribution-age and wait-time instrumentation, a reproducible mixed
workload, and the sprite setup improvements demonstrated here. Then implement
dependency-based background reuse and exact fill/transform fast paths. These
address concrete unnecessary work before making the scheduler more complex.

Use the new measurements to choose cost-aware chunks and, if justified, a bounded
shared job batch. Develop typed internal commands, preparation state machines
and complete stack-path accounting alongside that work. Surface the compiler
explanations and layer rates in the editor as their underlying data becomes
available.

The objective remains the highest useful sustainable refresh rate: fresh visible
animation contributions, responsive controls and safe memory use. A fixed 60 FPS
target would hide some of these costs without resolving them.
