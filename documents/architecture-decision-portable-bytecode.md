# Portable bytecode and animation contract

Status: Selected architecture direction; implementation and hardware validation pending

Date: 15 October 2026

LightVM will develop a small typed bytecode interpreter as the default execution
path for the component runtime. A browser or computer compiles supported source
languages into one portable package. Compatible controllers receive identical
bytes, store code and constants in flash, and reserve RAM for execution state,
services and buffers. The initial implementation will not compile programs into
native machine code on the device.

This decision refines the [component runtime requirements](architecture-request-component-runtime.md).
It selects the direction for new development; production still uses WASM with
legacy API v1 or [control ABI 2](../docs/control-api.md). Existing modules retain
their documented numeric, callback and publication behaviour. The custom package
format, exact API signatures and migration mechanism remain to be specified.

The shared **language-neutral animation contract** covers
entry points, types, library functions, resource declarations and scheduling rules.
These semantics apply to the bytecode and every language binding, independently
of the instruction encoding.

## Decisions and remaining choices

| Area | Selected direction | Still to establish |
|---|---|---|
| Distribution | Compile once and broadcast the same portable bytecode package to mixed compatible devices | Binary encoding, capability negotiation and migration from WASM |
| Compilation | Multiple source frontends, a shared typed intermediate representation and an off-device bytecode backend | Supported subsets and optimisation passes |
| Execution | Small register-based interpreter, explicit continuations and checked native buffer views | Instruction set, context layout and measured capacity |
| Storage | Interpret the stored bytecode directly from flash; share immutable code between instances | Flash layout, update timing and power-loss recovery per board |
| Animation API | Separate simulation, rendering, composition, geometry and device services | Exact signatures, colour/alpha formats and overload policies |
| Numbers | Prefer ordinary 32-bit integers; evaluate explicit fixed point and `f32`; make `f64` optional | Fixed-point formats, defaults, rounding and performance limits |

## Portable compilation and execution

The compiler pipeline is:

```text
Restricted Python / restricted JavaScript / blocks / compatibility frontends
                               |
                    Shared typed representation
                               |
                 Portable bytecode and manifest
                               |
                   USB, HTTP or MQTT delivery
                               |
                Controller flash and interpreter
```

The firmware interpreter is built for each processor. A program contains portable
operation IDs, register references, state locations and code offsets, not native
addresses. Different source programs need not produce identical bytecode; the
requirement is that a compiled package can be distributed unchanged across devices
supporting its version, capabilities and resource requirements. Device wiring and
geometry bindings remain local deployment data.

Type checking, constant folding, register assignment and reuse, branch resolution and
resource-layout preparation belong in the browser/computer compiler. Adding a
frontend must not require another source-language runtime on the controller.
Python and JavaScript support means documented subsets and bindings, not their
complete general-purpose runtimes. Pixelblaze compatibility is a separate frontend
or adapter with explicit handling of its numeric and frame-callback semantics.

The package specification must describe the information needed for independent
admission: format and API/library versions, numeric capabilities, entry points and
function signatures, code/constants, mutable-state layouts and initial values,
register/local/call-storage limits, buffer requirements, parameters and required
inputs/services. Define native operation IDs and their signatures as part of the
versioned contract. This is a required information set, not a frozen binary header;
board-specific bindings and admitted limits remain deployment data.

The controller must validate the package independently of the source compiler,
including instruction encodings, register types, branch targets, state bounds,
entry-point permissions and required resources. Retain execution-time checks for
dynamic indexes, handle validity, ownership, call depth and work limits. A compiler
manifest is not sufficient evidence that an untrusted upload is safe.

Stored instructions should already be executable by the interpreter, without a
second expanded instruction representation in RAM. The prototype's fixed-width
encoding illustrates simple decoding and validation; the production encoding
remains unspecified. User functions use
bounded call frames. Ordinary suspension saves the current instruction, registers,
locals and return state; it must not unwind and restart an invocation.

