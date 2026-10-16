# Native drawing and scene composition

A scene runs an ordered stack of independent animations on one configured
physical output. Color + Stars provides a conventional twinkle; Rainbow + Stars
uses exactly the same foreground. Replacing the background retains the stars'
state, clock, random sequence and contribution. Up to eight layers are admitted
per scene, subject to the shared VM memory budget.

`examples/layers/` contains Color, Rainbow and Stars. Their `@init` / `@render`
contract and frozen initialization settings match `animation/1`. The opt-in
`profile("layer/1")` compiles to LBC1/4, ABI 4, schema `lightvm.program/6` and an
offscreen `rgba8-premul` output. Programs using PixelArray or user-defined sprites emit schema `lightvm.program/7`.
Older RGB programs retain their existing format
and semantics, and can participate as opaque scene layers. Transparent programs
must be deployed into a scene. Programs receive a destination from the engine;
they cannot choose GPIOs or other layers.

## Drawing and user-defined sprites

The [PixelArray and sprite SDK](pixelarray-sprites.md) provides image formats,
channel operations, absolute alpha, native transforms and function-based sprite
callbacks. `examples/layers/stars.py` is the complete three-pixel star example.

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

Legacy `pixels.fill`, `point`, `gradient`, `colorwheel` and `palette` operations
remain available. Output surfaces use premultiplied RGBA, while mutable
`PixelArray` images use straight channels so changing alpha never destroys their
colour. Drawing converts and composites compatible formats in native kernels.

Existing indexed RGB writes and range fill paint opaque output pixels. Indexed
reads return unpremultiplied RGB from the working layer only. Every layer render
starts transparent black. `fill(rgb(0,0,0))` paints opaque black;
`fill(rgb(0,0,0), alpha=0)` is transparent. Previous frames and lower layers are
not implicitly visible to the guest.

## Blend and publication contract

Layers composite bottom to top over opaque black. RGB values are the existing
encoded 8-bit channel values; blending performs no gamma/linear-light conversion.
For each channel, define `mul(v,a) = (v*a + 127)//255`. Straight input color is
premultiplied at paint time. Given completed layer `src`, lower result `dst`, and
layer opacity `o`, source-over is:

```text
a = mul(src.alpha, o)
result.channel = mul(src.channel, o) + mul(dst.channel, 255-a)
```

Global layer opacity applies once to the completed layer, after overlapping
points/sprites have blended within it. Alpha zero reveals the moving background;
opaque black covers it. The compositor emits RGB8; physical brightness limiting
and wire order run once, after composition. Source-over is the supported blend
mode in v1. Additional blend operators and linear-light blending remain future
extensions.

Each layer owns its context, settings, RNG, program clock, continuation and
working/committed frames. Native kernels process at most 32 pixels or scan 8 instance slots
per dispatch; the compositor processes 32 output pixels per slice. A compositor
pass pins the committed contributions by deferring autonomous layer updates until
that pass finishes. Management mutations invalidate an unfinished pass before
and after changing a layer. A slow layer contributes its latest completed frame
while other layers continue. A fault retains that contribution and stops only
the faulting instance. Context mutations before a fault are not rolled back.

One scene presenter owns the physical output. Its front RGB buffer remains
immutable until the driver completes or cancels transfer. Composition can prepare
a back buffer meanwhile. Snapshots report the last successfully presented RGB
frame; transfer failures retain it and retry without faulting layer programs.
Layer replacement and full scene replacement prepare candidates before swapping;
failure retains the current scene. Peak memory admission includes the candidate.

Dense frame storage costs `8*N` bytes per RGBA layer (working + committed), plus
`9*N` per scene (front, back, last presented). Two RGBA layers therefore use
`25*N` frame bytes: 5,000 at 200 LEDs. Add VM/context/code-cache metadata and
native driver storage. A generic sprite batch adds an 8-byte header and aligned instance records
containing native clocks, an active flag and four bytes per scalar field. Each
batch reuses one local RGBA canvas; immutable artwork stays in its package. All VM allocations and the
scene's physical snapshot storage count toward the shared VM heap ceiling.
Candidate preparation temporarily retains both old and new allocations.

## Using the client

Compile and store the three layer examples:

