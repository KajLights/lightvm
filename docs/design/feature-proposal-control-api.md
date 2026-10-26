# Future control and input model

Status: Proposed for general guest events, triggers, jobs and controller
capabilities. The implemented contract includes
[animation](../animation-profile.md), frozen initialization settings,
[scalar live inputs](../inputs.md) and [current management](../control-api.md).
Keypad events already exist for the MQTT/HA adapter; general guest event delivery
below remains future work.

The [external input research and design](feature-proposal-inputs.md) develops
the scalar INPUT path for transition progress and live animation/sprite controls,
including native sensors, peripherals, serial bridges and network datagrams.

## Common resources

A resource has an identity, generation, owner and bounded lifetime. A capability
specifies typed operations, units, ranges and failure semantics. A property is
current state; an event is an occurrence; a sample includes capture time and
validity. Reading state must not consume occurrences. Requested actuator output,
committed commands and measured feedback are distinct.

Native configuration owns pins, buses, timers and driver selection. Programs
receive validated resource bindings. Only the engine owner executes guest code
or changes lifecycle state; interrupts and network callbacks capture or enqueue
bounded data. A stale generation must never address a replacement instance.

## INPUT snapshots and events

Scalar INPUT already delivers stable typed values for one render invocation.
Frozen settings remain initialization data. Changes to INPUT must not mutate
settings or restart a program. Extend this model with explicit event capture
timestamps, validity, units, delivery boundaries and pause behavior before
exposing a guest event API.

The [A/B and Transition proposal](feature-proposal-ab-transitions.md) uses one
settable progress percentage. A timed routine or an external source can update
it without depending on this general INPUT model. This proposal owns the broader
typed INPUT contract. All additions use the
[application release contract](../versioning.md).

Events preserve edge/message ordering and counts. State samples may coalesce
only under a documented policy. Queues have fixed capacities, dropped counters,
overflow diagnostics and recoverable current state. A measurement that is
unavailable is not zero. Management observers must not consume guest events.
Debounce, hysteresis, threshold rearming and startup-edge policy are explicit.
Input servicing must work independently of animation frame rates.

## Time, tasks and jobs

Keep native waveform generation, peripheral sampling and guest scheduling
independent. Monotonic capture/deadline time differs from pausable program time.
A job declares its clock, pause, replacement, completion and cancellation policy.
Command completion must not imply physical arrival without feedback.

Only bounded work runs in one scheduling slice. Triggers declare queue, restart,
ignore-while-active or bounded-multiple-invocation behavior. Waits reserve their
continuation state before execution; native threads are not required. No task,
timer, subscription or event queue may imply unbounded allocation. Unloading an
owner cancels its tasks, registrations and owned jobs. Correlation IDs alone do
not deduplicate retried actions; non-idempotent requests need retry identities.

## Triggered lightning effect proposal

**Status: proposed example and acceptance workload; no guest trigger API or
lightning implementation is delivered.** A named trigger starts a short lightning
burst over the current background. Several irregular flashes with quick attacks,
short gaps and a fading tail give each burst its shape. The layer is transparent
when idle and between flashes, and returns to transparency on completion without
restarting the background or retaining a painted frame.

This is ordinary animation code using generic event delivery, timing, sampling,
envelopes and drawing. Do not introduce a native lightning operation, a special
lightning scheduler or a source-language-specific callback contract. The source
API spelling is to be designed with the event extension, not presented here as
an available declaration or decorator.

### Trigger and lifecycle contract

- Begin with a synthetic management/editor trigger, then bind the same event to
  an admitted button/gesture source. Animation code names the event; controller
  configuration selects the hardware or transport. Existing keypad publication
  to HA does not by itself supply guest delivery.
- An audio-onset binding is a later producer with explicit threshold, hysteresis,
  rearming and refractory rules. Current `music.onset` is sampled scalar state;
  it cannot guarantee recovery of a pulse lost between samples or packets. Do
  not describe thresholding it as reliable beat/event delivery.
- Each event carries a validated target generation, stable occurrence/retry
  identity and capture time. A bounded ingress queue and bounded deduplication
  window are admitted before activation. Acknowledgements distinguish admission,
  expiry/rejection and any promised completion; retries within the documented
  window must not create additional bursts. Specify exhaustion and expiry rather
  than promising unbounded duplicate history or replay across reboot.