Use shared execution workers rather than reserving a native task stack for every
effect. Independent instances may later run on different cores under explicit
ownership rules. Bytecode work, validation and native helpers need bounded service
slices; an instruction count alone does not bound a long native operation. Keep
ordinary yield/resume separate from cancellation and total-work exhaustion.

Native library operations, compiler optimisations and combined instructions can
reduce execution overhead while preserving validation and bounded scheduling.
JIT or install-time native compilation is deferred to a measured need on suitable
devices. Any such backend must preserve bytecode semantics, checks and suspension,
and account for compiler scratch space, generated code and cache lifetime. A
target-specific cached implementation must not become a prerequisite for the
sender's portable broadcast. A WASM-to-bytecode translator is separate work, not
part of the initial deployment path.

## Flash and RAM ownership

| Resource | Placement and lifetime |
|---|---|
| Bytecode, constants, palettes and immutable tables | Flash; shared by instances using the same package |
| Entry points, function descriptions and state-layout metadata | Flash, with small active bindings and bookkeeping in RAM |
| Initial values for mutable state | Template in flash; working values in RAM |
| Interpreter and native helper code | Firmware flash by default; selected routines in instruction RAM where required |
| Registers, locals, call frames, continuation and mutable effect state | Bounded instance storage in RAM |
| Pixel, particle, history and output buffers | Explicit native allocations with formats, access rights and retained lifetimes |
| Service queues, scheduler state, worker stacks and upload workspace | Shared or separately budgeted RAM |

The CPU executes the interpreter and reads bytecode as data. Bytecode does not
require executable RAM. Flash mappings and caches still consume platform
resources; they do not imply a full RAM copy of each mapped package.

Admission must account for the complete peak:

```text
RAM = shared runtime and services
    + instance contexts, mutable state and queues
    + all retained pixel, simulation and output buffers
    + loading, validation and replacement workspace
```

There is no mandatory WASM-sized linear-memory arena or per-frame heap allocation
in this design. Array capacities, scratch requirements and call storage must be
bounded and admitted before execution. Code length principally consumes flash;
state and declared resources determine execution RAM. No fixed production
per-effect footprint has yet been established.

Views refer directly to platform-owned storage and cannot be manufactured from
numeric registers. Native transforms and compatible output hardware use the same
backing allocation. Publication seals storage until all consumers release it;
replacement must preserve outstanding references. Copies, format conversion,
buffer rotation and double buffering are permitted where needed and must be
charged explicitly. Three RGB8 buffers for 300 pixels occupy 2,700 payload bytes;
that example excludes metadata, drivers and extra replacement or in-flight buffers.

## Animation lifecycle and function permissions

Each frontend binds to the same lifecycle. The names below describe semantic
roles; source syntax and binary signatures are not yet fixed. A simple spatial
effect can implement only `shade`. A particle effect can use `step` plus a buffer
kernel without testing every particle for every pixel. Stages are optional according
to the declared rendering mode; every effect need not supply every entry point.

| Entry point | When it runs | Permitted responsibilities |
|---|---|---|
| `init` | Instance preparation or explicit reset | Initialise admitted state and resources within a preparation budget |
| Event handlers | Timestamped events delivered in a defined order at logical update boundaries | Update persistent state under the effect's work budget |
| `step` | Declared logical simulation ticks | Advance mutable state and explicit simulation/history buffers |
| `prepare` | Once per rendered frame | Read a stable state/input snapshot and write frame-local prepared data |
| `shade` | For each requested logical pixel | Read state, prepared data and inputs; return colour without changing shared state |
| Buffer-rendering kernel | A bounded render invocation or slice | Read stable simulation inputs and write declared output/scratch regions |

`prepare` must not become a second frame-rate-dependent simulation step. Mutable
simulation belongs in `step` or ordered event handling. Frame input and parameter
snapshots remain stable across rendering slices. A snapshot is a consistency
guarantee: on a small controller, defer mutation of that instance's state until
rendering releases it. Other instances and services can progress meanwhile. If
simulation must overlap rendering, budget the additional state versions. Admission
and overload policy must account for the resulting scheduling dependencies.

