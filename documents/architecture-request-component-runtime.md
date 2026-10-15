# LightVM Component Runtime Architecture Request

Status: Requirements retained; portable typed bytecode direction selected, implementation pending

Date: 14 October 2026

Updated: 15 October 2026

The [portable bytecode architecture decision](architecture-decision-portable-bytecode.md)
selects a small typed interpreter, off-device compilation from multiple source
languages and flash-resident programs as the next implementation direction. It
also records the staged animation API and numeric policy. These requirements
remain the acceptance criteria; exact formats, resource capacity and persistent
replacement timing still need specification and measurement.

LightVM should host multiple cooperating user-defined effects alongside
communications and device services, with independent effect updates during
operation. The intended experience is a robust resident core with the dynamism
of CircuitPython or MicroPython, but significantly better execution performance
and memory management. Those improvements must be demonstrated against relevant
workloads and baselines.

The architectural reference is an embedded operating environment with scheduling,
priorities and resource ownership. The FreeRTOS analogy does not select an RTOS
or require one native task, VM instance or memory allocation of a fixed size per
component. Native memory mapping for pixel arrays and other hardware-facing buffers
is a foundational requirement. Memory placement, ownership and the path from effect
computation to physical output guide runtime design. Compile portable bytecode in
the browser or on a computer; WASM execution on the device is not required for
the future contract. Target-specific native code remains an optional execution
optimisation, not a replacement for portable distribution.

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
4. **Map component buffers to native memory usable by optimized operations and
   hardware.** Effect code must be able to access bounded pixel-array views backed
   by native allocations. Native array operations and, where supported by the
   target and format, DMA must operate on the same backing storage. A private
   runtime copy or a host call for every pixel must not be the only access path.
   Memory placement, alignment, format and lifetime must satisfy the intended
   consumers. Native mapping means access to the same backing storage; it does
   not require virtual-memory hardware or unrestricted access to device memory.
5. **Distribute identical programs to mixed compatible devices.** Compile once and
   broadcast one package, including over MQTT, without per-controller native
   builds at the sender. Each device checks the package's API, capabilities and
   resource requirements; wiring and geometry bindings remain local.
6. **Support multiple authoring languages through one execution contract.** Python,
   JavaScript, blocks and compatibility frontends should share typed intermediate
   representation, APIs and portable bytecode. Specify supported subsets and
   numeric mappings; separate language frontends must not require separate device
   interpreters or silently change existing source semantics.

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
3. **Use available hardware facilities fully.** Extend the required native buffer
   integration across the available peripherals. Native services should exploit
   GPIO, Pico PIO, DMA, PWM, timers, input capture, ESP32 RMT and other suitable
   peripherals. Hardware operations should allow other components to progress
   while a transfer or waveform is in flight.

## Proposed component boundaries

The selected direction uses these boundaries; their concrete APIs and service
isolation mechanisms remain to be implemented and validated.

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

## Native buffers and data flow

The resident core should allocate or register buffers according to their consumers:
effect computation, native transforms, composition and physical output. Define
bounded views with element format, extent, access rights and lifetime. Permit
native fill, blend, conversion and other optimized array operations on these views,
as well as direct effect loops. A handle is useful for ownership, but a handle
whose only operations are individual pixel calls does not meet the mapping
requirement.

Specify when a component may write, when a completed buffer is published, and when
the compositor or driver releases it. Submitted storage must remain at a stable
address and must not be overwritten, moved or reclaimed while a consumer or DMA
transfer retains it. Define completion, cancellation, stale-view rejection and
any target-specific cache synchronization. Component replacement must respect
these outstanding references independently of the old code's lifetime.

Use ownership transfer or buffer rotation where it avoids copying. Composition,
format conversion, retaining a previous good frame and overlapping output with
rendering may require additional storage. Count every retained buffer and copy,
with its purpose and peak lifetime; zero-copy between compatible stages does not
imply that every scene can use one buffer. The current consume-or-copy-before-return
output contract is an implementation constraint to replace when evaluating this
model, not a requirement for the future architecture.

Evaluate memory protection against these shared buffers explicitly. Restricted
views and checked access must enforce the selected isolation policy; sharing a
native allocation does not itself provide fault containment. Any runtime candidate
must demonstrate this buffer model within the board budget before detailed
interpreter or compiler throughput comparisons drive a choice.

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

Give loading, initialization, activation, ordinary execution and teardown separate
budgets. Preparation must permit existing components to progress, including during
native calls and bulk operations. A longer initialization timeout alone does not
establish live replacement without disruptive stalls.

