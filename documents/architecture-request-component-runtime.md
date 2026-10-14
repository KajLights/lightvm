# LightVM Component Runtime Architecture Request

Status: Requested; feasibility research and architecture selection pending

Date: 14 October 2026

LightVM should host multiple cooperating user-defined effects alongside
communications and device services, with independent effect updates during
operation. The intended experience is a robust resident core with the dynamism
of CircuitPython or MicroPython, but significantly better execution performance
and memory management. Those improvements must be demonstrated against relevant
workloads and baselines.

The architectural reference is an embedded operating environment with scheduling,
priorities and resource ownership. The FreeRTOS analogy does not select an RTOS
or require one native task, VM instance or memory allocation of a fixed size per
component. Browser compilation for a known target is an available design option;
WASM execution on the device is not a requirement of this request.

## Core requirements

1. **Run multiple effects concurrently and switch between them.** Effects need
   independent state and lifecycle. Distinguish loading an effect, running or
   pausing it, and selecting its output. Concurrency must work on a single core;
   simultaneous execution on multiple cores is an additional goal. Define output
   ownership for effects using separate resources or contributing to one scene.
2. **Run communications alongside effects.** Support communications such as USB,
   Wi-Fi and Matter without requiring effects to stop. Each board and firmware
   profile must advertise the combinations and resource capacity it supports.
   Include connection setup, commissioning, reconnects and traffic bursts in
   capacity and timing analysis.
3. **Update effects dynamically without interrupting the entire board.** Replacing
   one effect must not require a firmware reflash, board reboot, global runtime
   reset or intentional shutdown of unrelated effects and services. Flash stalls
   and other hardware constraints must be identified and measured. This is not a
   promise of zero timing interruption during every hardware operation.

## Ideal capabilities

1. **Communications plugins can also be supplied and updated at runtime.** Extend
   the component lifecycle to protocol implementations and integrations. Research
   how far this can extend into the communications stack, including SDK-dependent
   transport services. Replacing an application adapter alone does not establish
   replacement of the full stack. A service restart or reconnection is distinct
   from restarting the board; preserving live sessions is a separate design choice.
2. **Use all available processors effectively.** Independent work should be able
   to run on multiple general-purpose cores. Specialised or asymmetric processors
   should be used according to their capabilities. Avoid making a permanent global
   execution lock part of the component contract.
3. **Use available hardware facilities fully.** Native services should exploit
   GPIO, Pico PIO, DMA, PWM, timers, input capture, ESP32 RMT and other suitable
   peripherals. Hardware operations should allow other components to progress
   while a transfer or waveform is in flight.

## Proposed component boundaries

The following is a design direction to evaluate, not a selected implementation.

| Part | Responsibility |
|---|---|
| Resident core | Scheduling, time, resource admission, memory accounting, component loading, lifecycle, update management and recovery |
| Effect components | User computation, parameters, independent state and owned output contributions |
| Service components | Protocols, integrations and device controllers, with their own budgets and lifecycle |
| Hardware services | Peripheral ownership, precise timing, input capture and physical output |

Component identity, scheduling unit and isolation boundary are separate concepts.
Several tasks may belong to one component, and trusted components may share
infrastructure. Sharing memory changes fault isolation and must be explicit.
Multiple instances should share immutable code where possible while retaining
independent mutable state.

A small set of execution workers is a candidate for multicore operation. A single
instance can initially execute serially while independent instances run in
parallel. Exchange messages or completed buffers through defined ownership
transfers. Serialising a hardware resource or final frame publication need not
serialise all effect computation.

Prefer a versioned component API over direct dependencies on SDK internals.
Peripheral capabilities should express operations such as submitting a buffer,
capturing input or setting a waveform. Platform services choose the appropriate
hardware implementation. Native driver and communications plugin boundaries,
including a recovery path that survives plugin failure, need explicit design.

## Scheduling and resource management

Separate physical output timing, input sampling, control updates, rendering,
protocol servicing and management work. Define deadlines, priorities, CPU budgets,
queue bounds and overload behaviour for each relevant class of work. Priority
alone does not prevent starvation. Resource contention and priority inversion
must be addressed, including across native and component scheduling layers.

