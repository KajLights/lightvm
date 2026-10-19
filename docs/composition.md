# Native drawing and composition

A composition runs an ordered stack of independent animations on one configured
physical output. Color + Stars provides a conventional twinkle; Rainbow + Stars
uses exactly the same foreground. Replacing the background retains the stars'
state, clock, random sequence and contribution. Up to eight layers are admitted
per composition, subject to the shared VM memory budget.

All animations use `profile("animation")`. The single `examples/animations/`
directory supplies examples for both direct playback and composition.
Programs use `lightvm.program`, a minimum LightVM release, and `rgba8-premul` output.
Every animation can run directly on a controller or inside a composition.
Direct playback shows the animation over black. A composition combines it with lower layers.
Programs receive a destination from the engine. They cannot choose GPIOs or other layers.
Stored RGB programs retain their original behavior and can act as opaque composition layers.

## Drawing and user-defined sprites

The [PixelArray and sprite SDK](pixelarray-sprites.md) provides image formats,
channel operations, absolute alpha, native transforms and function-based sprite
callbacks. `examples/animations/stars.py` is the complete three-pixel star example.

```python
# Star is a Sprite(width=3, init=star_init, render=star_render) definition.
# In @init:
ctx.stars = SpriteBatch(Star, capacity=20)

# In @render, when the animation chooses to create a star:
ctx.stars.spawn(position=position, duration=duration, color=color)
ctx.stars.render(runtime, pixels)
```

The animation owns births, positions, colour, movement, lifetime and fading.
The batch reserves bounded slots, runs each user initializer at spawn, and runs
active user render callbacks in slot order. Callbacks compile to bytecode and
participate in the VM's normal instruction budget and continuation. C implements
generic instance storage, pixel sampling, colour transforms and blending; it has
no star policy. There are no per-frame heap allocations.

`pixels.fill`, `point`, `gradient`, `colorwheel` and `palette` operations
remain available. Output surfaces use premultiplied RGBA, while mutable
`PixelArray` images use straight channels so changing alpha never destroys their
colour. Drawing converts and composites compatible formats in native kernels.

Existing indexed RGB writes and range fill paint opaque output pixels. Indexed
reads return unpremultiplied RGB from the working layer only. Every layer render
starts transparent black. `clear()` removes paint. `fill(rgb(0,0,0))` paints opaque black.
Ordinary drawing needs no alpha argument. Effects can specify alpha when partial transparency is necessary.
Previous frames and lower layers are
not implicitly visible to the guest.

## Blend and publication contract

Layers composite bottom to top over opaque black. RGB values are the existing
encoded 8-bit channel values; blending performs no gamma/linear-light conversion.
Straight input color is premultiplied at paint time using
`mul(v,a) = (v*a + 127)//255`. Layers, transitions, PixelArray drawing and sprites
then use the shared native compositor in `src/draw.c`. For a completed layer
`src`, lower result `dst` and 8-bit layer opacity `o`, source-over is:

```text
w = round(o * 65536 / 255) / 65536
result.channel = round(src.channel * w + dst.channel * (1 - src.alpha/255 * w))
```

The implementation uses integer arithmetic and rounds the combined sum once.
It retains pixel alpha's full `/255` precision through that final division.
Opaque sources use complementary Q16 weights, exactly as Python crossfade does;
an opaque layer at opacity `o` equals a crossfade at progress `o/255`. This can
change a channel by one level compared with the former separately rounded terms.
Transparent and fully opaque endpoints have fast paths. Composition composition uses
bounded spans of at most 32 pixels, as do transition drawing operations.

Global layer opacity applies once to the completed layer, after overlapping
points/sprites have blended within it. Alpha zero reveals the moving background;
opaque black covers it. The compositor emits RGB8; physical brightness limiting
and wire order run once, after composition. Each composition layer accepts optional
`blend`: `over` (default), `add`, `screen`, or `multiply`. It applies to the
completed layer, after its sprites have combined. Modes operate on the same
encoded 8-bit values; there is no gamma/linear-light conversion.

The other blend modes keep their existing 8-bit paint and effective-alpha
rounding. For opaque destination channel `d`, premultiplied source channel
`p = mul(src.channel, opacity)` and effective alpha `a = mul(src.alpha, opacity)`:

