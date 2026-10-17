# Animation profile

The proof-of-concept Python API has two decorators: `@init` and `@render`.
The compiler emits LBC1/4, execution ABI 4 and `lightvm.program/10`, with profile
`animation/1`. It parses source without executing it.

There is one source profile. Every animation can run alone or as a [scene layer](composition.md).
Each frame starts blank. Unpainted pixels let lower layers remain visible.
Ordinary colour writes paint solid pixels. Alpha is optional for effects that need partial transparency.
The same profile supports native drawing, [PixelArray and sprites](pixelarray-sprites.md),
periodic drawing, and [optimized pixel loops](pixel-maps.md).

```python
from lightvm import profile, setting, rgb, init, render
profile("animation/1")

@init
def setup(ctx, runtime):
    ctx.color = setting(rgb(255, 64, 0))
    ctx.speed = setting(50.0, minimum=0.0)
    ctx.progress = 1.0

@render
def draw(ctx, runtime, pixels):
    ctx.progress = min(float(pixels.count), ctx.progress + runtime.delta * ctx.speed)
    pixels.fill(ctx.color, start=0, stop=int(ctx.progress))
```

## Caller arguments and lifetime

`@init` receives `(ctx, runtime)` once per instance. It is optional. `@render`
receives `(ctx, runtime, pixels)` and is required. These are ordinary positional
arguments; their names can differ from the example. Decorators take no options.
There are no implicit `time`, `delta`, `count`, `index` or `pixels` globals.

`ctx` holds per-instance scalar fields and managed arrays. Declare fields directly
in init, before using them. Their types and array bindings stay fixed. Render may
change ordinary context fields and array elements. Mutable module-level state and `global`
are unsupported; readonly PixelArray artwork and Sprite definitions may be
declared at module scope.

`runtime.time` is the pausable elapsed program time in seconds. `runtime.delta`
is the actual elapsed time supplied for this render, also in seconds.
`runtime.pixel_count` is available during initialization and rendering.
Runtime properties are read-only. `pixels.count` describes the output buffer.
Pixels support indexed reads/writes and `fill(color, start=0, stop=pixels.count)`;
stop is exclusive. Each render starts with transparent working pixels.

Use `pixels.clear()` to remove all paint during a render. A normal render does not need this call.
Use `pixels.clear(start=first, stop=last)` to remove paint from a range.
Use `pixels.fill(rgb(0, 0, 0))` to paint solid black and cover lower layers.
Do not use a black fill to make an array blank. Use `clear()` instead.

An owned `PixelArray` retains its contents between frames. Use `image.clear()` before drawing a new image when necessary.
New arrays start with zero channels. The default array format supports transparency without an explicit alpha argument.
For example:

```python
ctx.image.clear()
ctx.image[3] = rgb(255, 64, 0)
pixels.draw(ctx.image)
```

Direct playback displays the animation over black. A scene displays it over the lower layers.
The animation code is the same in both cases.

Preparation calls init at time/delta zero, then renders the initial frame at zero.
Each subsequent render runs once with the supplied delta. A zero-delta render is
still a call. The animation cannot request a period: there is no fixed-step
scheduler, catch-up loop, cycle notification or completion callback. Animations
keep rendering until the host stops them or execution faults.

The VM can suspend a render between instruction slices and resume the same call.
Its arguments remain unchanged across those slices. Only completed frames are
published. A runtime fault preserves the previous published frame and stops the
instance; context changes made before the fault are not rolled back. Pause and
STOP finish an already-started render before suspending; STOP also clears output.
The native presenter retains each complete frame until transfer finishes.

## Frozen settings

`setting(default, minimum=..., maximum=...)` is declared directly in init, either
as a local variable or a ctx field. All settings are frozen for that instance.
There are no live setting updates or change listeners in this proof of concept.
The separate source `parameter(...)` API is removed.

Names come from assignment targets: `ctx.duration = setting(...)` exposes the
setting `duration`. Defaults and bounds can use literals, `runtime.pixel_count`,
earlier settings, arithmetic, and two-argument `min`/`max`. Bounds are inclusive
and numeric types must match. Initialization validates overrides before execution.

Existing activation and `program_reset(settings=...)` operations initialize a
fresh instance with supplied overrides. Reset keeps the old instance if candidate
preparation fails. `program_parameters_get` reports `manifest.settings` and
`settings_values`; `program_parameters_set` rejects this profile. The editor's
initialization settings panel applies settings through an explicit restart.
Activation/reset inputs belong to that invocation. Per-device `startup.args` in
configuration supplies inputs at boot; omitted arguments use the program defaults.

## Arrays and standard library subset

Use `from array import array` and `array(typecode, initializer)` in init. Supported
typecodes are `i`, `I`, `f`, `B`, and `H`. Initializers are literal lists/tuples or
`[value] * length`. Length may depend on initialization settings or pixel count.
Storage is allocated during preparation; rendering does not allocate. Indexing,
assignment and `len(array)` are supported. Resizing, slicing and append are not.
The integer value domain is signed 32-bit, including values stored in `I` arrays;
packed RGB fits within this domain. Narrow stores and invalid indices fault.

