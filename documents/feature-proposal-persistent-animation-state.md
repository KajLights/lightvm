# Persistent animation state, initialization and timing API

Status: **Accepted and implemented for the six supplied examples**

Date: 15 October 2026

Scope: the six supplied examples — Color, Pattern, Chase, Wipe, Rainbow and
Twinkle — and the compiler/runtime/editor support necessary to port their
original state and render algorithms. This proposal does not expand that set.

This specifies concrete bindings for the existing
[portable animation architecture](architecture-decision-portable-bytecode.md),
especially native retained buffers, admitted arrays, init, logical ticks and
render purity. It follows the [coverage review](review-picolights-animation-coverage.md).
The code blocks specify the agreed source contract. The [implementation notes](../docs/animation-profile.md) record the binary bindings, limits, validation and measured memory costs.

## VM and profile boundary

These lifecycle restrictions belong to `animation/1`, not to every VM component.
The portable function table and typed native-call boundary must allow later
components to register independent callbacks, such as socket and HTTP handlers.
Animation completion freezes that animation contribution; it does not terminate
the containing component or imply that unrelated handlers have finished.

Clocks, owned RNG streams and animation lifecycle operations use a shared typed
native-service binding mechanism. Efficient checked indexed loads/stores remain
VM operations. Native signatures declare argument/result types and effects, and
the animation profile verifies permitted effects at each entry. Resources remain
statically admitted for this profile; this is not a universal ban on future
runtime resource registration. Networking, dynamic allocation and a general
service framework are outside this increment.

Memory claims must include actual instance contexts, registers, call storage,
metadata, native allocations and replacement/in-flight peaks. Register execution
alone establishes no memory advantage over WASM.

## Accepted decisions

1. `array(...)` and `pixel_buffer(...)` declare persistent, instance-owned native
   storage. Its contents survive updates and renders. Its size is fixed at
   preparation; ordinary code cannot allocate or resize it.
2. `@init` runs once per fresh instance/reset. `@step` mutates simulation state;
   `@prepare`, `@shade` and `@kernel` read stable simulation state. The output
   `pixels` view remains a separate frame resource.
3. `setting(...)` is an installation value that can determine capacities or
   initialization. Existing `parameter(...)` remains a live scalar control.
4. A new explicit source profile uses fixed logical updates, initially 20 ms
   (50 Hz) for the examples, with an initial update at time zero. The interval
   is configurable; the historical hardware loop rate is not assumed to be 50 Hz.
5. Catch up at most four ordinary ticks per scheduling pass, then discard excess
   wall-time debt without skipping logical state transitions. Simulation slows
   during overload; counters make that visible.
6. `cycle_complete()` reports progress without stopping. `complete()` requests
   a final successful render/commit, then freezes time and holds that image.
7. Twinkle uses explicit active/occupied flags and bounded selection of a free
   position. Keep spawn-before-expiry and its original probability/fade equations.
8. Advertise a new program profile/format while retaining the existing LBC1/2
   execution path. None of these semantics silently changes old programs.

## Source declarations

```python
from lightvm import (
    profile, setting, parameter, array, pixel_buffer,
    bool8, u8, u16, i32, f32, rgb8, stamp,
    rgb, rng, init, step, prepare, shade, kernel,
    now, elapsed, cycle_complete, complete,
)

profile("animation/1")

# Evaluated when preparing an instance, after its logical pixel count is bound.
max_stars = setting(max(1, count // 10), minimum=1, maximum=count)
update_us = setting(20_000, minimum=1_000, maximum=1_000_000)
seed = setting(1, minimum=0, maximum=2_147_483_647)

# Existing live parameter semantics: coherent values at execution boundaries.
color = parameter(rgb(255, 64, 0))

paint = pixel_buffer(length=count, fill=rgb(0, 0, 0))
star_birth_time = array(stamp, length=max_stars)
star_positions = array(u16, length=max_stars, fill=65535)
star_colors = array(u8, length=3 * max_stars, fill=0)
star_active = array(bool8, length=max_stars, fill=False)
occupied = array(bool8, length=count, fill=False)
random = rng(seed=seed, stream=0)

draw_count = 0
```

