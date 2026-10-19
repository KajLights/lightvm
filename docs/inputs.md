# Live inputs and wiring

Programs declare the values they understand. Each running instance selects one
driver per input: a constant, a timed ramp, or a named published signal. Updating
wiring preserves the running VM, its clock, sprites, arrays and random sequence.
Frozen `setting(...)` declarations still control initialization and allocations.

## Try the capability demo

1. Build/install the current firmware and open the matching editor.
2. In Develop, select **eye.py · live inputs**, then **Preview animation**.
3. In **Live inputs**, drag position, width, hue and brightness. Dragging takes
   manual control of that input. The other inputs retain their selected drivers.
4. Restore position's default to resume its two-second eased bounce. The Timer
   controls can change its duration and select Once, Repeat or Bounce.
5. **Use these inputs in draft** copies the selected wiring to the draft's
   **Input recipe**. Save an animation version and use it in a preset to keep it.

For a composition, choose the animation under **Animation to control**, including
layers in A or B. Named transition programs have their own live input panel.
Sweep remains the separate, unchanged example for retained paint and timed trails.

## Authoring

```python
from lightvm import profile, render, Input, Ramp, hsv
profile("animation")

PHASE = Input("phase", default=0.0, minimum=0.0, maximum=1.0,
              unit="turns", driver=Ramp(duration=60.0, mode="repeat"))
BRIGHTNESS = Input("brightness", default=1.0, minimum=0.0, maximum=1.0)

@render
def draw(ctx, runtime, pixels):
    phase = runtime.input(PHASE)
    brightness = runtime.input(BRIGHTNESS)
    for i in range(pixels.count):
        pixels[i] = hsv(phase + i / pixels.count, 1.0, brightness)
```

`Input` is a static module declaration. The default's type determines float,
signed int32, bool or packed RGB8 (`default=rgb(...)`). Numeric bounds are
inclusive. Defaults and bounds are literals; they use the VM's binary32/int32
domains. Names use the existing 1..32-character object-name rules. Optional unit
labels contain up to 16 letters, digits or `_./%-`. Each program has at most eight
inputs, subject to the existing package/JSON and aggregate memory limits.

`runtime.input(HANDLE)` accepts a static declaration and is legal only in render
callbacks. Sprite render callbacks read the containing program's same snapshot,
while retaining their own clocks and local pixel count. To keep a birth-time
value, read it in the animation render and pass that scalar to `spawn(...)`.
Live values cannot determine initialization storage or sprite capacity.

Every invocation captures one snapshot before executing. Pixel loops, native
continuations and sprite callbacks retain it across yields. Publications and
binding changes affect the next invocation. Rendering and source publication do
not allocate; admission and wiring changes reserve bounded storage beforehand.

## Drivers and recipes

Default `Ramp` descriptions apply to float inputs and span 0..1. `duration` is
seconds per traversal, positive and at most 1e9. Modes are `once`, `repeat` and
`pingpong`; easing is `linear` (default) or `smoothstep`. Once holds its target.
An animation-owned timer uses pausable instance time, including explicit FRAME
deltas. A transition-program timer uses monotonic elapsed time since preparation,
independent of A/B animation pause, like native transition timing.

Each input binding selects exactly one of:

```json
{
  "position": {"driver": {"kind": "ramp", "duration": 3, "mode": "pingpong"}},
  "width": {"value": 0.15},
  "hue": {"source": "controls.hue", "timeout_ms": 1000, "fallback": "default"},
  "brightness": {"default": true}
}
```

Omitted inputs retain their program defaults. A ramp binding optionally supplies
`from`, `to`, `easing` and `restart`. Range endpoints default to 0 and 1. Replacing
a ramp or changing its rate preserves the current value where it fits the new
range; bounce rate changes preserve direction. `restart: true` begins at `from`.
`default: true` restores the declared constant or restarts its default timer.
Timers do not restart the animation itself.

Source values must match the input's type and bounds. Numeric mappings allow
explicit range conversion, clamping and inversion:

```json
{
  "width": {
    "source": "controls.size",
    "map": {"input_min": 0, "input_max": 100, "output_min": 0.02, "output_max": 0.4}
  }
}
```

