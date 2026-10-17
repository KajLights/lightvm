# Python transition profile

Transitions are named Python programs compiled and stored exactly like animations.
Their manifest uses `schema: "lightvm.program"`, `profile: "transition"`, the
existing `requires` release declaration and `pixels.format: "rgba8-premul"`.
There is one shared interpreter and flash procedure catalogue. Transition names
come from that catalogue, rather than a native list of recognized effect names.

```python
from lightvm import profile, render
from lightvm.transitions import crossfade

profile("transition")

@render
def draw(ctx, runtime, pixels):
    crossfade(pixels, runtime.progress)
```

The [crossfade](../examples/transitions/crossfade.py) and
[wipe](../examples/transitions/wipe.py) examples are the canonical sources.
The wipe exposes a frozen boolean `reverse` setting through `@init`, using the
same initialization and settings conventions as animations.

## Inputs and evaluation

`@init(ctx, runtime)` is optional; `@render(ctx, runtime, pixels)` is required.
`runtime.pixel_count`, `pixels.count`, context, managed arrays, ordinary drawing
and frozen `setting(...)` declarations follow the animation conventions.
`runtime.progress` is a read-only float in **0..1**. Transition programs have no
`runtime.time` or `runtime.delta`: the native controller independently drives the
public percentage in 0..100. Progress is zero during preparation.

Each evaluation starts with transparent working pixels. The owner captures the
percentage and both completed RGB source frames, restores the prepared baseline
(after init and its initial zero-progress render), resource data and RNG state,
and runs one callback. Backwards movement and seeking do not replay earlier
percentages. Identical inputs, settings and seed give identical results.

At exactly 0% and 100%, the owner copies A or B directly and bypasses the callback.
An empty scene supplies black. Transition RGB is displayed over black using the
same premultiplied representation as ordinary animation output. A fault retains
the last completed physical frame; selecting a fresh program recovers.

The program cannot mutate its input scene frames or drive a physical output.
Animations cannot use transition scene operations, and transition packages cannot
be activated as animations, scene layers or startup animation procedures.

## Library and optimized compilation

`lightvm.transitions` is a bundled Python library, parsed and statically inlined
like `lightvm.motion` and `lightvm.easing`. Its helpers accept named arguments:

- `crossfade(pixels, progress)` blends encoded RGB8 with complementary weights.
- `wipe(pixels, progress, reverse=False)` reveals B from the start or end. The
  number of revealed pixels is `int(progress * pixels.count)`.

Their underlying operations are also available inside a transition render:

- `pixels.mix(progress, start=0, stop=pixels.count)` replaces a range with a blend
  of A and B. Progress must be finite and in 0..1.
- `pixels.copy_scene("A" or "B", start=0, stop=pixels.count)` copies a source range.

Ranges are checked, half-open and use the same indices in source and destination.
Both operations lower directly to bounded native calls; no per-pixel guest loop
or temporary mask is needed. Layers, transitions, PixelArray drawing and sprites
share the compositing primitives in `src/draw.c`. The compositor specializes
opaque RGB spans while using the same channel sum as premultiplied alpha-over.
Crossfade uses complementary Q16 integer weights and
one rounded sum per RGB channel. Endpoints copy directly and identical colors
stay identical. There is no implicit white paint or black-dimming stage. Wipe's
direction and boundary calculation remain Python code, with native range copies.

The shared LBC1/4 container binds this profile with ID 4 and two typed runtime
arguments (pixel count and progress); animation profile ID 3 remains unchanged.
Native bindings 53 and 54 provide scene blending and scene range copies. Each
dispatch processes at most 32 pixels and resumes through the existing VM budget.
All buffers and initialized-state snapshots are admitted before activation;
rendering allocates no memory.

## Store and recall

```sh
python -m lightvm.compiler examples/transitions/crossfade.py -o build/examples/crossfade.lbc
python tools/client.py --port PORT --store-procedure crossfade build/examples/crossfade.lbc
python tools/client.py --port PORT --transition crossfade --device lights

python -m lightvm.compiler examples/transitions/wipe.py -o build/examples/wipe.lbc
python tools/client.py --port PORT --store-procedure wipe build/examples/wipe.lbc
python tools/client.py --port PORT --transition wipe --settings '{"reverse": true}' --device lights
```

Or call `client.save_procedure(name, compiled.bytecode)` then
`client.select_transition(name, settings={...}, device="lights")`.
The wire request is `transition_select` with `procedure`, `expected_revision`,
optional `settings` and `seed`. Preparation is transactional and preserves the
selected program and progress driver on failure. State reports `procedure` and
`program_crc32`; replacing a flash name leaves its currently running immutable
version pinned until selected again. Referenced programs cannot be deleted.

`procedure_list` includes `profile: "animation"` or `"transition"` for supported
packages. Firmware advertises `transition_programs: true`. The browser compiles
these same examples, saves editable versions in its library, saves and selects
named device programs, and lists stored transitions in the A/B selector. Program
storage has eight shared slots for animations and transitions.

Stored packages survive reboot. Live A/B scenes, transition selection and progress
drivers remain RAM state, as described in [A/B transitions](transitions.md).