| Mode | Result channel |
| --- | --- |
| `over` | Single-rounded source-over sum above. |
| `add` | `min(255, d+p)` |
| `screen` | `255 - mul(255-d, 255-p)` |
| `multiply` | `mul(d, 255-a+p)` |

Zero alpha or opacity leaves the lower composition unchanged in every mode. Multiply
is useful over an existing background; its result over black remains black.
Group blending and individually blended eyes can produce different results.

Each layer owns its context, settings, RNG, program clock, continuation and
working/committed frames. Native kernels process at most 32 pixels or scan 8 instance slots
per dispatch; the compositor processes 32 output pixels per slice. A compositor
pass pins the committed contributions by deferring autonomous layer updates until
that pass finishes. Management mutations invalidate an unfinished pass before
and after changing a layer. A slow layer contributes its latest completed frame
while other layers continue. A fault retains that contribution and stops only
the faulting instance. Context mutations before a fault are not rolled back.

One composition presenter owns the physical output. Its front RGB buffer remains
immutable until the driver completes or cancels transfer. Composition can prepare
a back buffer meanwhile. Snapshots report the last successfully presented RGB
frame; transfer failures retain it and retry without faulting layer programs.
Layer replacement and full composition replacement prepare candidates before swapping;
failure retains the current composition. Peak memory admission includes the candidate.