For the same logical tick and relevant inputs, changing presentation frequency,
pixel traversal order or the number of rendered pixels must not advance simulation
state differently. In particular, `shade` cannot consume a shared mutable random
stream; it can use stateless samples keyed by pixel identity and logical time.
Rendering may be split into slices without changing the invocation's snapshot.

History advances on declared logical ticks, not implicitly on whichever frames
the hardware last displayed. Neighbourhood simulations should read previous state,
write next state and swap at a defined completion boundary. In-place processing
requires specified ordering. Sparse writes and splats likewise need explicit
collision rules. Suspension must not publish partial output; state recovery after
faults and cancellation must be specified separately from output publication.

The runtime owns integer time, logical ticks and presentation. Define bounded
catch-up and an overload policy; neither unlimited catch-up nor an arbitrary large
time step is acceptable by default. Matching packages alone do not synchronise
devices: they also need a shared logical timeline, matching seeds and relevant
inputs, and consistent geometry bindings within the installation.

## Shared API and composition

Provide a versioned standard library usable from every frontend:

| API group | Required concepts |
|---|---|
| Time and signals | Durations, frequencies, phases, oscillators, easing, envelopes and smoothing |
| Colour | Explicit encodings, HSV conversion, palettes, gradients, interpolation and blending |
| Geometry | Local index, stable global identity, normalised and physical coordinates, optional topology |
| State and buffers | Bounded arrays, particle storage, explicit history, indexed drawing and transforms |
| Parameters | Stable IDs, types, units, defaults, ranges and optional UI hints |
| Inputs and events | Timestamped samples/events, validity, missing-input behaviour and bounded queues |
| Noise and randomness | Noise functions, owned state-update streams and stateless samples keyed by seed, pixel identity, tick and stream |

Distinguish pure functions from stateful processors. Small helpers can compile
into bytecode; costly common operations can use versioned native implementations.
The library need not add an opcode for every convenience function. Units and
structured source arguments need not introduce runtime objects or allocation.
User-defined helpers remain ordinary compiled functions with bounded call storage.
The compiler should track values that vary per instance, tick, frame or pixel so
that shared calculations can be performed once at the appropriate stage.

Geometry is a binding rather than a pin or strip-offset assumption in the effect.
Compute regular coordinates or read immutable maps from flash where appropriate;
do not require a stored object containing every possible attribute for each pixel.
Keep normalised coordinates, physical distances and topology distinct: a wave
count across an object and a wavelength in metres express different effects.
Periodic-distance helpers can handle rings and other wrapping coordinates. Each
controller can bind its subset of a shared installation coordinate system.
Parameters that alter resource capacity are installation settings; ordinary live
parameters become coherent snapshots at update/render boundaries.

Audio-reactive inputs should use the same sample/event contract: analysed levels,
bands, onset strength or beat phase with defined normalisation, timestamps and
missing-input behaviour. Analysis belongs to a host service or explicit processing
component. Synchronised effects need matching analysed inputs where their behaviour
depends on them; independent microphones do not provide that guarantee.

Composition, modulation, transitions and timelines form a separate declarative
layer above effects. Geometry bindings and native output handle physical placement,
colour encoding, brightness and drivers. Specify colour-space, alpha and palette
semantics before freezing their formats. An animation does not call `show()`,
sleep, configure pins or manage MQTT. The broader requirement for replaceable
service components remains; its API, trust and resource boundaries are separate.

## Numeric model

Most current target families execute floating point in software:

| Target | `f32` arithmetic | `f64` arithmetic |
|---|---|---|
| RP2040 including Pico and Pico W | Software, with optimised ROM/SDK routines | Software |
| ESP32-C3 | Software | Software |
| ESP32-C6 | Software | Software |
| ESP32-S3 | Hardware FPU for supported operations | Software |