This block illustrates declarations together; a real effect declares only the
resources it uses. Twinkle does not need `paint`; Chase does not need star arrays.

### Declaration contract

| Form | Meaning |
|---|---|
| `profile("animation/1")` | Explicit compiler-recognized source/semantic profile. Required to opt into this API. |
| `setting(default, minimum=..., maximum=...)` | Typed, immutable instance setting; default type determines its scalar type. Bounds apply to numeric settings. |
| `parameter(default, ...)` | Existing live scalar parameter form; resource capacities cannot depend on it. |
| `array(element_type, length=..., fill=...)` | Persistent fixed-length array. Without `fill`, initialize to the type's zero/false/black/zero-time value. |
| `pixel_buffer(length=..., fill=...)` | A pixel-oriented spelling of an RGB8 array, backed by packed native RGB storage. Same ownership and access checks. |
| `rng(seed=..., stream=...)` | Declares an owned RNG resource; no random draw happens during compilation. Seed/stream are fixed instance settings or literals. |

These are declarations, not executed Python constructors. Setting defaults may
be bool, i32, f32 or literal RGB colours, using the existing parameter encodings.
Lengths, numeric defaults, bounds and periods may use literals, `count`, earlier
settings, checked arithmetic and `min`/`max` from a documented declarative
expression subset. Lengths and periods must resolve to integers.
They may not call user functions, inspect live parameters, read clocks, sample
randomness or access devices. Cycles, invalid bounds, overflow and resource-limit
violations reject preparation before taking ownership of output.

Keep those expressions in the portable package: the same package can resolve
`count` and `max_stars` differently on two admitted installations. Validate
override values and expressions on the controller as well as in the compiler.
Use checked arithmetic for resource sizes; do not apply wrapping i32 arithmetic
to byte counts or allocation calculations.

Initial element formats:

| Type token | Stored element | Load/store semantics |
|---|---|---|
| `bool8` | 1 byte | Python bool values only; stored encoding 0 or 1 |
| `u8`, `u16` | 1 or 2 bytes | Load as i32; stores outside 0..255 / 0..65535 fault |
| `i32` | 4 bytes | Existing signed i32 value semantics |
| `f32` | 4 bytes | Existing binary32 arithmetic/conversion semantics |
| `rgb8` | 3 bytes | Existing typed RGB colour; integer bit patterns cannot forge a resource |
| `stamp` | 8 bytes | Opaque logical-clock microsecond timestamp, described below |

`stamp` avoids losing short star lifetimes when a long-running f32 absolute time
loses precision. It is a deliberate widening of Twinkle's birth-time storage,
not a change from birth times to a different animation algorithm. The implementation uses an eight-byte typed register value. This does not expose
general integer-to-pointer or integer-to-stamp casts.

General arrays may have length zero; operations on the empty range are no-ops
and element access faults. An assigned output still requires `count >= 1`.
Board limits bound each length and total memory. In this first RGB profile,
`count <= 65535`, making the original u16 position sentinel 65535 unambiguous.
Twinkle further requires `1 <= max_stars <= count`.

Settings are resolved at preparation and remain unchanged through pause, stop
and same-settings reset. Changing one explicitly prepares a fresh instance with
a new generation; it never reallocates a running effect's array. The
management API accepts an optional `settings` object on a capability-advertised
`procedure_activate` request, with validated defaults when omitted. The editor
exposes preparation settings through `program_reset`. Runtime overrides are not
saved in startup bindings in this increment; edit the source defaults and save
the recompiled procedure to make them durable.

Existing `parameter` updates remain atomic. Init may read their initial snapshot,
but later changes do not rerun init or automatically recompute derived state.
All six examples use settings for original constructor-only controls. Automatic warnings about
init-only live parameters in arbitrary custom programs are deferred.

## Array and buffer operations

```python
value = paint[i]
paint[i] = color
size = len(paint)
paint.fill(color)
paint.fill(color, start=left, stop=right)
pixels.copy_from(paint)
```

- Indexes are i32. Require `0 <= index < len(resource)`; invalid access faults.
  Negative indexes are not silently wrapped. Source adapters must normalize any
  intended Python negative indexing explicitly.
