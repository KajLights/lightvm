# Control ABI 1 and the Python animation frontend

The first control profile implements one independently owned RGB8 contribution
on logical resource `A`. It follows the control feature notes' named resources,
start/frame triggers, program time, typed properties, ownership and successful
step publication. Its import namespace is `lightvm.control.v1`; the legacy
`led` namespace and `render`/`beforeRender` callbacks remain a separate adapter.
Transport framing and configuration remain version 1.

Scenes, lower-layer filters, A/B transitions, device events, waits, RGBW and
multiple outputs are not advertised by this profile. Presence is already
explicit so a subsequent compositor can distinguish black paint from absence.

## Author and compile

Use Python 3.9+ with the repository's pinned requirements, from the repository
root. Firmware does not include Python, its standard library or Wasmtime.

```python
from lightvm import pixels, parameter, rgb, on_frame

strip = pixels("A", retention="fresh")
colour = parameter("colour", rgb(255, 64, 0), kind="rgb8")

@on_frame
def draw(ctx):
    strip.fill(colour.value)
```

```sh
python -m lightvm.compiler samples/python/rainbow.py -o build/samples/rainbow_control.wasm --wat
python tools/build_samples.py
python tools/client.py --port PORT --store-procedure rainbow-control build/samples/rainbow_control.wasm
python tools/client.py --port PORT --activate rainbow-control
python tools/client.py --port PORT --set-parameters '{"speed":0.3}'
python tools/client.py --port PORT --pause
python tools/client.py --port PORT --snapshot
python tools/client.py --port PORT --resume
```

Configure `A` using the existing configuration workflow before activation. A
saved startup procedure can be a control artifact; its manifest and parameter
defaults are embedded in the stored WASM. Live parameter changes are volatile.
The existing activation replacement guarantee remains destructive.

`compile_source(source, filename="<string>", target=None)` returns a `Compilation`
with `wasm`, `manifest`, `diagnostics`, `wat` and `success`. Invalid source returns
filename/line/column diagnostics. The CLI writes WASM and a matching JSON sidecar;
`--wat` also writes WAT. `--target descriptor.json` checks advertised control ABI
and module size. The device still validates the artifact independently.

The compiler parses source with Python AST and symbol-table tools; it does not
execute declarations or import user-selected modules. A typed expression/control
flow representation lowers to WAT and the existing Wasmtime tool emits WASM.
Only original SDK helpers are included. Scalar state uses WASM globals and
palettes use generated checked lookups; generated modules have no linear memory.

## Python support matrix

| Construct | Initial support |
|---|---|
| Imports | Explicit names from `lightvm` and `lightvm.easing`; aliases supported |
| Resources | Exactly one `pixels("A", retention="fresh" or "retained")` |
| Parameters | Up to eight unique named bool, signed int, float or packed `rgb8` properties |
| State | Literal-initialized module scalars and typed/inferred scalar locals; explicit `global` for mutation |
| Functions | Fixed positional arguments with `int`, `float`, `bool` or `RGB` annotations and a result on every path |
| Handlers | At most one each `@on_start` and `@on_frame`, taking one context; frame handler required |
| Flow | `if`/`else`, conditional expressions, `for NAME in range(...)`, break/continue, return |
| Arithmetic | `+ - * / // %`, integer `& | ^ ~`, comparisons, unary signs |
| Booleans | Short-circuit `and`/`or` on bool operands, `not`, numeric truth in conditions |
| Builtins | `int`, `float`, `bool`, two-argument `min`/`max`, `abs`, `len` of pixels/palettes |
| Colours | `rgb(r,g,b)`, `hsv(h,s,v)`, RGB equality; use constructors for RGB values |
| Collections | Immutable tuples of at most sixteen literal `rgb(...)` colours, with checked indexed reads |
| Unsupported | Classes, recursion, dynamic collections, arbitrary imports, exceptions, async/waits, while loops, comprehensions, slicing, shifts, chained comparisons and runtime keyword arguments |

Integers are signed 64-bit values; add/subtract/multiply and bitwise arithmetic
wrap at that width. Division/modulo use Python's floor/sign convention; division
by zero and signed division overflow trap. Floats use WASM double precision.
`int(float)` truncates toward zero and traps outside signed 64-bit range. Numeric
promotion to float is allowed; RGB and bool retain distinct compiler types.
SDK declarations and builtin names cannot be shadowed by locals or arguments.
Locals retain their inferred type and possible reads before assignment are
rejected. Range arguments evaluate once, left to right; changing the visible
loop variable does not change the iterator. An iteration variable cannot be
used afterward unless it was already initialized before the loop.

## Resource and clock semantics

`ctx.program_us` and `ctx.delta_us` are signed microseconds supplied from the
native pausable program clock. `ctx.time` and `ctx.delta` expose seconds as
floats. All operations in a step use one stable context and parameter snapshot.
Autonomous playback uses actual elapsed time, including overdue frame gaps,
without the legacy millisecond cap. Missed frames coalesce into one update.
The existing explicit `FRAME` command still accepts 0..1000 milliseconds and
pauses autonomous playback; it advances program time by exactly that delta.

