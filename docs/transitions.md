# A/B compositions and transitions

Each output can retain two independently layered compositions, A and B. A transition
combines their latest completed RGB frames at an absolute progress percentage.
0% gives A exactly; 100% gives B exactly. An empty slot contributes black.
Endpoints keep both slots, generations, layer settings, clocks and RNG state.
Hidden compositions keep animating and can be prepared or replaced independently.

A single composition continues to use its existing presenter until B or a transition
control is used. Enabling A/B retains the running A instances. A direct program
becomes A's `main` layer with its current state. Subsequent named activation in
A or B replaces only that composition. Transient preview uploads retain their existing
whole-output replacement behavior. A/B state is held in RAM; startup uses the
output’s saved off, restore-last or preset policy.

## Preset recalls and power

Manual A/B controls keep fixed slots, including timed `transition_run` movements.
Reaching an endpoint never swaps or unloads either composition. `preset_activate`
loads a named recipe into a slot (A by default) without moving progress or selecting
its entry transition. `transition_swap` explicitly exchanges the instances and
complements progress at an endpoint; it preserves clocks, settings and RNG state.
It rejects intermediate progress, where a directional effect need not preserve
its image under a swap.

`preset_recall` is a separate output operation. It prepares a preset and its
optional `entry_transition`, then always runs from A (outgoing) to B (incoming),
0 to 100. If another recall arrives, the previous destination becomes full
immediately, moves into A without restarting, and the new destination is placed
in B. A held manual mix has no recall target; takeover completes toward B when
progress is above zero, or retains A at zero. No snapshot or queue is used.
A manual STOP followed by recall starts from black. Directional transition
settings therefore have the same meaning for every recall. At completion the
slots remain in place until the next recall or explicit swap.

Preset documents accept this optional invocation, with a pinned transition program
version and frozen initialization settings:

```json
"entry_transition": {
  "program": "wipe",
  "crc32": 123456789,
  "settings": {"reverse": false},
  "duration_ms": 1500
}
```

Use the real CRC32 from `program_list` with `type: "transition"`. `program`,
`crc32` and finite, nonnegative `duration_ms` are required. `settings` and unsigned
`seed` are optional. Omission means an immediate cut; duration zero also cuts.
Saved presets protect these transition program versions from replacement and
deletion, independently of the animation namespace.

Each preset owns one transition, configured with the preset. Selection, ON and
startup use the incoming preset's `entry_transition`. OFF uses the selected
preset's same invocation to transition from the current composition to black;
its program, version, settings, seed and duration stay the same. Every transition
runs forward from 0 to 100. There are no output-level on/off overrides. A preset
without a transition cuts immediately for all these operations.

`output_power` with `action: "off"` recalls black and remembers the selected preset.
`action: "on"` recalls optional `name`, otherwise the remembered preset, then the
output's HA default when no selection is known. HA supplies its exposed selection
or default explicitly. OFF without an identifiable preset cuts to black.
Exactly one transition runs for each operation.

Both recall operations require `expected_revision` matching the output's shared
composition revision, and accept `output`. They admit the whole candidate and
transition before committing. Invalid references, settings, resource admission or
stale revisions preserve the old playback and driver. Repeating the same target
is a no-op. A recall revokes old source assignment tokens. Manual transition and
composition controls release recall ownership without enabling automatic swaps.

