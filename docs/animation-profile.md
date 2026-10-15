# Animation profile 1

The six supplied examples now use `profile("animation/1")`, LBC1/3, execution ABI
3 and `lightvm.program/4`. Unmarked source still compiles to LBC1/2 with its old
semantics. The [accepted API](../documents/feature-proposal-persistent-animation-state.md)
defines ownership, initialization, timing, completion and the bounded Twinkle fix.
This document records the implemented bindings and their limits.

## Authoring

```python
from lightvm import profile, setting, pixel_buffer, rgb, step, kernel, cycle_complete
profile("animation/1")
color = setting(rgb(255, 64, 0))
update_us = setting(20_000, minimum=1_000, maximum=1_000_000)
paint = pixel_buffer(length=count)
draw_count = 0

@step(period_us=update_us, first="immediate", max_catch_up=4, overload="slow")
def update():
    global draw_count
    paint[draw_count % count] = color
    draw_count += 1
    if draw_count % count == 0:
        cycle_complete()

@kernel
def draw():
    pixels.copy_from(paint)
```

`array(i32|f32|rgb8|bool8|u8|u16|stamp, length=..., fill=...)` and
`pixel_buffer(...)` declare persistent native storage. Their size is fixed at
preparation; `len`, indexed access, `fill(value, start=..., stop=...)` and
whole-array `copy_from` are checked. Stop is exclusive. Empty arrays/ranges are
valid; out-of-range access and narrowing stores fault. An immutable RGB tuple
can be indexed or copied into an equal-length RGB array.

`@init` runs once after templates/settings resolve. Init and step may mutate
persistent state; prepare derives frame scalars, reset before each prepare;
shade/kernel read stable simulation state. Kernel alone reads/writes working
`pixels`, which starts black each render. Scalar assignments require `global`
when referring to module state. Helpers are statically inlined, positional,
nonrecursive, at most eight nested calls, and must return a value on every path.
Effects are checked through helpers. Scalar arguments are passed by value;
resource arguments retain their binding and permissions. The compiler never
executes source Python, and these source forms are not desktop SDK constructors.

Declaration expressions support literals, `count`, earlier settings, `+ - * // %
/`, and two-argument `min`/`max`; integer size arithmetic is checked, not wrapping.
Settings are immutable bool/i32/f32/RGB values. Numeric bounds have the setting's
type. Resource sizes cannot depend on live parameters or runtime calls. Supplied
examples use settings for their original constructor controls. A custom live
`parameter` still supports atomic updates; using it in init does not make later
updates rerun init.

## Execution and lifecycle

An explicit step period must resolve to 1,000..1,000,000 microseconds. Examples
use 20,000. Immediate tick zero has delta zero; later ticks receive one period.
`first="after_period"` omits tick zero. Render sees the last successful tick's
logical time. No-step programs follow pausable elapsed time. `now()` produces an
opaque u64 microsecond stamp; `elapsed(stamp)` subtracts integers before converting
to f32 seconds. Zero is a valid birth time.

Playback preserves fractional wall credit, executes at most `max_catch_up`
(default four, maximum 64) updates, and counts/discards excess whole intervals.
Discarding wall debt never skips a simulation transition. Autonomous work yields
to the native event loop after at most 4,096 VM dispatches; native fill/copy does
at most 32 elements per dispatch. Parameters remain stable for the entire pass.
Each pass is bounded by two million aggregate dispatches and a 500 ms deadline.
Preparation and manual FRAME use the same aggregate limits synchronously.

FRAME accepts 0..1000 ms, processes all due logical ticks and renders once. More
than 64 due ticks is rejected before execution, even before fresh init. Repeated
FRAME(0) does not update state. Pause/STOP finish a partially started tick, cancel
unstarted catch-up ticks (`cancelled_wall_ticks`), discard unfinished render and
retain fractional credit. Pause holds the last image; STOP clears physical output.
PLAY re-renders retained state and resumes with a fresh wall-time anchor.

`cycle_complete(count=1)` increments a checked counter after a successful update;
it does not stop. `complete()` requests the final render without returning early.
Completion and cycles from that terminal update become visible only after the
final contribution and attached native output succeed. Failure preserves the
previous complete image and reports a fault. Offscreen completion needs only a
contribution commit. Completed animations hold their image/time until fresh reset;
PLAY can redisplay a held image after STOP but cannot restart it. The component
remains available; independent service handlers are not terminated by completion.

Simulation writes are not rolled back after a fault. Reset is required before
further updates. Reset/activation prepare a candidate alongside the old instance
and replace it only after successful preparation/presentation; memory admission
includes coexistence and rejects a candidate that cannot fit. Raw upload retains
its existing replace/unload behaviour and is not this transactional reset API.