- `fill(value, start=0, stop=len(resource))` uses an exclusive stop. Require
  `0 <= start <= stop <= len(resource)`; there is no implicit clipping. An empty
  range is valid. Element type/conversion rules match indexed stores.
- `copy_from(source)` initially copies the whole array. Types and lengths must
  match; exact self-copy is a no-op. Partial copies, resizable slices and general
  overlap rules are outside this increment. Wipe slices can lower to range fill.
- Reads observe earlier writes within the same permitted invocation, in program
  order. Multiple writes to an element use last-write-wins. Other instances cannot
  access the allocation without a separately declared binding.
- Resource names cannot be rebound, resized, appended to, deleted or converted
  to numbers. Helper parameters can accept statically typed resource references;
  permissions remain those of the calling stage and original resource.
- Fill/copy may suspend in bounded native chunks. Suspension retains progress,
  ownership and the invocation's input snapshot; it is not a commit or completion.

The initial implementation can retain the existing working/committed output
buffers and copy persistent paint into working output. Later allocation sharing
or rotation is permitted if consumer lifetimes, failure visibility and write
permissions stay identical. A full copy is an implementation choice, not a new
architectural requirement.

Each render's output `pixels` starts black, as in the current profile. Reading
it observes this render's writes, never the previous presented frame. Persistence
is explicit in resources such as `paint`; `pixels.copy_from(paint)` publishes
that retained image through the ordinary successful-frame boundary.

## Initialization and stage permissions

```python
@init
def setup():
    global draw_count
    draw_count = 0
    # Declared arrays have already received their initial values.
    # Count/settings are now known; owned RNG draws are allowed here.

@step(period_us=update_us, first="immediate", max_catch_up=4, overload="slow")
def update():
    global draw_count
    draw_count += 1
    cur_pix = (draw_count - 1) % count
    paint[cur_pix] = color
    if draw_count % count == 0:
        cycle_complete()

@kernel
def draw():
    pixels.copy_from(paint)
```

This is an accumulating Chase-shaped example. Its cycle notification does not
stop it. A one-shot variant would explicitly call `complete()` at that boundary;
that policy must not be silently added to the original repeating algorithm.

| Stage | Persistent scalars/arrays | Frame-local scalars | Output `pixels` | RNG/completion |
|---|---|---|---|---|
| Module declarations | Initial-value templates only | Initial-value templates only | No access | Declare RNG only |
| `@init` | Read/write | No frame-local writes | No access | RNG permitted; completion calls forbidden |
| `@step` | Read/write | No frame-local writes | No access | RNG and cycle/complete permitted |
| `@prepare` | Read only | Read/write | No access | No owned RNG or completion |
| `@shade` | Read only | Read only | No reads/writes; return one colour | Pure/stateless helpers only |
| `@kernel` | Read only | Read only | Read/write current working frame | Pure/stateless helpers only |

Locals are writable within their invocation. Plain module scalar declarations
remain supported: writes in init/step identify persistent state, while writes
in prepare identify frame-local state; conflicting roles are compile errors.
`global` remains required for scalar reassignment. Indexed resource writes do
not rebind the resource and need no `global`. Array resources in this increment
are persistent; admitted frame-scratch arrays can be a separate later extension.

Each package has at most one init, step and prepare entry, plus exactly one
shade or kernel. Init/step/prepare are optional. Bounded helper functions and
loops are supported in every appropriate stage; their effects are checked
transitively. Every shader/kernel invocation sees a stable simulation/parameter
snapshot. The scheduler defers that instance's next mutation until its render
releases the snapshot, while other instances/services can progress.

Preparation order is fixed:

1. Resolve count/settings and validate all resource, clock and operation needs.
2. Reserve peak storage, including old/new coexistence where replacing an instance.
3. Apply scalar/array templates, reset RNG streams, counters and clocks to zero.
4. Run init once, under the advertised preparation budget.
5. For an immediate step, run logical tick zero once, at `time=0`, `delta=0`.
6. Run prepare/render into candidate output. Only after success activate/commit it.