Distinguish a scheduling interruption from a logical output commit. Suspending
computation must not publish a partial frame or an incomplete batch of actuator
changes. Define cancellation and fault behaviour for each owned resource.

Admission must cover component state, execution stacks or continuations, runtime
metadata, queues, timers, buffers and native allocations. Include peak coexistence
during uploads, validation, commissioning, reconnects and replacement, as well as
the largest required contiguous allocation. Evaluate per-instance arenas, pooled
storage and allocation-free rendering. Runtime heap counters alone are insufficient.
Account separately for executable code, ordinary RAM and memory with peripheral or
access restrictions. Include compiler workspace if compilation occurs on the
device, allocations deferred until first use, and all simultaneously retained
buffer versions. A runtime's allocation granularity must fit the component model
and board capacity rather than dictate them.

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

## Selected execution direction and alternatives

Advance the small typed interpreter with native buffer views, explicit saved
execution state and flash-resident bytecode. Compile source off-device and avoid
a second expanded instruction representation in RAM. The initial device loader
validates and prepares packages; it does not JIT-compile native code. Establish
the shared staged animation and numeric contract before freezing the instruction
set or language bindings. Exact fixed-point formats and the default real type
remain measurement-driven choices.

Use the native implementation as a performance and data-layout reference, and
existing WASM runtimes as compatibility paths and measured alternatives. A native
reference does not establish safe dynamic loading or fault isolation. Revisit
install-time or selective compilation only where it offers a measured benefit
within compiler workspace, code-storage and scheduling budgets.

The browser compiler may itself run as WASM without requiring WASM on the device.
Direct native compilation allows a component-specific memory layout; AOT compilation
from WASM does not automatically remove WASM memory semantics. Native execution
still needs a loader, lifecycle management and the selected bounds, stack and
scheduling checks. Ordinary native C plugins do not inherently contain memory faults.

Evaluate flash-resident execution, relocation requirements, compiler download and
working-memory costs in the browser, and backend availability for each target.
Separate flash-write interference from scheduler priority. Keep a browser preview
or simulator path with equivalent component semantics.

Requiring target-specific uploads would violate the selected mixed-device
distribution requirement. Optional native implementations must derive from the
portable package without requiring sender-side builds for each board. The existing
WASM ABI and its portability commitments remain the current implementation
contract until an explicit migration is designed.

## Evaluation and open decisions

The [same-board component experiment](../experiments/component-runtime/device/REPORT.md)
has exercised two integer effects, native buffer views, asynchronous RMT output,
USB traffic and RAM-resident replacement. Next extend the typed path with a real
source compiler, representative numeric workloads, flash-resident execution and
persistent replacement while communications and effects remain active. Include
replacement while a transfer is outstanding. Use DMA where the selected peripheral
supports it; trace placement, ownership, copies, retained bytes and completion
through computation, composition and physical output.

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

The [focused runtime measurements](../experiments/runtime/REPORT.md) provide
evidence about the tested integrations' memory overhead, guest/host crossings,
initialization costs and synchronous output. They do not establish the feasibility
of native buffer mapping, DMA integration, concurrent effects or the requested
replacement model. Keep them as supporting measurements. Further runtime-specific
benchmarks should answer a concrete unresolved architecture question, particularly
numeric cost, whole-device capacity and persistent installation interference.

Resolve fault isolation, supported frontend subsets, numeric formats, plugin trust,
board capacity, composition, concrete component APIs, replacement-state policy and
the communications plugin boundary. The portable interpreter direction and
multi-language input requirement are selected. The package encoding, compiler
implementation and RTOS choice remain open.

## Related architecture and proposals

- [Portable bytecode and animation contract](architecture-decision-portable-bytecode.md)
- [Current engine and native ports](../docs/architecture.md)
- [Control API and execution model](feature-proposal-control-api.md)
- [Device support](feature-proposal-device-support.md)
- [Animation composition](feature-proposal-animation-composition.md)
- [A/B transitions](feature-proposal-ab-transitions.md)
- [Build profiles](feature-proposal-build-profiles.md)
- [Web editor](feature-proposal-web-editor.md)
- [Existing MQTT feasibility measurements](../experiments/mqtt/REPORT.md)
- [Focused runtime measurements](../experiments/runtime/REPORT.md)

The current WASM-specific and single-owner descriptions explain the implemented
system or earlier proposals. Their assumptions should be revisited during this
architecture research rather than treated as restrictions on the requested future
component model.