## Management and editor

`describe` retains legacy scalar fields and adds `program_abis`,
`bytecode_formats`, `program_profiles`, `max_manual_ticks`, `animation_reset` and
`activation_settings`. The compiler/editor require explicit profile support.

```json
{"op":"procedure_activate","name":"twinkle","slot":"A","settings":{"max_stars":12}}
{"op":"program_reset","expected_generation":7,"settings":{"duration":1.5},"values":{}}
```

Reset retains resolved settings and current parameter values unless overridden;
success increments the instance generation and preserves whether playback was
running. Failure keeps the old instance/output. Transport RESET still unloads.
Parameter updates to completed instances return `requires_reset`.

Parameter/snapshot status adds `completed`, `cycle_count`, `completion_time_us`,
`logical_ticks`, `active_wall_us`, `dropped_wall_ticks`, `cancelled_wall_ticks`,
`random_exhaustions`, resolved `settings_values`, and memory fields below.
The editor loads all six canonical files and imports the same Rainbow file for
its initial `rainbow.py`, selected in the dropdown. Rainbow's existing duration
setting controls its cycle time, with a 60-second default.
Its preparation settings panel applies a fresh reset;
live parameters remain a separate control. Overrides are runtime-only. To retain
settings after reboot, change source defaults and save the recompiled procedure.
Startup-binding overrides and init-only parameter diagnostics are deferred.

## Portable container and native calls

The VM's typed function table is general. Its host invokes a selected function
with checked typed arguments and receives a typed return. The animation adapter
selects lifecycle entries, schedules updates and traverses pixels; the interpreter
does not embed that scheduling. Native contract tests also execute a non-animation
callback and a private array in resource slot zero without any output binding.
No networking, dynamic resource registration or general service framework is
introduced here. Source helpers are inlined, so guest call storage is currently
zero; future bytecode-to-bytecode calls need their own admitted frame bound.

All table words are little endian. Header: 96 bytes (`LBC1`, followed by 23 u32s).

| Offset | Fields |
|---|---|
| 4 | Version = 3 |
| 8..36 | Register, instruction, palette-colour, function, resource, declaration-expression, native-call-site, setting counts |
| 40..52 | Animation step, prepare, render, init function IDs |
| 56..76 | Render mode (1 shade/2 kernel), period expression ID, catch-up bound, first (0 after-period/1 immediate), profile ID (1 animation), manifest bytes |
| 80..92 | Four reserved zero words |

After the header: registers (8 bytes each), instructions (8), palette colours
(4), functions (24), resource declarations (24), expressions (16), native call
sites (16), settings (16), then UTF-8 JSON manifest. Functions contain first/end
instruction, rights, result type, argument-start register and argument count.
Arguments occupy typed input registers, maximum eight. Resource declarations
contain kind, length/fill/seed/stream expression IDs and flags (1 borrowed output).
Expressions form a checked acyclic graph. Call sites contain native binding ID,
argument count, four register bytes and a reserved zero word. Settings contain
register/default/minimum/maximum expression IDs; UINT32_MAX means absent bound.

Types: 1 i32, 2 f32, 3 RGB8, 4 bool, 5 stamp, 6 resource reference. Array kinds use
1..5 plus 7 u8, 8 u16, 9 RNG, 10 immutable palette. Registers hold eight-byte
values; arrays use packed element storage. Reference values are checked IDs,
never native pointers. Function effects are state write=1, frame write=2,
output write=4, RNG=8, animation event=16. The profile requires exact lifecycle
permissions; these restrictions do not impose an animation lifecycle on other
portable functions.

Scalar instruction IDs retain LBC1/2 assignments. Legacy PALETTE (22) and the
profile-specific/fused operations 28..32 are not accepted in v3; arrays and
ordinary typed instructions replace them. The new instructions are:

| ID | Operation | Operands |
|---|---|---|
| 33 | LOAD_ELEM | destination, resource, i32 index |
| 34 | STORE_ELEM | resource, i32 index, value |
| 35 | LENGTH | destination, resource |
| 36 | RESOURCE_FILL | resource, value, start register, stop register |
| 37 | RESOURCE_COPY | destination resource, source resource |
| 38 | NATIVE | result register (255 for void), call-site ID |

The common typed native-call boundary handles clocks, random streams and lifecycle
signals; none needs its own interpreter opcode. Binding IDs are fixed for v3:
1 now, 2 elapsed, 3 uniform, 4 try_index, 5 cycle_complete, 6 complete, 7 sin,
8 red, 9 green, 10 blue, 11 rainbowio-compatible colorwheel, 12 original
pico_colorwheel, 13 rounded hue_degrees, 14 require (fault if false).
`try_index` has an internal i32 result, -1 on exhaustion; source scalarizes that
to `(False, 0)` or `(True, rank)`. RNG v1 and its four-attempt rejection policy
are fixed in the accepted API. RGB8 uses the existing checked channel rules.

