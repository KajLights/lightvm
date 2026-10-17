# LightVM A/B and Transition

Status: Implemented — see [A/B scenes and transitions](../transitions.md).

Date: 17 October 2026

## Model

There are two scene slots, **A** and **B**, and a **Transition** between them.
The transition has one settable **progress percentage**, from 0% to 100%.
It renders the output from A and B at that percentage.

Progress can be set directly, updated by a small routine over a specified
period, or assigned to an external source. All three use the same value.
The transition does not need to know what sets it.

| Part | Responsibility |
|---|---|
| A | Supply the completed frame from scene A. |
| B | Supply the completed frame from scene B. |
| Transition | Combine A and B according to its effect and current progress. |

The existing [scene contract](../composition.md) supplies each scene, including
its layers, settings, state and clocks. Firmware implements scenes, A/B mixing and the progress controls here.

## Progress

- **0%** outputs A exactly.
- **100%** outputs B exactly.
- Values between them produce the transition's appearance at that percentage.

Progress can increase, decrease, jump to any percentage, or stay fixed.
Setting 35% means displaying the transition at 35%; it does not mean advancing
by 35%. Reject non-numeric, non-finite or out-of-range values.

Changing progress does not restart either scene, swap A and B, or reset the
transition at an endpoint. Both scenes can continue animating while progress
stays fixed. Scene pause, resume, replacement and unloading remain scene
operations. Reaching an endpoint does not perform those operations automatically.
The hidden slot can be prepared while the other scene supplies the output.

## Transition appearance

The selected effect defines what a percentage looks like. Crossfade is the
default: at 35%, the output is 65% A and 35% B. Use the existing encoded RGB8
values and complementary weights, so mixing an image with itself preserves it.

Other effects use the same progress control:

| Effect | Appearance controlled by progress |
|---|---|
| Wipe | Progress places the boundary between A and B. |
| Soft wipe | Progress places a boundary with a gradual edge. |
| Dissolve | Progress determines which pixels reveal B, using fixed thresholds. |
| Dip through black | A fades to black, then black fades to B. |
| White flash | A changes to B under white paint around the midpoint. |

A custom transition can calculate a per-pixel reveal mask and optional
premultiplied RGBA paint above the scene mix. These use the existing drawing
and blend rules. At the endpoints, output is exactly A or B, without paint.
The same progress, geometry, settings and seed give the same mask and paint.
Scene frames themselves can keep changing.

The effect evaluates the current percentage directly. It does not run through
previous percentages to reach it. Time belongs to the routine that sets progress.

## Setting progress

Only one routine or external source drives progress at a time. Selecting a new
driver replaces the previous one. A direct manual set removes that assignment
and leaves progress at the supplied value. **Hold** removes the assignment and
keeps the current value. These actions do not pause the scenes.

### Timed routine

A small controller routine moves from the current percentage to a target over
the requested duration. For a linear movement:

```text
start = current progress
u = clamp(elapsed / duration, 0, 1)
progress = start + (target - start) * u
```

Elapsed time uses the controller's monotonic clock. A movement from 35% to 100%
over two seconds takes two seconds. Movement to 0% uses the same calculation.
A zero duration sets the target immediately; a negative or non-finite duration
is rejected. Validate the target and duration before replacing the active driver.

At the end, set the target exactly, stop the routine and leave progress there.
A new routine starts from the current value. It needs only its start, target,
start time and duration; it does not need a general job system or completion
history. Once started, it continues on the controller if the browser disconnects.

### External source

An assigned external source supplies the percentage instead of the timed routine.
Examples are a slider, sensor, telemetry value or incoming message. Any conversion
from the source's units to 0–100% happens before setting progress. A source can
move in either direction or hold a value indefinitely; it need not complete.

If the source has no usable value, keep the current progress. An invalid update
also leaves it unchanged. Replacing or removing the assignment stops the old
source from changing progress. Updates set current state; they do not form a
queue of percentages that must all be displayed.

Adapters use whichever drivers and transports the firmware provides. A/B does
not require ADC, MQTT or a general guest INPUT framework to be implemented first.

## Controls

These are conceptual operations; final wire names belong to the shared
[management API](../control-api.md).

| Operation | Behavior |
|---|---|
| Set progress | Remove the active driver and set a percentage. |
| Run to target | Assign the timed routine with a target percentage and duration. |
| Assign source | Replace the driver with an external source of percentages. |
| Hold | Remove the driver and retain the current percentage. |
| Read state | Report A, B, the selected transition, progress and the active driver. |

A cut to A or B is simply Set progress to 0% or 100%. A UI needs A and B scene
selection, transition selection, a progress slider, duration controls and source
assignment. No separate replay or generation mechanism is introduced for A/B.

## Implementation boundaries

Use the existing native owner, scene preparation, completed-frame publication,
output-buffer ownership and aggregate memory checks. Capture progress once for
each transition evaluation and publish only a completed result. Keep the last
completed output available while another result is being calculated.

The timed routine and source adapters update the same progress property through
the native owner. Rendering and updates remain bounded. Account for both scenes,
the transition and output buffers within the device's advertised limits.

Use the shared interpreter for custom transition programs and the existing
[application release contract](../versioning.md) for new capabilities. This
includes named Python `profile("transition")` programs compiled and stored like
animations; see the [transition source contract](../transition-profile.md).
This proposal does not define a general source registry, event system or task API;
that broader work belongs to the [control model](feature-proposal-control-api.md)
and is independent of this feature.

## Acceptance criteria

- Setting 0%, 35% and 100% produces the corresponding output directly.
- Progress can move backwards, jump or stay fixed without restarting scenes.
- A timed routine reaches its target over the specified duration and holds it.
- Manual set, Hold and driver replacement prevent further updates from the old driver.
- An external source drives the same progress property without a timed routine.
- Missing or invalid external values leave progress unchanged.
- Endpoint selection preserves A/B slot names and scene state.
- Both layered scenes and transition buffers fit the declared device limits;
  failed preparation preserves current playback.