Use normal `import random`. The supported functions follow these call shapes:

- `random.seed(a=None, version=2)`: integer or None seeds; version 1 or 2.
- `random.random()`: float in `[0, 1)`.
- `random.randint(a, b)`: inclusive integer endpoints, including keyword arguments.
- `random.randrange(stop)` or `random.randrange(start, stop[, step])`: positional
  integer arguments; negative steps are supported and stop is excluded.

An empty integer range or zero step faults. An omitted seed uses native entropy
inputs (clock and an instance sequence). Explicit integer seeds reproduce the
per-instance PCG32 sequence across hosts; this is not CPython's generator or a
cryptographic RNG. Unbiased integer sampling may yield and resume internally.
`random.randint(0, 359)` is the Rainbow hue selection.

`math.sin`, `math.pi`, and their explicit from-import forms are supported. LightVM
provides `rgb`, `hsv`, `hash01`, `red`, `green`, `blue`, `pico_colorwheel`,
`hue_degrees` and `colorwheel`. Hue degrees uses the original rounded hue; Rainbow
converts it into the colorwheel's 255-unit scale.

The [motion and easing libraries](motion.md) compile bundled scalar functions
into ordinary bytecode. They support explicit imports, named arguments and their
documented defaults. Plain `assert condition` faults the invocation when false.

User helpers are positional, nonrecursive, statically inlined and must return a value
on every path. Scalar arguments are copied; ctx/runtime/pixels arguments retain
access to the caller objects. The subset supports scalar arithmetic, comparisons,
conditionals, `range(bound)` loops, break/continue, int/float and two-argument
min/max. Numbers use signed wrapping i32, binary32 floats, bool and packed RGB8.
General Python objects, arbitrary imports, exceptions and dynamic allocation are
unsupported. Sprite, PixelArray, Transform and ColorTransform are statically
compiled SDK descriptions rather than arbitrary Python objects. Standard-library support is limited to the forms above.

## Package and validation

The 96-byte LBC1/4 header describes typed registers, instructions, functions,
resources, declaration expressions, native calls and settings. It binds init and
render entry IDs; old scheduler fields are reserved zero. Both entries receive
three typed native values: pixel count, time and delta. Context and output storage
are instance bindings. Init may write state and use randomness; render may also
write output. Admission independently validates these rights and signatures.

All new programs use schema 10, profile binding 3, and premultiplied RGBA output.
The public profile name remains `animation/1`. Targets must advertise schema 10 before accepting new programs.
Older firmware rejects these packages instead of using the previous RGB rules.
Stored packages with profile bindings 1 and 2 retain their original behavior.
The compiler no longer accepts `layer/1` source declarations.

Header word 80 identifies the drawing API. Zero selects basic operations, 2 adds images and sprites,
3 adds periodic drawing, and 4 adds compiler-generated pixel maps.
The remaining three trailing words stay zero. Extended native sites support eight arguments in each 16-byte record.

`render_count` counts successful calls including the initial frame. `program_us`
tracks elapsed time; snapshots are passive. The fixed-step statistics and cycle
fields have been removed. FRAME keeps its existing wire range of 0..1000 ms;
autonomous playback supplies its complete elapsed delta without clamping it.

Tests compile the six Python examples in `examples/animations/`.
An independent Python caller checks Color, Pattern, Wipe, and Rainbow output.
Sprite tests check Stars and Rainbow Sprite, including transparency, clocks, and movement.
The suite also checks seeded random ranges, arrays, variable frame rates, frozen settings,
fault publication, and native continuation/ownership. Hardware measurements below describe the previous API;
they are historical evidence, not measurements of LBC1/4.

## Historical measurements (LBC1/3, before this API change)

## Measured memory

Measured on the 64-bit host build, 175 RGB pixels, default settings, fresh process
per effect (bytes). The heap tracker includes allocation headers and the native
output charge. State includes registers, resource descriptors and packed payload;
register/payload columns are subsets, not additional allocations.

| Example | Package | Metadata | State | Registers | Payload | Steady tracked heap | Preparation peak | Reset peak |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| color | 984 | 54 | 112 | 44 | 0 | 3093 | 4141 | 7234 |
| pattern | 1527 | 90 | 216 | 60 | 24 | 3233 | 4481 | 7714 |
| chase | 889 | 48 | 629 | 36 | 525 | 3604 | 4764 | 8368 |
| wipe | 1146 | 66 | 645 | 56 | 525 | 3638 | 4814 | 8452 |
| rainbow | 1977 | 118 | 152 | 84 | 0 | 3197 | 4245 | 7442 |
| twinkle | 3822 | 168 | 765 | 124 | 413 | 3860 | 5268 | 9128 |