Maximums: 128 physical registers, 2,048 instructions, 1,024 palette colours,
16 functions/resources/settings, 128 declaration expressions, 256 native call
sites, eight live parameters, 8 KiB manifest and 32 KiB package, further limited
by board `MODULE_LIMIT` and aggregate `VM_HEAP_LIMIT`. Each array has at most
65,535 elements. Lengths/capacities are also constrained by actual byte admission.
The loader independently checks types, permissions, indices and all table entries.
The compiler uses liveness allocation to reuse typed local registers.

## Original fidelity and validation

The [original source fixtures](../tests/original_picolights/README.md) preserve the
supplied methods. Replay supplies controlled clocks/RNG and substitutes only the
accepted bounded Twinkle allocation and Chase's missing colour binding. Color,
Pattern, Chase and Wipe compare exactly. Rainbow/Twinkle permit one channel level
for f32 arithmetic; integer birth-time differences remain precise at long uptime.
Original f64 and VM f32 threshold comparisons can differ at values not exactly
representable in f32; the VM's f32 duration/elapsed comparison is normative.

Tests cover counts 1, 5 and 175, varied presentation schedules, constructor
settings, full star pools, short/non-divisible Wipe widths, all 9,227 selector
occupancy/rank cases through compiled bytecode, forced RNG rejection, late clocks,
permissions, stale views, render/publication faults, reset/activation rollback,
storage relocation, bounded autonomous suspension and no execution-time allocation.
Legacy fixtures retain the previous simplified algorithms' tests. Browser checks
exercise compilation, upload, storage and settings restart against the native host.

## Measured memory

Measured on the 64-bit host build, 175 RGB pixels, default settings, fresh process
per effect (bytes). The heap tracker includes allocation headers and the native
output charge. State includes registers, resource descriptors and packed payload;
register/payload columns are subsets, not additional allocations.

| Example | Package | Metadata | State | Registers | Payload | Steady tracked heap | Preparation peak | Reset peak |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| color | 984 | 496 | 208 | 144 | 0 | 3631 | 3631 | 42950 |
| pattern | 1527 | 688 | 392 | 240 | 24 | 4007 | 4383 | 44078 |
| chase | 889 | 448 | 717 | 128 | 525 | 4092 | 4292 | 44072 |
| wipe | 1146 | 544 | 765 | 176 | 525 | 4236 | 4484 | 44408 |
| rainbow | 1977 | 952 | 376 | 312 | 0 | 4255 | 4255 | 44198 |
| twinkle | 3822 | 1592 | 1085 | 448 | 413 | 5604 | 6284 | 47576 |

Each row additionally has a 1,272-byte portable context and 40-byte continuation,
already included in tracked heap, and 1,575 bytes of RGB buffers (working,
committed and physical output), also included. Call storage is zero because
helpers are inlined. Twinkle payload is 14×17 + 175 = 413 bytes.

The enclosing `VmInstance` is **35,680 bytes on this host**, including the existing
32 KiB upload staging area and legacy VM context. The live instance is embedded
in native App storage and is **not** included in steady tracked heap. A replacement
candidate is heap allocated, so the reset peaks include that candidate, old/new
resources and admission temporaries. Package bytes occupy staging or native
storage and must not be added a second time when counting that staging allocation.
Native App/transport/driver memory and stacks also remain outside these per-effect
heap figures. These are not total firmware RAM or evidence of an inherent memory
advantage over WASM.

The Pico W HTTP/boot build passes the link-time RAM gate: 165,992 static bytes,
65,536-byte VM heap ceiling, 16,384-byte native stack, 3,072-byte driver reservation
and 16,384-byte operating headroom, leaving 2,968 bytes. ESP32-C3 HTTP/boot and ESP32-S3 USB also
cross-compile. Layout sizes differ on 32-bit targets; these are build/admission
checks, not on-hardware stack high-water or timing measurements. No firmware was
flashed as part of this change.

The Arm GCC 13.2 `-O3 -fstack-usage` audit also checks the new management paths.
Control requests bypass the general upload/management parser frames. Largest
individual core frames are management 6,480 bytes, control management 5,848,
manifest validation 5,224 and declaration evaluation 1,112. Reset's retained core
chain is approximately 11.3 KiB; activation's approximately 12 KiB, before native
transport/libc frames. Keeping these parser paths separate removes a former
nesting of more than 22 KiB. The reserved 16 KiB native stack still needs hardware
high-water verification; these compiler estimates do not substitute for it.