The same sequence runs for an explicit fresh reset; reset never keeps old array
contents accidentally. In this profile a reset request must either
prepare a replacement successfully or leave the old valid instance/output in
place. Reject when coexistence cannot fit; do not free the live instance first.
Init never runs on ordinary render, parameter update, pause/resume or repeated
PLAY of an initialized instance. Preparation emits no external cycle/completion
notification until activation succeeds.

Fresh resources use their declared values, black by default for pixels. They do
not inherit another program's previous physical output. Preserving an arbitrary
previous image across program changes needs an explicit image-handoff binding;
it is not implied by persistent storage within one instance.

## Timing and overload semantics

The decorator selects a logical update interval in integer microseconds. Its
period is a literal or setting. The new profile requires an explicit period
when step exists; there is no hidden dependence on presentation FPS. Supported
values of `first` are `"immediate"` and `"after_period"`. The latter skips tick
zero and first updates after one full interval, with `delta=period`.

The example recommendation is a configurable 20,000 µs period and immediate
first update. This is an explicit compatibility choice: PicoAnimation advances
per draw, and the historical scheduler/hardware rate is not a portable constant.
Replay must feed the originals the same logical draw times as the port.

| Input/helper | Defined meaning |
|---|---|
| `time` | f32 seconds at the current logical simulation timestamp; convenience view of an integer clock |
| `delta` | In step: zero for immediate tick zero, then exactly the declared period in seconds. In init/prepare/shade/kernel: zero |
| `now()` | An opaque `stamp` for that same logical timestamp; constant throughout the invocation/snapshot |
| `elapsed(birth)` | Subtract the stamp from the current integer logical time, then convert the difference to f32 seconds |

Stamps are from this instance's logical clock. Zero is a valid time, not an
inactive sentinel. They support storage/copy/equality and `elapsed`; arbitrary
arithmetic and numeric casts are excluded. A future/incompatible stamp faults.
Resource reset removes old stamps. Wide clocks/counters are native and checked
for overflow; ordinary numeric variables retain declared i32/f32 semantics.

At a render, time is the last committed simulation tick's time. Re-rendering
without an update therefore gives the same clock, state and random history.
This first profile has no presentation-time interpolation. A continuous effect
can update at a suitably admitted rate; interpolation can be added as an explicit
later capability. For a package without step, its logical program clock follows
pausable elapsed time because there is no discrete simulation state to advance.

### Normal playback and overload

Accumulate unpaused wall-time credit. Execute a due logical tick for each whole
period, up to `max_catch_up` ticks per scheduling pass; preserve fractional
credit. If more whole periods remain, discard that excess wall-time debt and
increment overload counters. **Do not advance logical time, RNG or draw count
for discarded wall intervals.** Each executed tick still gets one normal delta.
Report logical time, active wall time and discarded intervals separately so the
wall/simulation offset remains observable.

For the 20 ms/4-tick policy, after logical time 40 ms, a scheduling
pass with 160 ms credit executes four updates to logical time 120 ms and drops
80 ms of wall debt. The simulation has slowed by 80 ms; it has not skipped four
state transitions or taken a 160 ms integration step. The remaining clock offset
is observable, not secretly added back on the next frame.

Autonomous playback yields to the native event loop after at most 4096 VM
dispatches; each fill/copy dispatch handles at most 32 elements. Other components
and management can run between slices. Preparation and manual FRAME are bounded
synchronous management operations in this increment (two million aggregate VM
dispatches and a 500 ms deadline), so their completion does not require a new
asynchronous management protocol.
The catch-up limit complements per-invocation and aggregate CPU budgets; it does
not authorize four unbounded native calls. Fault/work-limit behaviour is distinct
from overload and ordinary suspension. Synchronized installations that cannot
tolerate independent slowdown need a separately negotiated shared overload policy;
that is outside these six local examples.

### Manual FRAME, pause, stop and reset

- For the new profile, `FRAME(delta_ms)` advances a **logical test timeline**,
  processing every due tick within that requested interval, then renders once.
  Carry sub-period remainder across calls. It does not apply real-time wall-debt
  dropping. Reject before execution if the number of due ticks exceeds the
  advertised `max_manual_ticks` (64), including on a fresh, uninitialized instance. Runtime faults can still occur while executing admitted work.