See the [RP2040 SDK](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#group_pico_float)
and Espressif's [C3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-guides/performance/speed.html),
[C6](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-guides/performance/speed.html)
and [S3](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html)
documentation. Hardware support for basic arithmetic does not imply hardware
implementations of every trigonometric or library function.

Use `i32`/`u32` as the preferred ordinary integer widths for the new contract.
Evaluate explicit fixed-point types for phases, brightness, interpolation and
other repeated lighting calculations alongside `f32` where its range and
convenience justify the measured cost. Treat `f64` as an explicit optional
capability rather than the animation default. Wider integer timestamps and other
justified 64-bit values remain possible without making every value 64 bits.

The exact fixed-point formats and default real type remain open. Specify scale,
range, rounding, overflow, division, conversions and intermediate widths before
choosing them. A 32-bit fixed-point value and `f32` both occupy four bytes; fixed
point's potential benefit is execution cost. `f32` halves value storage relative
to `f64`, but is still software arithmetic on three of the four target families.
Fixed-point multiplication may need wider intermediates and must also be measured.

Every frontend must map its supported syntax to documented semantics, including
division and remainder. A new embedded language profile may deliberately differ
from general-purpose Python or JavaScript, but must identify those differences.
Do not silently narrow existing Python `i64`/`f64` programs or change legacy
frame callbacks into fixed simulation ticks. Existing WASM profiles remain
unchanged; migration requires an explicit profile or source adaptation.

The same declared type and API operation must retain its specified semantics
across devices. A backend must not substitute fixed point for floating point
merely because a chip lacks an FPU. Specify allowable numerical tolerances and
approximation behaviour separately from any strict replay profile. Native helpers
and JIT compilation do not remove software floating-point costs on those chips.

## Installation and live replacement

Receive bounded chunks into an inactive flash slot; do not require staging an
entire package in RAM. Check completeness/integrity, validate in slices, reserve
resources and initialise the replacement before switching ownership at a defined
frame/scheduling boundary. Preserve the current working instance on preparation
failure. Reclaim old code and buffers only after all execution and consumer
references have ended. Persistent activation requires a durable activation record
and interrupted-write recovery; their formats and mechanisms remain unspecified.

Separate flash slots do not provide independent flash buses. RP2040 programming
interrupts normal XIP access; ESP32-C3 operations can disable caches, with optional
auto-suspend dependent on the actual flash chip and configuration. See the
[Pico flash API](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html#group_pico_flash)
and [ESP32-C3 flash concurrency](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/peripherals/spi_flash/spi_flash_concurrency.html).
Pre-erased slots and bounded writes can help, but uninterrupted effects and
services during persistent installation require measurement on each board.
Specify an achievable update policy rather than treating scheduler priority as
a solution to flash unavailability.

## Existing evidence

The experimental sources and reports are retained on the local
`codex/experiment-component-runtime` branch, which includes the earlier runtime
and MQTT research branches.

The typed prototype (`experiments/component-runtime/typed/README.md`) supplies
an execution and buffer-ownership starting point. Its register file, return
addresses and fixed state total 128 bytes, excluding views, bookkeeping and
buffers. It is a narrow u32 machine, not a production memory estimate for richer
language semantics.

The same-board comparison (`experiments/component-runtime/device/REPORT.md`)
demonstrated two integer effects, asynchronous RMT output, USB service and
RAM-resident replacement on C3. It did not measure flash-resident typed execution,
persistent installation, richer source languages or Wi-Fi/MQTT/Matter workloads.
The WASM runtime comparison (`experiments/runtime/REPORT.md`) separately shows
the memory/speed trade-off between the tested wasm3 and Toywasm integrations.
Neither experiment establishes final component capacity for this contract.

The C3 arithmetic measurements in that report
also motivate the numeric investigation: the same 2,048-iteration double-arithmetic
kernel took a median 4,454 µs in native C and
5,593 µs in wasm3. This is evidence that removing dispatch overhead alone leaves
substantial arithmetic cost, not a measured fixed-point or `f32` speedup. Native C
was an unsandboxed reference with different compiler/checking overheads.

## Open design choices

Unspecified details include the opcode/binary format, frontend subsets,
numeric defaults, colour/alpha layout, event ordering and overload behaviour,
state migration/recovery, service-component isolation and board resource limits.
The interpreter direction is selected; these details remain open.