Guest work must either complete in a bounded step or suspend and resume with
bounded interruption latency. Aborting an over-budget invocation is fault handling,
not resumable scheduling. Long loops, nested calls and blocking native calls all
matter. Consider compiler-generated continuations, runtime continuations and
native task contexts, charging the retained state of each approach.

Distinguish a scheduling interruption from a logical output commit. Suspending
computation must not publish a partial frame or an incomplete batch of actuator
changes. Define cancellation and fault behaviour for each owned resource.

Admission must cover component state, execution stacks or continuations, runtime
metadata, queues, timers, buffers and native allocations. Include peak coexistence
during uploads, validation, commissioning, reconnects and replacement, as well as
the largest required contiguous allocation. Evaluate per-instance arenas, pooled
storage and allocation-free rendering. Runtime heap counters alone are insufficient.

## Proposed replacement lifecycle

1. Receive and validate a new package while the existing component remains active.
2. Reserve resources and prepare the replacement without taking over live outputs.
3. At a defined scheduling or frame boundary, transfer ownership and optionally
   migrate compatible state.
4. Reclaim the old instance only after its executing work, queued callbacks and
   outstanding hardware references have been drained or cancelled safely.

Reject a replacement that cannot be prepared within its budget while preserving
the working instance. Define rollback for activation failure. Supporting old and
new instances together requires explicit replacement headroom; unloading the old
instance first is a different service guarantee. Persistent installation also needs
interrupted-write recovery. State migration and seamless protocol-session transfer
remain optional capabilities rather than prerequisites for every replacement.

## Technology choices to research

Compare these approaches against the same component contract and fault guarantees:

- Browser compilation directly to target-specific native components.
- Browser compilation through WASM to native components with an AOT runtime.
- Interpreted WASM with resumable execution and an appropriate memory model.
- Embedded Java or another compact managed runtime with lightweight tasks.

The browser compiler may itself run as WASM without requiring WASM on the device.
Direct native compilation allows a component-specific memory layout; AOT compilation
from WASM does not automatically remove WASM memory semantics. Native execution
still needs a loader, lifecycle management and the selected bounds, stack and
scheduling checks. Ordinary native C plugins do not inherently contain memory faults.

Evaluate flash-resident execution, relocation requirements, compiler download and
working-memory costs in the browser, and backend availability for each target.
Separate flash-write interference from scheduler priority. Keep a browser preview
or simulator path with equivalent component semantics.

Target-specific compilation would move portability towards common source and API
semantics rather than identical executable bytes. This is a future architecture
option; the existing WASM ABI and its portability commitments remain the current
implementation contract until a migration is designed.

## Evaluation and open decisions

Use mixed workloads with multiple effects, communications and live replacement.
Exercise event floods, reconnects, commissioning, allocation failures, runaway
computation and component faults. Report deadline misses, worst observed dispatch
and interruption latency, frame-gap distributions, recovery behaviour, peak RAM,
largest allocatable block and scaling across cores. Distinguish measured maxima
from proven timing bounds, and strict deadline misses from skipped-frame counters.

Compare useful component capacity and whole-system behaviour, including relevant
MicroPython native-module workloads, rather than only interpreter throughput or
average FPS. Choose representative effect counts, output sizes, deadlines and
improvement targets before declaring a technology suitable.

Resolve the required fault-isolation level, permitted source languages, plugin
trust model, supported board profiles, output composition semantics, component API,
replacement-state policy and achievable communications plugin boundary. No runtime,
compiler, package format or RTOS is selected by this request.

## Related architecture and proposals

- [Current engine and native ports](../docs/architecture.md)
- [Control API and execution model](feature-proposal-control-api.md)
- [Device support](feature-proposal-device-support.md)
- [Animation composition](feature-proposal-animation-composition.md)
- [A/B transitions](feature-proposal-ab-transitions.md)
- [Build profiles](feature-proposal-build-profiles.md)
- [Web editor](feature-proposal-web-editor.md)
- [Existing MQTT feasibility measurements](../experiments/mqtt/REPORT.md)

The current WASM-specific and single-owner descriptions explain the implemented
system or earlier proposals. Their assumptions should be revisited during this
architecture research rather than treated as restrictions on the requested future
component model.