- A fresh FRAME first performs initialization and immediate tick zero exactly
  once. Subsequent `FRAME(0)` only renders. Under a 20 ms period, `FRAME(1000)`
  from an initialized instance executes 50 updates; fifty `FRAME(20)` calls
  reach the same state. A complete event can terminate advancement early; report
  actual logical time and completion instead of claiming the remainder ran.
- FRAME is an intentional debug advance while playback is paused. Starting PLAY
  afterward resets the wall-time anchor; time spent paused/manual testing creates
  no automatic catch-up. Preserve the sub-period remainder across that handoff.
- Pause freezes logical time, wall-credit accumulation and ordinary updates;
  retain state/RNG/last image. A partially executed update finishes at its bounded
  boundary; admitted catch-up ticks that have not started are cancelled and counted
  separately as `cancelled_wall_ticks`. Preserve only fractional credit so FRAME(0)
  remains render-only. Resume uses a fresh wall anchor. Parameter changes
  remain queued for the next permitted execution boundary.
- STOP freezes the instance and clears physical output, retaining its stable
  state. Apply it at a logical-update boundary; an in-flight step must reach that
  bounded boundary or fault, and its pending output is not presented after stop.
  A render can be discarded safely because it cannot mutate simulation state.
  PLAY republishes retained state and continues; completed instances remain held.
- A fresh animation reset/re-activation reruns templates/init and tick-zero policy. It is
  the operation that restarts a completed run or changes installation settings.
  Use `program_reset` with the expected generation for this operation; preserve
  the existing transport `RESET` command's unload semantics.

These definitions apply only to the opt-in profile. Existing LBC1/2 FRAME and
PLAY semantics remain unchanged, including their present initialization behaviour.

## Cycles and completion

Source operations:

```python
cycle_complete()          # Equivalent to cycle_complete(count=1).
cycle_complete(count=3)   # One bounded notification representing three cycles.
complete()               # Request a terminal final frame; does not return early.
return                   # Explicitly end this update when that is intended.
```

`cycle_complete(count)` requires a positive i32 count. On successful step return,
increment the native cycle counter. Status exposes the current successful logical
timestamp; it does not retain a separate timestamp for every cycle.
It does not reset state, rewind time or stop playback. Multiple calls in one
step combine into one bounded record with checked count arithmetic; no unbounded
event list is allocated. Observation is by generation/counter, so polling cannot
consume an event intended for a future sequence manager.

`complete()` is idempotent within a run and never implicitly adds a cycle. It
latches a completion request; remaining statements in the current step execute.
After successful return, run the final prepare/render at that logical timestamp
before any next step. Commit that contribution and, for the attached output,
require successful native presentation before reporting completion. Then freeze
the program clock and hold the final frame. An offscreen instance completes after
its contribution commit; it does not need to become physically visible.

Presentation success means the native driver's defined consumption/completion
boundary, not measured light output. Retain the previous committed contribution
until terminal publication succeeds, charging that lifetime in admission. An
asynchronous consumer must acknowledge completion and retain its buffer reference;
the guest cannot declare it finished merely by requesting publication.

If final rendering/presentation faults, report fault instead of completion and
retain the previous successfully published image according to the output contract.
Cycle notifications from a completing step become visible with terminal success,
not before a failed final render. A complete request followed by a step fault
publishes neither that step's cycles nor its completion.

`program_snapshot`/status for the new profile adds `completed`, `cycle_count`,
`completion_time_us`, logical tick/time counters and overload counters. No guest
callback runs synchronously from the output driver. Future sequencing consumes
the completed boundary; it must not re-enter guest code from a completion call.

A completed instance holds its committed image without further updates/renders.
PLAY/RESUME does not restart it. A live parameter update against a completed run
returns `requires_reset`; a reset can prepare new settings/initial parameters.
Completion is distinct from management pause and STOP; STOP may still clear the
physical output, and a later PLAY may display the held final contribution again.

Original mapping must be recorded per example. In particular, the custom
PicoAnimation base does not itself translate every assignment to
`self.cycle_complete` into a callback. Adding reliable cycle notifications is a
deliberate, accepted adapter correction; it must not silently change a repeating
Chase into a one-shot effect. For Wipe, preserve its original end condition and
draw-count ordering, then explicitly choose terminal completion at that boundary.