Each row additionally has a 1,272-byte portable context and 40-byte continuation,
already included in tracked heap, and 1,575 bytes of RGB buffers (working,
committed and physical output), also included. Call storage is zero because
helpers are inlined. Twinkle payload is 14×17 + 175 = 413 bytes.

The enclosing `VmInstance` is **1,040 bytes on this host**, down from 35,680.
It contains no package/upload array and no embedded legacy interpreter. V2
interpreter state is allocated only for v2 packages. V3 immutable tables and
initial values remain in flash; compact type/layout bindings use roughly three
bytes per register. Mutable ordinary registers use four bytes, timestamps eight;
constants have no mutable register slot. Code, palettes and immutable table size
do not determine steady RAM consumption.

The shared App is 30,488 bytes on this host, including eight 1,040-byte slot
contexts, down from the earlier 79,992-byte single-slot App. It includes one
336-byte upload workspace (256 bytes of page data), configuration staging and
the store's bounded extent references. Slot contexts are embedded in App and are
not included in steady tracked heap; replacement candidates are heap allocated and
included in peaks. Preparation now happens before a transient upload replaces the
old instance. The table uses fresh processes and includes that candidate in its
preparation peak. Reset also overlaps old/new resources and admission temporaries.
Native App/transport/driver memory and stacks are outside the VM heap figures.
These numbers are not total firmware RAM or a cross-runtime performance benchmark.

The multi-device Pico W HTTP/boot build uses 115,880 static bytes, including
a 29,720-byte App with eight 968-byte slot contexts. Its RAM gate reserves a
65,536-byte VM heap, 16,384-byte native stack, 6,144 bytes for aggregate Pico
conversion buffers/drivers, and 16,384 bytes of operating headroom, leaving
50,008 bytes. Aggregate configured pixels cannot exceed the board pixel capacity. Native SDK/driver costs remain separate from per-effect payload figures.
ESP32-C3 HTTP/boot and ESP32-S3/C6 USB builds also pass. The changed DMA/RMT
paths still need on-device timing, cancellation and stack high-water checks;
cross-builds and simulated delayed-output tests do not establish those timings.

Before the multi-device extension, Arm GCC 13.2 `-O3 -fstack-usage` reported the
largest individual core frames as
management 6,480 bytes, control management 5,848 and manifest validation 5,240.
Decoded-key scratch is outside recursive JSON parsing, reducing each recursive
frame from 312 bytes to at most 56 (depth remains bounded at 16). The shared
native stack is retained during cooperative management work. Native polling
callbacks must only enqueue commands; the command guard prevents recursive
execution and defers upload cancellation until the current command releases its
workspace. The 16 KiB reservation still requires hardware high-water verification.

## Pico W simultaneous-device verification (2026-10-15)

These historical measurements use the earlier example set. Chase and Twinkle have since been removed.

On Pico W `E6614103E7236D37`, the GP16 physical output retained the original
200-pixel Rainbow at brightness 100/255. Five additional named `virtual_rgb`
devices each ran an independent 100-pixel Color, Pattern, Chase, Wipe or Twinkle
instance. They use separate VM state and RGB buffers, with a timed output sink
on the controller; only Rainbow exercised physical PIO/DMA in this test.

The 60-second run passed exact fixed-step host parity for all five added
animations, independent pause/resume, unchanged generation on A during uploads,
and stable aggregate VM heap. Heap remained **16,632 bytes**, with a
**20,868-byte** preparation/reset peak. Each Pico slot context is 968 bytes in
App; the single shared upload workspace is 328 bytes including its 256-byte page.
These figures exclude static App/transport memory, native drivers and stacks.

| Slot | Device | Pixels | Average output fps |
|---|---|---:|---:|
| A | Physical Rainbow, GP16 | 200 | 31.25 |
| B | Virtual Color | 100 | 48.53 |
| C | Virtual Pattern | 100 | 35.16 |
| D | Virtual Chase | 100 | 40.96 |
| E | Virtual Wipe | 100 | 34.38 |
| F | Virtual Twinkle | 100 | 34.69 |

Rainbow alone measured approximately 47.6 fps in the short baseline sample.
That historical run used the former one-shot Wipe, with a one-second update
interval; it held its output after approximately 51 seconds. Wipe now repeats,
so the recorded average includes a hold that no longer occurs. That workload therefore
does not establish six simultaneous 60 fps outputs. No device faulted. Saved
configuration and the procedure library were unchanged; the extra devices were
left in the active RAM configuration.

The first concurrent attempt exposed excessive background work during manual
commands; checkpoints now cap each background v3 instance at 64 dispatches.
USB ingress now reads up to 256 bytes per scheduler pass, resolving the severe
command latency caused by processing one byte per pass under concurrent load.
The final run used both fixes. Reproduce with `tools/smoke_multi_device.py`.
Electrical timing across multiple physical strips, RMT hardware behavior and
native stack high-water measurements remain separate checks.