Initialization runs once on first PLAY/FRAME after activation, with time/delta
zero. PLAY resumes an initialized program. Pause preserves contribution, output
and state; resume excludes paused time. STOP blanks physical output while holding
program time and committed paint. Reconfiguration, RESET and activation unload
state and invalidate handles. Disconnect leaves autonomous playback running.
Faulted programs preserve inspectable state but require reactivation or RESET
before further guest execution. There are no stop/pause guest handlers yet.
The legacy RUN command is rejected for manifested control programs; only the
control scheduler may execute their guest code.

| Python operation | Meaning |
|---|---|
| `strip.count` / `len(strip)` | Configured logical pixel count |
| `strip[index] = rgb(...)` | Stage colour and mark the pixel present |
| `strip.fill(colour)` | Paint the entire contribution, including presence |
| `strip.clear()` | Paint covered black everywhere |
| `strip.erase(index)` | Remove presence and store black at that index |
| `strip[index]` | Read own working colour in this step |
| `strip.present(index)` | Read own working presence in this step |

Fresh contributions begin each frame empty/black. Retained contributions copy
committed colour and presence before the next step. Start handlers can prepare
retained paint. This one-contribution output composes over black; erased pixels
therefore display black while retaining different presence from painted black.
RGB channels are integers 0..255, packed as `0xRRGGBB`. HSV hue wraps in turns;
saturation/value are finite fractions 0..1. Hue quantizes to 16 bits and S/V to
8 bits by rounding half up, then uses the existing MIT integer HSV conversion.
Native wire ordering and brightness apply once on submission, after logical RGB.

Successful steps commit their contribution and then attempt output submission.
A guest trap or nonzero result discards pending paint; prior commits and guest
state are not rolled back. Output failure stops playback and retains the last
successful output snapshot, while the successfully committed contribution remains
valid. Physical driver failure cannot promise electrical rollback. Snapshots
report separate contribution/output validity and frame counters.

For N pixels, committed/working RGB, committed/working packed presence and the
separate output cost `9N + 2*ceil(N/8)` bytes. Interpreter allocations, buffer
allocation headers and the already allocated output are charged to the current
aggregate VM heap budget. Fixed module/metadata/native structures and driver
allocations additionally occupy native RAM. No heap ceilings are raised. All
functions compile eagerly and storage is reserved before start; rendering does
not allocate. The existing 500 ms guest deadline is a recovery bound, not a
promised rendering frame rate. STATUS reports measured render/output durations.

## Language-neutral wire contract

A control artifact contains exactly one `lightvm.program` custom section with
bounded JSON (at most 8192 bytes and the existing 256-token parser limit):

```json
{"schema":"lightvm.program/1","abi":1,"binding":"A","retention":"fresh","parameters":[{"name":"speed","type":"float","default":0.1,"minimum":-10.0,"maximum":10.0}]}
```

Numeric bounds are optional and inclusive. Bool and RGB8 have intrinsic domains.
Names use the existing 1..32-character ASCII identifier convention. The native
loader rejects duplicate/unknown fields, malformed metadata and unsupported
ABIs/bindings. Control artifacts must have no memory, tables, data/element or
start sections, and may import only the following functions. wasm3 validates
actual function types and code; PLAY/FRAME validates the step export before
initialization. Ordinary legacy modules retain their existing rules.

Export: `control_step(trigger: i32, program_us: i64, delta_us: i64) -> i32`, with
start=0 and frame=1; return zero for success. Other trigger values are reserved.
Calls use instance-owned opaque handles, resolved before start. Invalid calls
trap separately from valid return data; no negative-data error sentinel exists.

| Import from `lightvm.control.v1` | WASM signature |
|---|---|
| `resource` | `(slot:i32) -> i32`; slot 0 resolves A |
| `count` | `(handle:i32) -> i64` |
| `write` | `(handle:i32,index:i64,rgb:i32) -> void` |
| `fill` | `(handle:i32,rgb:i32) -> void` |
| `erase` | `(handle:i32,index:i64) -> void` |
| `read` / `present` | `(handle:i32,index:i64) -> i32` |
| `rgb` | `(r:i64,g:i64,b:i64) -> i32` |
| `hsv` | `(h:f64,s:f64,v:f64) -> i32` |
| `param_int` / `param_float` | `(slot:i32) -> i64 / f64` |
| `param_bool` / `param_color` | `(slot:i32) -> i32` |

Parameter slots follow manifest order. Calls are valid only inside a control
step. The sole engine owner serializes guest execution and management.

## Management and replay

Discovery adds `control_abi`, `legacy_animation_api`, `retention_modes`,
`parameter_limit`, `composition:false` and `program_generation`. MANAGE operations
use the existing framing and DATA/STATUS replies:

| Operation | Inputs/result |
|---|---|
| `program_parameters_get` | Manifest, values, generation/revision, clocks, validity, frame IDs and buffer bytes |
| `program_parameters_set` | `values` patch, `expected_generation`, `expected_parameter_revision`; complete patch validates before applying |
| `program_pause` / `program_resume` | `expected_generation`; preserve state and update playback selection |
| `program_snapshot` | Passive RGB via PIXELS plus DATA containing committed presence hex and state |

RGB8 parameter JSON values are packed integers; float updates must be finite.
Integer and bool updates require their exact JSON kind. A successful patch
increments the parameter revision, including an empty patch. On a lost update
reply, read back revision/values before retrying. Stale requests never change the
replacement program. Mutations occur between complete guest steps.
Generations and parameter revisions are scoped to the current engine boot;
clients rediscover state after reconnecting or restarting a controller.

`Client.program_parameters()`, `set_parameters(values)`, `pause_program()`,
`resume_program()` and `snapshot()` implement these operations; snapshot returns
metadata and RGB bytes. A false output-valid flag yields no RGB bytes. Source
errors have filename/line/column; runtime errors name the failing operation or
step and generation, without instruction-level source mapping in this version.

The SDK's Python classes provide reference semantics for trusted desktop replay;
the compiler does not execute them. Tests replay fixed timestamps/parameters/seeds
against the actual wasm3 host on both profiles. Portable smoke checks compile one
artifact and compare the same bytes/inputs across ports. Physical timing and
colour/wiring correctness still require hardware observation.

## Original effect and easing library

Examples are Color (live colour), Rainbow (continuous hue phase), Pattern
(repeating/stretching palette), Wipe (retained symmetric paint), Chase (moving
scalar phase) and Twinkle (sparse seeded per-pixel lifetimes). Twinkle's density
is visible time fraction, not picolights' probability/star-slot simulation. It
contains no background painter or mutable star array.

Easing clamps normalized progress to [0,1]. For degree n=2 or 3, in is `t^n`, out
is `1-(1-t)^n`, and in/out applies half-scaled copies on either side of 0.5.
Linear is `t`; smoothstep is `3t^2-2t^3`. These functions and the small modular
polynomial `hash01` are original arithmetic implementations. No upstream easing
library, picolights easing code or Adafruit API is embedded or imported.

## Verification record

On 12 October 2026, all 76 Python tests passed on both the default host and
175-pixel host profile, together with each profile's native contract test and
extended portable smoke checks. Native simulated tests deny rendering allocations,
advance an uncapped 1,500,123-microsecond gap, reject aggregate-budget exhaustion,
and inject output failure while retaining the last successful output. Stored
control startup/default restoration is tested using the host snapshot store.

The same core cross-built for the original Pico, XIAO ESP32-S3 and XIAO ESP32-C6.
The six generated modules occupy approximately 1.9–2.5 KiB each without linear
memory. Those initial checks were host/simulated and cross-build results. A subsequent
connected Pico run is recorded below; ESP32 hardware validation remains pending.

## Connected Pico animation runs, 12 October 2026

The connected original Pico ran each of the six original Python animations for
at least 30 seconds continuously on 100 WS2812B pixels, GP16, GRB ordering and
brightness limited to 32/255. The tested control-ABI firmware was installed using
a UF2 whose flash addresses stay below the reserved configuration/procedure
store. Saved configuration and the stored legacy Rainbow module were backed up
and verified unchanged after the firmware update and after the tests.

| Effect | Observed run (s) | Frames in 30 s | Estimated fps | Sampled render time (µs) | Tracked heap (bytes) |
|---|---:|---:|---:|---:|---:|
| Color | 30.010 | 1800 | 59.98 | 56–58 | 19423 |
| Rainbow | 30.022 | 1800 | 59.96 | 7931–8029 | 19487 |
| Pattern | 30.014 | 1800 | 59.97 | 2874–2927 | 19625 |
| Wipe | 30.014 | 1800 | 59.97 | 101–107 | 19636 |
| Chase | 30.014 | 1800 | 59.97 | 91–95 | 19463 |
| Twinkle | 30.088 | 1800 | 59.82 | 11926–12308 | 19479 |

All six passed: no reported guest/output fault, continuous frame advancement,
stable tracked heap and peak during playback, and identical Pico/host RGB
snapshots for explicit deltas 0, 16, 33, 500 and 1000 ms. Native output submissions
took approximately 3.3 ms. The fps estimates include final query/snapshot
overhead; each 30-second status window counted 1800 animation frames. Rendering
timings are periodic samples, not worst-case bounds.

Wipe completed its paint and retained it for the rest of its interval; Color and
Pattern intentionally produce static pictures while frames continue. The original
stored Rainbow playback was restored at the end. Optical appearance and signal
integrity were not independently observed; these checks exercise the real Pico
interpreter, clocks, USB and synchronous physical output driver.