## Bounded Twinkle fix

Preserve these original meanings and order:

1. One spawn trial per logical update: `random_value < probability * delta`.
   Values of the product above one mean a successful trial, not multiple spawns.
   Do not replace this with a Poisson formula or per-pixel procedural phases.
2. Find the first inactive star slot. Spawn before expiring old stars, matching
   the original call order. A star that expires later in this update still
   occupies its slot/pixel during this update's spawn attempt.
3. Store a birth time, position and three channel values for the new star.
4. Expire when `elapsed(birth) > duration`, retaining the original strict `>`.
5. Render background, then active stars using the original sine-out fade and
   channel-wise maximum. For each channel, background is
   `int(base_channel * background_brightness)`, computed during init. An active
   star's fade is `1 - sin((pi / 2) * elapsed(birth) / duration)`, and its output
   is `max(background_channel, star_channel * fade)`. Background brightness does
   not additionally scale the star. Preserve the original output conversion.

Move expiration out of the rendering callback into step so repeated rendering
cannot change simulation. Use `star_active` instead of `birth != 0`: a star can
be born at logical time zero. Maintain `occupied[p]` for active positions and
`active_count` as an ordinary persistent scalar. Birth marks both flags; expiry
clears both and decrements the count. Dead slots may retain their old position/
colour values, but those stale values no longer reserve pixels.

### Random helper contract

`random.uniform()` consumes one 32-bit word and returns its upper 24 bits times
`2**-24`, exactly representable as binary32 in `[0,1)`. This avoids rounding a
full-width sample up to 1. The versioned native generator and seed/stream-to-state
mapping are fixed by RNG v1 below and exercised by native contract tests. The source API does not expose a
host Python RNG or promise CircuitPython's exact seeded sequence. Render stages
cannot advance this stream.

`ok, rank = random.try_index(n)` is a bounded integer-selection helper for
`1 <= n <= 65535`. Using a generator output word `x` in `0..2**32-1`, compute
`limit = 2**32 - (2**32 % n)` with native wide arithmetic. Try at most four
words; accept the first `x < limit` and return `(True, x % n)`. If all four are
rejected, return `(False, 0)` and increment a diagnostic counter. Invalid `n`
faults instead of masquerading as random exhaustion. This avoids modulo mapping
bias for uniformly distributed input words without an unbounded retry loop.
The tuple is statically scalarized; no runtime tuple allocation is needed.

RNG v1 uses one u32 state: `x ^= x << 13; x ^= x >> 17; x ^= x << 5`, with
u32 truncation. Initial state is `seed ^ (stream * 0x9e3779b9)` modulo 2**32;
replace zero with `0x6d2b79f5`. Seed and stream are nonnegative i32 values.
For seed=1, stream=0 the first five words are 270369, 67634689, 2647435461,
307599695 and 2398689233. This fixes replay across the native targets; it does
not promise the original CircuitPython random sequence. The four-attempt limit
and no-spawn-on-exhaustion behaviour cannot vary by target.
Initialization helpers that need a random palette or colour propagate exhaustion
as a preparation fault, preserving the previous instance; they must not add an
unbounded retry or silently choose a fallback colour.

### Free-position selection

```python
def choose_position():
    free_count = count - active_count
    if free_count == 0:
        return -1

    ok, rank = random.try_index(free_count)
    if not ok:
        return -1

    for position in range(count):
        if not occupied[position]:
            if rank == 0:
                return position
            rank -= 1
    return -1  # Defensive fallback; correct occupancy invariants make this unreachable.
```

The spawn helper first scans at most `max_stars` slots for the first inactive
one. If none is free, return without drawing a position. Otherwise select a rank
among the free pixels and scan at most `count` pixels to locate it. Provided the
rank is uniform, every free pixel has equal probability; a random-start/first-free
scan would bias the choice toward pixels after long occupied runs.

