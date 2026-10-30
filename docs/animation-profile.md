# Animation profile

The proof-of-concept Python API has two decorators: `@init` and `@render`.
The compiler emits portable bytecode with document kind `lightvm.program` and
profile `animation`. Its `requires` field names the minimum LightVM application
release. It parses source without executing it. See [versioning](versioning.md).

Every animation can run alone or as a [composition layer](composition.md).
The separate [transition profile](transition-profile.md) uses the same compiler,
init/render conventions and libraries to combine prepared A/B compositions by progress.
Each frame starts blank. Unpainted pixels let lower layers remain visible.
Ordinary colour writes paint solid pixels. Alpha is optional for effects that need partial transparency.
The same profile supports native drawing, [PixelArray and sprites](pixelarray-sprites.md),
periodic drawing, and [optimized pixel loops](pixel-maps.md).

```python
from lightvm import profile, setting, rgb, init, render
profile("animation")

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
are unsupported; readonly PixelArray artwork, Sprite definitions and static Input
declarations and immutable scalar/colour constants may be declared at module scope.

Explicit [Q16.16 values and FixedArray resources](numeric-types.md) are available
with application feature floor 0.1.0. Convert with `fixed(...)`, `int(...)` or
`float(...)`; mixed fixed/integer/float arithmetic is rejected. Fixed expressions
can execute in eligible bulk kernels with feature floor 0.1.0. Settings/live
inputs retain their existing wire types.

`runtime.time` is the pausable elapsed program time in seconds. `runtime.delta`
is the actual elapsed time supplied for this render, also in seconds.
`runtime.pixel_count` is available during initialization and rendering.
Runtime properties are read-only. `pixels.count` describes the output buffer.
Programs can additionally declare [live inputs](inputs.md) and read their stable
per-render snapshots with `runtime.input(HANDLE)`, including in sprite renders.
Default timed drivers allow a program to run autonomously until a recipe replaces
its input wiring. These controls are distinct from initialization settings.
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

Direct playback displays the animation over black. A composition displays it over the lower layers.
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
preparation fails. `program_state` reports `manifest.settings` and
`settings_values`. The editor's
initialization settings panel applies settings through an explicit restart.
Activation/reset inputs belong to that invocation. For boot playback, save the
settings and input bindings in a preset and select that preset in the output’s
startup policy; omitted settings use the program defaults.

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

`math.fabs`, `math.floor/ceil/trunc`, `math.sin/cos/sqrt/log/exp/pow`, `math.pi`,
and their explicit from-import forms are supported. Rounding functions in `math`
return checked i32; SDK `floor/ceil/trunc` return f32. Domain errors and nonfinite
results fault. See [numeric semantics and feature floors](numeric-types.md).
LightVM
provides `rgb`, `hsv`, `hash01`, `red`, `green`, `blue`, `pico_colorwheel`,
`hue_degrees` and `colorwheel`. Hue degrees uses the original rounded hue; Rainbow
converts it into the colorwheel's 255-unit scale.

The [motion and easing libraries](motion.md) compile bundled scalar functions
into ordinary bytecode. They support explicit imports, named arguments and their
documented defaults. Plain `assert condition` faults the invocation when false.

User helpers accept positional arguments, named arguments and constant defaults.
They are nonrecursive and statically inlined. A value function returns the same
type on every reachable path; a drawing procedure may finish without a value
or use a bare `return`. Scalar arguments are copied; ctx/runtime/pixels arguments retain
access to the caller objects. The subset supports scalar arithmetic, chained comparisons,
conditionals, `range(stop)`, `range(start, stop[, step])`, iteration over managed
number arrays, `while`, break/continue, int/float and two-argument
min/max. Numbers use signed wrapping i32, binary32 floats, explicit Q16.16, bool
and packed RGB8. `fixed` and `FixedArray` provide the explicit fractional type;
mixed arithmetic requires a conversion.
General Python objects, arbitrary imports, exceptions and dynamic allocation are
unsupported. Input, Ramp, Sprite, PixelArray, Transform and ColorTransform are statically
compiled SDK descriptions rather than arbitrary Python objects. Standard-library support is limited to the forms above.

## Package and validation

The 96-byte LBC1/4 header describes typed registers, instructions, functions,
resources, declaration expressions, native calls and settings. It binds init and
render entry IDs; old scheduler fields are reserved zero. Both entries receive
three typed native values: pixel count, time and delta. Context and output storage
are instance bindings. Init may write state and use randomness; render may also
write output. Admission independently validates these rights and signatures.

Application compatibility uses `requires` and the controller’s `version`. The typed
container’s fixed layout marker (LBC1, layout 4) and animation binding 3 describe
how to decode and validate the bytes; neither tracks application releases.
Premultiplied RGBA output is the supported binding. Retired bindings and native
operations remain rejected. Pre-baseline artifacts require source recompilation.

Words 80–92 are reserved zero. There is no drawing API revision selector. Extended native sites support eight arguments in each 16-byte record.

`render_count` counts successful calls including the initial frame. `program_us`
tracks elapsed time; snapshots are passive. The fixed-step statistics and cycle
fields have been removed. FRAME keeps its existing wire range of 0..1000 ms;
autonomous playback supplies its complete elapsed delta without clamping it.

Tests compile the Python examples in `examples/animations/`.
An independent Python caller checks Color, Pattern, and Rainbow output.
Sprite tests check Stars, Rainbow Sprite and Sweep, including transparency,
clocks, movement, retained wipe modes and frame-independent sampled trails.
The suite also checks seeded random ranges, arrays, variable frame rates, frozen settings,
fault publication, and native continuation/ownership.