The input range must increase; output endpoints may descend and must fit the
declared consumer domain. Integer mappings round to the nearest integer, with
ties away from zero. No implicit colour or boolean conversion occurs.

An absent or unusable source initially supplies the declared scalar default.
After a valid sample, the last accepted value remains until `timeout_ms` expires
(default 1000; zero disables expiry). Then `fallback: "default"` supplies the
declared scalar; `fallback: "hold"` retains the last accepted value. Recovery
resumes following the source. Invalid samples do not refresh consumer freshness.
Fallback does not silently reenable a default timer.

Pause retains the published frame and timer clock. Sources continue to receive
data. Binding updates do not resume paused or stopped playback; resume captures
current inputs. Explicit FRAME can render while paused, as with other animations.

`inputs` is accepted alongside `settings` in direct activation, transient preview,
reset, composition layers, presets, startup invocations and transition invocations.
Saved recipes contain bindings, never live samples, publisher tokens or timer phase.
Reset retains selected wiring and restarts its timers. Replacing a program uses
the replacement invocation's validated declarations and recipe.

## Management

`describe` reports `live_inputs`, `input_limit=8` and `input_source_limit=16`.
`program_state` adds `inputs_revision`, input snapshots and bindings, and
`program_crc32`. Values describe the most recently captured invocation; a paused
program's snapshot can therefore differ from its newly selected constant binding.

Update one or several bindings atomically:

```json
{"op":"program_inputs","output":"lights","slot":"A","layer":"eye",
 "expected_generation":7,"expected_inputs_revision":2,
 "inputs":{"hue":{"value":0.5},"brightness":{"value":0.6}}}
```

The reply is the updated program state. Both generation and wiring revision must
match. Unknown inputs or any invalid binding reject the entire update. Omit layer
for direct playback. Use `target: "transition"` with `program_state`,
`program_inputs` or `program_snapshot` to address the selected transition program.

Published sources are controller-wide, typed and retained for the current boot.
Claim one bounded group of signal names:

```json
{"op":"input_claim","signals":[{"name":"controls.hue","type":"float"},
                                 {"name":"controls.size","type":"float"}]}
```

The reply supplies `generation` and `boot_id`. Publish every signal in that group
as one atomic batch, using a positive strictly increasing uint32 sequence:

```json
{"op":"input_publish","boot_id":"BOOT_ID","generation":1,"sequence":1,
 "values":{"controls.hue":0.5,"controls.size":75}}
```

Partial, malformed, duplicate, older-generation and previous-boot publications
are rejected. Reclaiming any name revokes its previous publisher group. Claim a
new generation before exhausting the sequence. `input_list` reports names, types,
values, validity, generations, sequences and local receipt ages. Up to 16 source
names are retained until reboot. Sources may be bound before their first claim.
One source can feed several animation instances and transition progress.

`transition_bind` accepts a binding for native progress and requires the current
`expected_assignment`. Values are normalized 0..1; transition state/UI continue
to display percentages. Source loss holds progress by default. Manual progress,
Run, Hold and the existing Fader replace this driver and invalidate its ownership.
Transition-program inputs are captured alongside progress and completed A/B
frames, outside restored guest state. Endpoints still bypass the callback.

Python clients expose `set_inputs`, `claim_inputs` and `publish_inputs`, with
optional activation/upload/reset `inputs`. Hardware acquisition, events,
filtering and buffered history remain separate extensions to this scalar core.
The native [TCA9555/APA102 keypad path](mqtt.md#keypad-and-rgb-indicators) supplies
HA/Matter-style tap, multi-press and hold events to HA and independently controlled RGB indicators. Its
hardware adapters live in `src/digital_inputs.[ch]`; animation input declarations
and the scalar signal registry live in `src/inputs.[ch]`.

## Experimental source changes

Rainbow's old `duration` and `scale` initialization settings are removed. Set
`inputs.phase.driver.duration` and `inputs.scale.value` instead. Recompile its
source, save a new version and recreate affected experimental recipes. No
migration or compatibility aliases are provided. Sweep's API and history remain
unchanged.