Only mark the slot/pixel active after every spawn decision succeeds, including
random colour selection if requested. If position/colour selection exhausts its
bounded RNG attempts, skip this birth and continue expiry/render normally. Do not
leave a half-born star or corrupt `active_count`. A fixed twinkle colour requires
no extra random colour draw. A random colour uses the original colourwheel's
bounded 361-choice helper/table and the same checked integer selection policy.

This gives at most `O(max_stars + count)` work per spawn attempt plus
`O(max_stars)` expiry and star rendering; background fill is `O(count)`. No
per-frame allocation or unbounded position retry remains. Stop at the first
successful position/slot, retaining the original one-birth-per-update limit.

Review-visible differences from the original:

- Dead positions become reusable immediately after their expiry update.
- A zero-time birth is valid, using an active flag.
- Choosing a free rank consumes a different random sequence from collision
  retries; exact seeded trajectories will change. Distribution over available
  positions is preserved under uniform random input, not the original draw count.
- The rare four-rejection outcome skips a birth instead of retrying forever.
- Birth timestamps use integer stamps; the fade converts elapsed differences
  to f32. Keep colour quantization and sine tolerances explicit in replay tests.
- All clock reads within a tick agree. A newly born star starts at age zero,
  removing the original's small intra-draw clock differences and negative ages.
- Updates use the declared logical cadence, not whatever rate the physical
  CircuitPython loop happened to achieve.

Use `duration > 0`, `probability >= 0`, `brightness` in `[0,1]` and
`1 <= max_stars <= count`. For this first faithful port, treat original
constructor-only colour, brightness, duration, probability and capacity inputs
as settings. Optional `twinkle_color=None` can be resolved by the authoring
adapter into a random-colour flag plus a typed colour; it does not require a
general dynamic None/object value in the VM.

Twinkle's payload is `14 * max_stars + count` bytes: eight timestamp
bytes, two position bytes, three RGB bytes and one active byte per slot, plus
one occupied byte per pixel. This excludes scalar
state, RNG state, descriptors/alignment and output buffers. It uses no retained
full RGB paint array. Keeping the original f32 birth array would save four bytes
per star but loses the long-running clock guarantee; the recommendation is stamps.

## Consequences for the six examples and editor

| Example | State/render adaptation under this API |
|---|---|
| Color | Resolve its constructor colour in init/settings; render the same fill. Random defaults use the declared stream and original colourwheel helper. |
| Pattern | Resolve a fixed pattern off-device or initialize a bounded colour array once; keep repeat/stretch indexing and original fit default. Random palette length/capacity must be admitted before execution. |
| Chase | Keep draw count and persistent paint; write only the advancing pixel during step, then copy paint during render. Cycle notification does not itself stop it. |
| Wipe | Keep draw count, step width, two range writes and original completion condition in simulation; render retained paint. Clamp/validate legacy slice bounds explicitly rather than relying on unchecked stores. |
| Rainbow | Initialize colour-derived hue and spatial scale, use duration-based progress and the compatible colourwheel. Render the original hue-per-index calculation using the implemented colour conversion helpers. |
| Twinkle | Keep birth/position/channel arrays and its probability/fade equations; use the reviewed active/occupied fix and stage separation above. |

For Pattern's random default, admit `array(rgb8, length=8)` and store a separate
`pattern_length` selected in init from 2..8. Render indexes the active prefix
using that length, not the capacity. This preserves its chosen pattern without
allocating during init. Explicit string patterns are decoded off-device into
bounded immutable data; no general string/list runtime is required.

Accepted lifecycle mapping: Color, Pattern and Twinkle run until externally
stopped; Chase reports cycles and continues; Rainbow reports duration boundaries
and continues; standalone Wipe calls `cycle_complete()` and `complete()` at its
existing end condition. Wipe's terminal policy is an explicit adapter decision,
not an assumption that every legacy cycle flag was terminal. Wipe's slice bounds
must also be checked against the actual original pixel binding for short strips
and step widths that do not divide the strip. If normalization would change
which pixels it writes, stop for review rather than silently substituting a
different wipe algorithm.

Expose original constructor controls as preparation settings where their effect
is inherently initialization-only. Keep genuine live parameters separate in the
editor and label settings as requiring restart. The example dropdown continues
loading canonical source files; the default snippet must come from the same
canonical example. Do not replace all six snippets with newly advertised syntax
until the device/compiler capability checks and original-algorithm replay pass.