- Propose one active burst with **restart on a new accepted event** for the first
  example. A duplicate does not restart it. Other generic trigger policies may
  be implemented separately, but concurrent bursts and pending queues always
  require explicit capacities. Proposed queue overflow drops the newest event
  with a diagnostic; it must not silently merge distinct occurrences.
- Dispatch through the engine owner at a render boundary. Record a burst start
  in pausable program time; monotonic capture/deadline time governs ingress
  expiry. Proposed pause policy freezes an active burst, rejects new triggers
  while paused and discards pending triggers on entering pause. Resume must not
  replay a backlog. Unload/replacement cancels pending and active work, and stale
  generations cannot flash a replacement scene.

### Procedural burst with bounded state

Keep only one burst seed/identity, start time and active flag, plus admitted
settings and bounded execution scratch. Derive flash count, incremental
`start_delta`, duration, intensity and optional spatial extent from the seed and
flash index, using the generic indexed sampling proposed for
[Stars](core-numeric-kernels-plan.md#stars-optimisation-proposal-procedural-reconstruction).
Reconstruct at most the configured maximum flash count for the current burst;
clamp or reject generated timing outside its maximum total duration. There is
no per-flash array, replay of previous bursts or render-time allocation.

Unlike autonomous Stars, an external trigger cannot be reconstructed from wall
time alone: the active event record and bounded ingress/deduplication state are
real memory costs. Because each burst has a bounded flash count and duration,
replaying its short generated sequence is bounded without time-window history.
Define endpoint ownership, finite numeric ranges and time precision explicitly.

Start with configurable colour, local intensity and burst timing, using existing
artwork/drawing and mathematical envelopes. Whole-strip flashes provide the
first fixture; spatial variation can reuse generic sampling if useful. Smooth
noise may modulate a flash's envelope or footprint, but is optional and does not
determine whether an external trigger occurred. Output power, master brightness
and the configured hardware brightness limit continue to apply normally.

### Delivery and acceptance

Make lightning the concrete demonstration for the input plan's
[event-delivery stage](feature-proposal-inputs.md#suggested-delivery). Before
guest events land, rendering/math fixtures may inject a fixed burst seed and
start time directly; that validates the effect only, not trigger delivery.

Verify one accepted occurrence starts one burst; duplicate/reordered/retried
messages, expiry, queue overflow, sustained audio levels, pause/resume and stale
generations follow the declared rules. Compare the same seeded burst at the same
program timestamps with different frame cadences, skipped frames and chunk
boundaries. Render the current phase after a stall rather than replaying missed
flashes; a completed burst is transparent. Measure latency from event capture to
the first displayed contribution separately from queue admission and computation,
alongside memory, largest execution slice and management responsiveness. Keep
all queues, active state and scratch bounded under a trigger flood.

## Publication and ownership

Current animations publish completed premultiplied RGBA contributions; the
compositor publishes a completed composition and the driver presents immutable RGB.
Future output batches must define success/suspension boundaries and failure
behavior. A wait cannot hold an unfinished output batch. Guest state generally
is not transactional. Previously published output survives a failing step.
Different physical outputs cannot promise simultaneous electrical rollback.

Ordinary actuators begin with one exclusive owner. Combining servo targets or
bus transactions needs its own arbitration contract; pixel blend modes do not
supply it. Preserve aggregate admission for state, images, native buffers,
network services, queues and jobs, including peak replacement memory.

## Prior art and provenance

The core is independently authored under MIT with documented picolights roots.
Source-language frontends bind to the core contract and do not define it.
Keep provenance for source-derived algorithms and SDK adapters. Do not copy
GPL-marked picolights easing code or unverified dependency implementations.
Third-party behavior may inform requirements; implement it independently and
retain notices for code actually distributed. See [notices](../../THIRD_PARTY_NOTICES.md).

## Delivery criteria

Extend the implemented scalar INPUT path with guest events separately. Specify
one contract, generate/check producer-consumer metadata, reject superseded contracts and test
queue overflow, pause clocks, cancellation, stale generations, ownership,
fault publication and no allocation during execution. Continue to support
independent [A/B transition planning](feature-proposal-ab-transitions.md),
[controllers](feature-proposal-output-support.md) and
[build profiles](feature-proposal-build-profiles.md).
