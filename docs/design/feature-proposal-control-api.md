# Future control and input model

Status: Proposed. The implemented contract is [animation/1](../animation-profile.md)
with frozen initialization settings and [current management](../control-api.md).
Live INPUT, general events, jobs and device capabilities below are future work.

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

A future INPUT contract should deliver stable typed values for one callback.
Frozen settings remain initialization data. Changes to INPUT must not mutate
settings or restart a program. Define sampling, capture timestamps, validity,
units, delivery boundaries and pause behavior before exposing an API.

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

## Publication and ownership

Current animations publish completed premultiplied RGBA contributions; the
compositor publishes a completed scene and the driver presents immutable RGB.
Future output batches must define success/suspension boundaries and failure
behavior. A wait cannot hold an unfinished output batch. Guest state generally
is not transactional. Previously published output survives a failing step.
Different physical devices cannot promise simultaneous electrical rollback.

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

Implement INPUT separately after the current-only cleanup. Specify one contract,
generate/check producer-consumer metadata, reject superseded contracts and test
queue overflow, pause clocks, cancellation, stale generations, ownership,
fault publication and no allocation during execution. Continue to support
independent [A/B planning](feature-proposal-ab-transitions.md),
[devices](feature-proposal-device-support.md) and
[build profiles](feature-proposal-build-profiles.md).