## Versioning, admission and fault behaviour

Implemented identifiers: source profile `animation/1`, package `LBC1/3`,
program execution ABI 3 and manifest `lightvm.program/4`. The existing LBC1/2, ABI 2 and schema-3 path remains
available with its existing semantics; merely recompiling unmarked old source
must not opt into this profile.

Keep transport framing, storage names and existing management operations. Add
capability lists for supported program ABIs/formats/profiles while retaining the
legacy scalar descriptor fields for old clients. New compiler/editor versions
select a supported profile explicitly and reject unsupported targets. Older
firmware rejects new packages rather than executing them with different rules.

The new manifest/package must describe resource IDs/types, checked size
expressions and bounds, initial values, settings, stage entries/permissions,
period/first-tick/overload policy, RNG/library versions, register/call bounds and
complete peak memory. The device independently verifies encodings, types, every
resource access, stage permissions and actual target budgets. Native buffers are
capabilities; ordinary registers cannot forge their addresses or rights.

At minimum the backend needs checked element load/store, resource fill/copy,
clock-stamp operations, RNG operations and cycle/complete signalling, plus init
entry metadata. These may be opcodes or versioned native operations. The [implementation notes](../docs/animation-profile.md) freeze the opcode IDs,
header layout and library bindings without changing the accepted source semantics.

Persistent simulation writes are ordered but **not transactional**. On a fault,
stop the instance, discard unpublished output and pending notifications, and
preserve the last successful displayed image. Require reset before further
simulation; do not resume partially changed state as a valid tick. This avoids
requiring a rollback copy of every array. General neighbourhood algorithms can
later declare explicit previous/next arrays; no implicit swap is needed by these
six effects. Publication retains/seals storage until consumers release it, and
reset/replacement must not reclaim outstanding views.

## Acceptance gates

1. Persistent paint retains earlier writes across ticks and repeated renders;
   reset restores declared state, instances are isolated and stale views fault.
2. Init and immediate tick zero run exactly once. Parameter updates/resume/render
   do not rerun either. Failed preparation preserves the current live instance.
3. Step cannot write output; render cannot mutate persistent arrays/RNG or signal
   completion. Helper calls cannot bypass those checks.
4. Equal logical timelines produce equal state at different presentation rates,
   traversal orders and slice sizes. Exercise fractional manual-frame advances,
   bounded catch-up/slowdown, pause/resume and rejected oversized debug advances.
5. Completion commits the final frame before becoming visible, then holds it;
   faults suppress completion. Cycles are counted once and do not stop repeating
   effects. Test a Wipe whose last paint and completion occur on different ticks.
6. Twinkle: exhaust every occupancy mask on short strips; each free rank selects
   exactly one different free pixel; full pools skip birth; expiry releases
   positions; no duplicates arise; rejected RNG draws cannot partially allocate.
   Test zero-time birth, one-pixel strips, capacity=count and exact expiry boundary.
7. Compare against original state/render algorithms at controlled timestamps and
   random decisions. Record the explicitly approved Twinkle, cadence, completion
   and numeric differences; do not use the current simplified samples as oracles.
8. Enforce complete resource/work budgets on actual target profiles. Keep old
   package/compiler/editor fixtures passing alongside the new profile.

## Review outcome

Kaj accepted the following recommendations before implementation:

- Accept `array`/`pixel_buffer`/`setting` plus an explicit profile marker as the
  source vocabulary, with init/step ownership and read-only simulation during render.
- Accept configurable 50 Hz example updates, immediate tick zero, four-tick
  catch-up and explicit slowdown on overload.
- Accept cycle-only notification versus terminal complete-and-hold, including
  the documented legacy adapter corrections and required reset to restart.
- Accept Twinkle's active/occupied bookkeeping, integer birth stamps, free-rank
  selection and skip-on-bounded-RNG-exhaustion changes.
- Accept a new negotiated profile while keeping the existing program path;
  freeze PRNG test vectors and binary resource encoding during implementation,
  without revising these semantics silently.