```sh
python -m lightvm.compiler examples/layers/color.py -o build/color.lbc
python -m lightvm.compiler examples/layers/rainbow.py -o build/rainbow.lbc
python -m lightvm.compiler examples/layers/stars.py -o build/stars.lbc
python tools/client.py --port PORT --store-procedure native_color build/color.lbc
python tools/client.py --port PORT --store-procedure native_rainbow build/rainbow.lbc
python tools/client.py --port PORT --store-procedure native_stars build/stars.lbc
python tools/client.py --port PORT --device lights --scene examples/layers/rainbow-stars.json
python tools/client.py --port PORT --device lights --get-scene
python tools/client.py --port PORT --device lights --layer stars --parameters
python tools/client.py --port PORT --device lights --layer stars --pause
python tools/client.py --port PORT --device lights --layer bg --activate native_color
python tools/client.py --port PORT --device lights --layer stars --resume
python tools/client.py --port PORT --device lights --layer stars --layer-opacity 128
python tools/client.py --port PORT --device lights --scene-snapshot
```

The Python `Client.configure_scene(layers, device=...)` stages a
`lightvm.scene/1` document then applies it atomically. `activate(..., layer=...)`
replaces one layer. `reset_program(settings=..., layer=...)` creates a new
instance of that layer's program; frozen settings require this explicit restart.
`pause_program` retains the contribution and excludes paused time; resume retains
state. Layer `STOP` clears its contribution and suspends it; `RESET` unloads it.
`remove_layer` releases its resources. Root PLAY/STOP controls the entire scene;
root STOP clears physical output while retaining layer state and desired play
flags. Root FRAME advances all layers by its explicit delta and composites once.
Root RESET or `clear_scene` releases the scene. Normal root procedure activation
replaces a scene with the original single-program presentation path.

`snapshot()` returns final RGB; `snapshot(layer=...)` returns that layer's
committed RGBA (or RGB for legacy programs). `scene()` reports ordering,
generations, opacity, physical frames, composite frames, cumulative compositor
CPU time and output latency. Per-layer `program_parameters()` includes completed
renders, cumulative render CPU, native draw CPU, dispatch and resume counts.
`native_draw_us` measures SDK bindings 18–51; existing bulk fill/copy
instructions contribute to total render CPU only.
Render CPU includes native kernels and excludes time spent servicing peers;
output latency includes asynchronous transfer wait. These counters have distinct
denominators: a scene may reuse a layer contribution for several outputs.

## Management and compatibility

Discovery advertises `scene_api=1`, `native_drawing_api=2`,
`scene_layer_limit=8`, `layer_pixel_format="rgba8-premul"`, `alpha=true`,
`composition=true`, schemas 6/7 and `layer/1`. Target-aware compilation checks both
profile and schema; old targets reject the new packages. Existing schema 5 RGB
packages retain their ABI and pixel behavior.

The object upload protocol accepts kind `scene`. Operations are `scene_get`,
`scene_snapshot`, `scene_apply`, `scene_clear`, `scene_layer_activate`,
`scene_layer_opacity` and `scene_layer_remove`. Writes require
`expected_revision`; activation additionally requires `expected_generation`.
The `layer` selector extends existing program controls and `device_command`.
Full-scene writes replace every instance; use layer activation to retain peers.

Legacy native binding IDs 18–26 are respectively RGBA fill, RGBA point, colorwheel fill,
sprite advance, sprite spawn, sprite draw, sprite active count, gradient fill and
palette fill. Resource kind 12 is premultiplied RGBA; kind 13 is the bounded sprite
pool, retained for stored schema 6 packages. New source uses generic batch kind
18 and PixelArray kinds 14–17; see the [SDK reference](pixelarray-sprites.md).
Typed calls enforce stage permissions, resource kind, bounds and generation.
There are no guest pointers or per-frame allocations.

Scenes are RAM-only in v1. Saved hardware configuration and its single-procedure
startup rules are unchanged; reboot restores that saved startup. The browser
editor has no scene authoring interface yet. Persistent scenes, new blend modes, two-dimensional rotation and A/B
transitions remain follow-on work.

The [Pico W measurement](design/measurement-native-composition-pico-w.md) records
the initial four-output throughput, CPU and memory checks.