Dense frame storage costs `8*N` bytes per RGBA layer (working + committed), plus
`9*N` per composition (front, back, last presented). Two RGBA layers therefore use
`25*N` frame bytes: 5,000 at 200 LEDs. Add VM/context/code-cache metadata and
native driver storage. A generic sprite batch adds an 8-byte header and aligned instance records
containing native clocks, an active flag and four bytes per scalar field. Each
batch reuses one local RGBA canvas; immutable artwork stays in its package. All VM allocations and the
composition's physical snapshot storage count toward the shared VM heap ceiling.
Candidate preparation temporarily retains both old and new allocations.
Use a [Sweep](motion.md#shared-sweep-animation) batch for inexpensive eyes sharing
one blend mode. Separate single-eye layers provide independent controls and
blending, at the cost of another `8*N` frame bytes and a VM context per layer.

## Using the client

Compile and store Color, Rainbow, and Stars from the shared animation directory:

```sh
python -m lightvm.compiler examples/animations/color.py -o build/color.lbc
python -m lightvm.compiler examples/animations/rainbow.py -o build/rainbow.lbc
python -m lightvm.compiler examples/animations/stars.py -o build/stars.lbc
python tools/client.py --port PORT --store-program color build/color.lbc
python tools/client.py --port PORT --store-program rainbow build/rainbow.lbc
python tools/client.py --port PORT --store-program stars build/stars.lbc
python tools/client.py --port PORT --output lights --composition examples/compositions/rainbow-stars.json
python tools/client.py --port PORT --output lights --get-composition
python tools/client.py --port PORT --output lights --layer stars --program-state
python tools/client.py --port PORT --output lights --layer stars --pause
python tools/client.py --port PORT --output lights --layer bg --activate color
python tools/client.py --port PORT --output lights --layer stars --resume
python tools/client.py --port PORT --output lights --layer stars --layer-opacity 128
python tools/client.py --port PORT --output lights --layer stars --layer-blend screen
python tools/client.py --port PORT --output lights --composition-snapshot
```

The Python `Client.configure_composition(layers, output=...)` stages a
`lightvm.composition` document then applies it atomically. `activate(..., layer=...)`
replaces one layer. `reset_program(settings=..., layer=...)` creates a new
instance of that layer's program; frozen settings require this explicit restart.
`pause_program` retains the contribution and excludes paused time; resume retains
state. Layer `STOP` clears its contribution and suspends it; `RESET` unloads it.
`Client.layer_blend(layer, mode, output=...)` changes the blend without restarting
the animation or its peers. Program replacement retains layer opacity and blend.
`remove_layer` releases its resources. Root PLAY/STOP controls the entire composition;
root STOP clears physical output while retaining layer state and desired play
flags. Root FRAME advances all layers by its explicit delta and composites once.
Root RESET or `clear_composition` releases the composition. Normal root program activation
replaces a composition with the original single-program presentation path.

`snapshot()` returns final RGB; `snapshot(layer=...)` returns that layer's
committed RGBA. `composition()` reports ordering,
generations, opacity, physical frames, composite frames, cumulative compositor
CPU time and output latency. Per-layer `program_state()` includes completed
renders, cumulative render CPU, native draw CPU, dispatch and resume counts.
`native_draw_us` measures SDK bindings 27–54 and compiler-generated pixel maps; existing bulk fill/copy
instructions contribute to total render CPU only.
Render CPU includes native kernels and excludes time spent servicing peers;
output latency includes asynchronous transfer wait. These counters have distinct
denominators: a composition may reuse a layer contribution for several outputs.

## Management and compatibility

Discovery reports the application `version`, `compositions=true`, `composition_layer_limit=8`,
`layer_pixel_format="rgba8-premul"`, `alpha=true`, and `composition=true`.
`composition_blend_modes` lists `over`, `add`, `screen` and `multiply`; omitted `blend`
means `over`. Compatibility uses the program's minimum application release and
actual resource limits. See [versioning](versioning.md).

The object upload protocol accepts kind `composition`. Operations are `composition_get`,
`composition_snapshot`, `composition_apply`, `composition_clear`, `composition_layer_activate`,
`composition_layer_opacity`, `composition_layer_blend` and `composition_layer_remove`. Writes require
`expected_revision`; activation additionally requires `expected_generation`.
The `layer` selector extends existing program controls and `output_command`.
Full-composition writes replace every instance; use layer activation to retain peers.

Resource kind 12 is premultiplied RGBA. SpriteBatch uses generic batch kind 18;
PixelArray uses kinds 14–17. Source point/fill/gradient/colorwheel/palette methods
use generic PixelArray kernels. Removed resource kind 13 and native bindings
18–26 are rejected even inside a package claiming the current schema.
Typed calls enforce stage permissions, resource kind, bounds and generation.
There are no guest pointers or per-frame allocations.

Running compositions remain in RAM. The browser Configuration tab authors compositions and saves them
as **presets** on the controller. A preset has one or more
layers, so standalone and composite effects use the same recipe format. Saving
does not change playback; activation prepares the whole candidate before swapping.
Saved hardware configuration selects off, restore-last or a preset at startup.
Preset selection, ON, OFF and startup all use the preset’s saved transition.

Controller presets use `lightvm.preset` with a `layers` array and optional
`entry_transition` invocation (program, CRC32, settings, duration and optional seed;
this one invocation is used for selection, ON, OFF to black and startup; see
[recall transitions](transitions.md#preset-recalls-and-power)), using the same layer fields
as a composition. Each layer additionally requires `crc32`, matching the stored program.
Composition documents may also supply `crc32` to check the expected version. The editor
deploys programs under user-selected names; source remains in external files.
Preset layers pin each deployed program by name and CRC32. Multiple presets/layers share
one stored program; active instances retain independent settings and state.

Firmware advertises `presets: true`. Presets share available storage with compiled
programs; neither has a fixed count quota. Each document is limited to `config_limit`
bytes and the JSON parser's token bound. Public preset names allow 1–32 letters,
digits, dots, underscores or hyphens. They use their own typed namespace and do not
appear in `program_list`. See [internal storage](storage.md).

| Operation | Fields / behavior |
| --- | --- |
| `preset_list` | Returns `{name,size,crc32}` entries. |
| `preset_get` | `name`; returns the recipe JSON. |
| `preset_save` | Stage object kind `preset` with `name`, then supply `name`, `output`, `expected_revision` (catalog). Prepares against that output, then saves atomically. |
| `preset_activate` | `name`, `output`, optional `slot` (A by default), `expected_revision` (composition); prepare only that slot, retaining progress and driver. |
| `preset_recall` | `name`, `output`, `expected_revision` (composition); complete the previous recall and transition A to B using the destination entry transition. |
| `preset_delete` | `name`, `expected_revision` (catalog); running instances continue. |

Saved references prevent deleting or replacing the referenced program version.
Delete the recipes that use it before removing unused code. An identical program
can be saved again under the same name. Invalid recipes, stale revisions, full
storage and failed memory admission preserve existing saved recipes and playback.
There is no per-preset frame or VM allocation while a recipe is idle in flash.
The Python client exposes `presets`, `preset`, `save_preset` (optional `entry_transition`), `activate_preset`,
`recall_preset`, `set_power`, and `delete_preset` helpers.

Compositions can also supply independent A/B slots to the [transition owner](transitions.md).
Linear-light blending and two-dimensional rotation remain follow-on work.
