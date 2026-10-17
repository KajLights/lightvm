# A/B scenes and transitions

Each output can retain two independently layered scenes, A and B. A transition
combines their latest completed RGB frames at an absolute progress percentage.
0% gives A exactly; 100% gives B exactly. An empty slot contributes black.
Endpoints keep both slots, generations, layer settings, clocks and RNG state.
Hidden scenes keep animating and can be prepared or replaced independently.

A single scene continues to use its existing presenter until B or a transition
control is used. Enabling A/B retains the running A instances. A direct program
becomes A's `main` layer with its current state. Subsequent named activation in
A or B replaces only that scene. Transient preview uploads retain their existing
whole-output replacement behavior. A/B state is held in RAM; startup remains the
configured single procedure.

## Progress and effects

Progress is a finite number from 0 to 100. Setting it selects that percentage
immediately, including backwards movement or jumps. It never replays intermediate
percentages. The native owner captures one percentage for an evaluation, pins
completed scene frames during mixing, and publishes only the completed result.
Continuous updates mark the next evaluation dirty without cancelling an evaluation
already in progress. Snapshots and the physical driver retain complete frames.

Crossfade uses encoded RGB8 values, with complementary weights and one rounding:
`round(A * (1-p) + B * p)`, where `p = progress / 100`. Identical inputs remain
identical. Brightness and wire order apply once to the final physical output.

The supplied [Python crossfade and wipe programs](transition-profile.md) are
compiled and stored under ordinary procedure names. Select them with
`transition_select` and `procedure`, just as scenes recall stored animations.
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
pauses a scene.

A timed movement starts at the current progress and linearly interpolates to the
target over `duration_ms`, using the native monotonic clock. The target and
nonnegative finite duration are validated before replacing a driver. Zero duration
sets the target immediately. Completion sets the target exactly and leaves the
driver manual. Runs continue when the client disconnects, independently of guest
FRAME deltas or scene pause state.

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
`procedure_list` reports each current package's `profile` so clients can filter
stored animations and transitions without a firmware effect-name table.

All operations accept `device`. Transition controls address the output owner,
without a slot selector. Scene and layer operations accept `slot`, defaulting to A.
Scene revisions are shared by A and B, including transition-effect selection.
Preparation failures and stale revisions preserve both scenes and the active driver.

| Operation | Fields / response |
| --- | --- |
| `transition_get` | State with A, B, procedure, pinned program CRC32, progress, driver, counters and errors. |
| `transition_set` | `progress`; releases the driver. |
| `transition_run` | `target`, `duration_ms`, optional `interval_ms`; replaces the driver after validation. |
| `transition_assign` | `source`; returns `driver.assignment`. |
| `transition_update` | `source`, `assignment`, `progress`; status value 1 if accepted, 0 if ignored. |
| `transition_hold` | Releases the driver and retains current progress. |
| `transition_select` | `procedure`, `expected_revision`, optional initialization `settings` and unsigned `seed`. Legacy alternative: `effect` with optional `mask`/`paint`. |
| `scene_apply` / `preset_activate` | Existing operations plus optional `slot`. |
| `scene_pause` / `scene_resume` | `slot`, `expected_revision`; retain scene contributions and state. |
| `scene_clear` | `slot`, `expected_revision`; unloads just that slot in A/B mode. |
| `scene_snapshot` | Default: last physical RGB. Explicit `slot`: that slot's completed RGB in A/B mode. |

Transition writes return refreshed state, except source updates. Structural writes
use `expected_revision`; progress updates do not require a revision. Source tokens
are unsigned 32-bit counters, are not reused within a boot, and never wrap; new assignments are rejected on
exhaustion. Hold remains available.

Root PLAY, STOP, FRAME and RESET control the whole A/B output. STOP retains scene
state and clears the physical lights. FRAME advances both scenes by its explicit
delta, evaluates current controller progress and presents one complete mixed frame.
RESET unloads both scenes and the transition. Existing program controls and layer
commands accept both A/B slot selectors. Commands addressed to B alone affect B;
use `scene_pause` / `scene_resume` for either whole scene while retaining its frame.

```python
client.configure_scene(scene_a_layers, device="lights", slot="A")
client.configure_scene(scene_b_layers, device="lights", slot="B")
client.select_transition("crossfade", device="lights")  # stored Python package
# Or: client.select_transition("wipe", settings={"reverse": True}, device="lights")
client.set_progress(35, device="lights")
client.run_transition(100, duration_ms=2000, device="lights")
client.hold_transition(device="lights")
state = client.assign_transition_source("sensor", device="lights")
client.update_transition_source("sensor", state["driver"]["assignment"], 42,
                                device="lights")
```

The CLI supports `--slot B --scene FILE`, `--get-transition`, `--progress 35`,
`--run-to 100 --duration-ms 2000` (add `--interval-ms 10000` to alternate), `--hold-transition`, `--transition wipe --settings '{"reverse": true}'`,
`--assign-source NAME`, and `--source-value P --source-name NAME --assignment TOKEN`.
The browser's A/B panel loads saved device presets into either slot, selects a
stored transition program, sets progress, cuts to endpoints, starts timed movements and assigns a
management source. The preset composer can preview into A or B.

## Legacy custom mask and paint programs

Custom transitions reuse ordinary `profile("animation")` packages and the shared
interpreter. `mask` and `paint` each accept an object containing `procedure` and
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
`client.select_transition_effect("custom", mask={"procedure": "mask"}, device="lights")`.
Custom programs cannot read scene frames or drive physical outputs. A custom fault
retains the last completed physical output; selecting a fresh effect recovers.

## Ownership and memory

The native owner uses 32-pixel mixing slices, 64-instruction cooperative custom
program slices and 256-byte state-restore slices. Execution and mixing can yield
while the scene engines continue advancing. Only the completed front buffers
are pinned: scene rendering and back-buffer composition continue until the back
buffer is ready. A ready back buffer waits for publication before starting more
layer renders; animation clocks retain the actual elapsed time. Python programs
release scene fronts as soon as their RGBA result is committed, allowing source
publication during final RGB conversion. State checkpoints contain mutable values
and owned payload, excluding immutable resource descriptors. Completed restore
and evaluation phases advance within the same bounded scheduler visit.
Python transitions coalesce progress
updates to the presentation cadence. A pending physical transfer
retains its front buffer while another completed result is prepared. Transfer
failure keeps the last successfully presented snapshot and retries.

Each offscreen scene needs `6*N` RGB bytes for working and completed composition,
plus `8*N` per RGBA layer and its VM resources. The transition needs `9*N` for its
working, front and physical snapshot storage. Thus two single-layer scenes need
`37*N` frame bytes before VM, code-cache and native-driver storage. A promoted
direct instance retains its already allocated RGB presentation buffer. Each custom
program adds `8*N` RGBA frame bytes, VM/resources and an admitted state baseline.
All allocations and physical snapshot charges use the aggregate VM ceiling;
candidates and current playback coexist during peak admission. There are no
per-frame allocations. Replacing a scene or effect prepares the entire candidate
before changing native ownership.