`describe.outputs[].power` is logical destination power, separate from `playing`:
the presenter continues rendering a fade to black. `selected_preset` remembers
the accepted selection, including while off; `preset` remains the identity of the
actual endpoint composition and is null for a mix or black. `transition_get.recall`
is `{power,preset}` while recall owns the output, otherwise null. Selection and
power targets are persisted on writable controllers; reboot applies the saved
[startup policy](configuration.md#json-configuration-v1).

The Python client provides `recall_preset`, `set_power` and `swap_transition`.
The CLI exposes `--recall-preset NAME`, `--power on|off` and `--swap-transition`.
The editor provides **Preset transition** settings in the preset composer and
**Controller preset transition** for an existing stored preset. **Run on output** and
HA use recalls; the Mixer buses keep fixed manual slots.

## Progress and effects

Progress is a finite number from 0 to 100. Setting it selects that percentage
immediately, including backwards movement or jumps. It never replays intermediate
percentages. The native owner captures one percentage for an evaluation, pins
completed composition frames during mixing, and publishes only the completed result.
Continuous updates mark the next evaluation dirty without cancelling an evaluation
already in progress. Snapshots and the physical driver retain complete frames.

Crossfade uses encoded RGB8 values, with complementary weights and one rounding:
`round(A * (1-p) + B * p)`, where `p = progress / 100`. Identical inputs remain
identical. Brightness and wire order apply once to the final physical output.

The supplied [Python crossfade and wipe programs](transition-profile.md) are
compiled and stored under ordinary program names. Select them with
`transition_select` and `program`, just as compositions recall stored animations.
The following firmware effect table is retained for compatibility with the
earlier A/B controls; new code should select stored Python programs.

| Legacy effect | Wire name | Appearance |
| --- | --- | --- |
| Crossfade | `crossfade` | Complementary A/B weights. |
| Wipe | `wipe` | B reveals pixels from the start of the strip. |
| Soft wipe | `soft_wipe` | A moving boundary with a gradual edge. |
| Dissolve | `dissolve` | Fixed per-pixel thresholds from pixel index and seed. |
| Dip through black | `dip_black` | A fades to black at 50%, then B fades in. |
| White flash | `white_flash` | White paint peaks at 50%; B replaces A underneath. |
| Custom | `custom` | Optional reveal-mask and RGBA-paint programs. |

Every effect bypasses masks and paint at the endpoints. Geometry is the configured
one-dimensional pixel order. Effects evaluate directly from progress and seed.

## Drivers

There is one active driver: manual, timed, or source. A manual set removes the
assignment. Hold evaluates a timed driver at the current monotonic instant,
removes the assignment and keeps the resulting percentage. Neither operation
pauses a composition.

A timed movement starts at the current progress and linearly interpolates to the
target over `duration_ms`, using the native monotonic clock. The target and
nonnegative finite duration are validated before replacing a driver. Zero duration
sets the target immediately. Completion sets the target exactly and leaves the
driver manual. Runs continue when the client disconnects, independently of guest
FRAME deltas or composition pause state.

An optional `interval_ms` repeats alternating movements between the starting
percentage and target. Each fade starts at that interval and holds its endpoint
for the remainder. The interval must be finite and at least the fade duration;
zero retains the one-shot behavior. For example, a 2,000 ms duration and 10,000 ms
interval give a two-second crossfade followed by eight seconds at the endpoint,
then the reverse fade. The controller calculates the current leg directly after
any scheduling gap. Manual set, Hold or another driver cancels repetition.

A source assignment returns an `assignment` token. Each adapter update supplies
that token, the source name and a percentage. Reassignment, including reassignment
to the same source name, issues a fresh token. Old updates cannot change progress.
Missing, nonnumeric, nonfinite and out-of-range values retain current progress;
malformed JSON is rejected before dispatch. Values update current state with no
percentage queue. Adapters convert sensor/message units before sending updates.
The management transport is an available adapter; ADC, MQTT and a general input
registry are not prerequisites.

## Management API

Discovery advertises `slots: ["A", "B"]`, `ab_transitions`, `transition_sources`,
`transition_programs`, `custom_transitions` and the legacy `transition_effects`.
Transition packages use `lightvm.program` with `profile: "transition"`, through
the same compiler, checked interpreter and release contract as animations.
`program_list` reports each current package's `type` and `profile`, and accepts
a `type` filter. Animation and transition names are unique within their own types;
both can be called `wipe`. Transition selection resolves the transition namespace.
See [internal storage](storage.md).

All operations accept `output`. Transition controls address the output owner,
without a slot selector. Composition and layer operations accept `slot`, defaulting to A.
Composition revisions are shared by A and B, including transition-effect selection.
Preparation failures and stale revisions preserve both compositions and the active driver.

| Operation | Fields / response |
| --- | --- |
| `transition_swap` | `expected_revision`; swap A/B and complement progress at an endpoint, release the driver. |
| `preset_recall` | `name`, `expected_revision`; prepare the destination and its entry transition, run A to B. |
| `output_power` | `action: "on"` or `"off"`, `expected_revision`, optional on `name`; recall preset or black. |
| `transition_get` | State with A, B, program, pinned program CRC32, progress, driver, counters and errors. |
| `transition_set` | `progress`; releases the driver. |
| `transition_run` | `target`, `duration_ms`, optional `interval_ms`; replaces the driver after validation. |
| `transition_assign` | `source`; returns `driver.assignment`. |
| `transition_update` | `source`, `assignment`, `progress`; status value 1 if accepted, 0 if ignored. |
| `transition_hold` | Releases the driver and retains current progress. |
| `transition_select` | `program`, `expected_revision`, optional initialization `settings` and unsigned `seed`. Legacy alternative: `effect` with optional `mask`/`paint`. |
| `composition_apply` / `preset_activate` | Existing operations plus optional `slot`. |
| `composition_pause` / `composition_resume` | `slot`, `expected_revision`; retain composition contributions and state. |
| `composition_clear` | `slot`, `expected_revision`; unloads just that slot in A/B mode. |
| `composition_snapshot` | Default: last physical RGB. Explicit `slot`: that slot's completed RGB in A/B mode. |

Transition writes return refreshed state, except source updates. Structural writes
use `expected_revision`; progress updates do not require a revision. Source tokens
are unsigned 32-bit counters, are not reused within a boot, and never wrap; new assignments are rejected on
exhaustion. Hold remains available.

Root PLAY, STOP, FRAME and RESET control the whole A/B output. STOP retains composition
state and clears the physical lights. FRAME advances both compositions by its explicit
delta, evaluates current controller progress and presents one complete mixed frame.
RESET unloads both compositions and the transition. Existing program controls and layer
commands accept both A/B slot selectors. Commands addressed to B alone affect B;
use `composition_pause` / `composition_resume` for either whole composition while retaining its frame.

```python
client.configure_composition(composition_a_layers, output="lights", slot="A")
client.configure_composition(composition_b_layers, output="lights", slot="B")
client.select_transition("crossfade", output="lights")  # stored Python package
# Or: client.select_transition("wipe", settings={"reverse": True}, output="lights")
client.set_progress(35, output="lights")
client.run_transition(100, duration_ms=2000, output="lights")
client.hold_transition(output="lights")
state = client.assign_transition_source("sensor", output="lights")
client.update_transition_source("sensor", state["driver"]["assignment"], 42,
                                output="lights")
```

The CLI supports `--slot B --composition FILE`, `--get-transition`, `--progress 35`,
`--run-to 100 --duration-ms 2000` (add `--interval-ms 10000` to alternate), `--hold-transition`, `--transition wipe --settings '{"reverse": true}'`,
`--assign-source NAME`, and `--source-value P --source-name NAME --assignment TOKEN`.
The browser's **Mixer** loads saved controller presets into either slot, selects a
stored transition program, cuts to endpoints, swaps A/B at endpoints and starts
timed movement or a repeating loop. Its crossfader supplies continuous external
input through the selected USB, HTTP or WebSocket connection, with explicit takeover
and Hold. See [Mixer controls](web-editor.md#mixer).

## Legacy custom mask and paint programs

Custom transitions reuse ordinary `profile("animation")` packages and the shared
interpreter. `mask` and `paint` each accept an object containing `program` and
optional initialization `settings`. Mask red values supply B's reveal weight in
0..255; the remaining mask channels are ignored. Omitting the mask gives the
normal crossfade. Paint is premultiplied RGBA, composited source-over the mixed
RGB using the existing drawing rules. Omitting paint leaves the mix unchanged.

For these evaluations, `runtime.time` and `runtime.delta` contain normalized
progress in 0..1, rather than elapsed wall time. The custom program is not an
animation clock. Each evaluation restores its admitted initialized state (after
its first zero-progress render), resource data and RNG state, then invokes one
render at the captured percentage. Context changes do not accumulate across
percentages. Default RNG seeding, including `random.seed()`, uses the selected
transition seed; explicit guest seeds retain their ordinary behavior. The same
program, settings, seed, geometry and percentage produce the same output.

For example, compile and store this as `mask`:

```python
from lightvm import profile, render, rgb
profile("animation")

@render
def reveal(ctx, runtime, pixels):
    pixels.fill(rgb(int(runtime.time * 255.0), 0, 0))
```

Then select it with
`client.select_transition_effect("custom", mask={"program": "mask"}, output="lights")`.
Custom programs cannot read composition frames or drive physical outputs. A custom fault
retains the last completed physical output; selecting a fresh effect recovers.

## Ownership and memory

The native owner uses 32-pixel mixing slices, 64-instruction cooperative custom
program slices and 256-byte state-restore slices. Execution and mixing can yield
while the composition engines continue advancing. Only the completed front buffers
are pinned: composition rendering and back-buffer composition continue until the back
buffer is ready. A ready back buffer waits for publication before starting more
layer renders; animation clocks retain the actual elapsed time. Python programs
release composition fronts as soon as their RGBA result is committed, allowing source
publication during final RGB conversion. State checkpoints contain mutable values
and owned payload, excluding immutable resource descriptors. Completed restore
and evaluation phases advance within the same bounded scheduler visit.
Python transitions coalesce progress
updates to the presentation cadence. A pending physical transfer
retains its front buffer while another completed result is prepared. Transfer
failure keeps the last successfully presented snapshot and retries.

Each offscreen composition needs `6*N` RGB bytes for working and completed composition,
plus `8*N` per RGBA layer and its VM resources. The transition needs `9*N` for its
working, front and physical snapshot storage. Thus two single-layer compositions need
`37*N` frame bytes before VM, code-cache and native-driver storage. A promoted
direct instance retains its already allocated RGB presentation buffer. Each custom
program adds `8*N` RGBA frame bytes, VM/resources and an admitted state baseline.
All allocations and physical snapshot charges use the aggregate VM ceiling;
candidates and current playback coexist during peak admission. There are no
per-frame allocations. Replacing a composition or effect prepares the entire candidate
before changing native ownership.
