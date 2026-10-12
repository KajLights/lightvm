# LightVM Web Editor Feature Proposal

Status: Proposed  
Date: 12 October 2026

LightVM should provide a browser editor for creating, previewing and deploying
lighting and device-control programs in several languages. Python, C, a
Scratch-like block editor and an optional Pixelblaze compatibility frontend
should use the same device connection and independently designed
[control API and execution model](feature-proposal-control-api.md).

The common model covers resources, properties and samples, actions/jobs, events,
triggers, resumable tasks and time. Addressable RGB pixels, digital/analogue IO, PWM and
servos use its shared conventions. Language syntax and compatibility rules belong
in frontends; Pixelblaze does not define the core ABI.
[Animation composition](feature-proposal-animation-composition.md) adds reusable
backgrounds and ordered foregrounds; [A/B transitions](feature-proposal-ab-transitions.md)
can mix their completed scene frames when both capabilities are available.

A public HTTPS hosting service should serve the full editor and compiler assets,
similar to CircuitPython's externally hosted editor. The user's browser downloads
those assets and runs the UI, compiler and preview locally, producing a portable
WebAssembly program.

The device provides the runtime and management API. USB devices expose that API
over USB without needing an HTTP server. Network-capable devices can additionally
run a small embedded HTTP API server for discovery, program transfer and controls.
Bluetooth can become another management transport; Matter should integrate
ordinary lighting controls through shared controller state.

[Build profiles](feature-proposal-build-profiles.md) select the drivers and
services included in each board instance's firmware. HTTP, MQTT and OTA are
optional even on boards capable of supporting them. The editor should offer
features according to the connected instance's advertised profile and limits.

## User experience

A user opens the public HTTPS editor site, chooses a language and starts from an
example or saved project. They can preview the effect before connecting a device,
then connect through USB or a local network address. The editor reads the device's capabilities
and checks the compiled program against its supported API and resource limits.

Deploying a valid program starts autonomous playback. Closing the browser or
disconnecting USB leaves playback running. Saving a program for startup after a
reboot is a separate action, available on devices with persistent storage.

Program controls such as speed, colour and sparkle density appear from a common
parameter schema. The same definitions expose buttons, sampled inputs, timed
actions and device events. Compilation errors point to source lines or blocks; device
errors explain unsupported features, resource limits or runtime failures.

Choosing another language starts or opens a project in that language. Automatic
conversion of arbitrary code between languages, or back into blocks, is outside
the initial scope.

Runtime configuration and compiled-procedure persistence are now implemented over
USB on host, Pico and ESP32. [Configuration management](../docs/configuration.md)
specifies staging, revisions, quotas and idle recovery. Editable source/project
storage and the HTTP mappings below remain proposed.
The [control ABI 2 Python frontend](../docs/control-api.md) is now available as a
desktop compiler and SDK with live parameters and passive snapshots. It supports
one RGB8 frame redrawn from black each step and six original examples. Browser authoring,
compiler workers and USB uploading are now available in the
[static Python editor prototype](../docs/web-editor.md). It saves a local browser
draft and downloads source/WASM; managed project/source storage and preview UI
remain future editor work.

## Reusable scenes and backgrounds

On composition-capable devices, a user can put Twinkle over Color, Rainbow or
Pattern, adjust each effect independently and change the background while the
stars continue. A standalone "stars on a dim colour" preset uses the same star
artifact with a Color background. Foregrounds should not require duplicated
background code or a different language binding for each combination.

Offer an ordered scene view with per-layer controls, contribution/completion
policy, hold/resume, reorder and remove actions. Basic composition overwrites
covered pixels; removing a foreground contribution reveals the current lower
scene. Retention and optional merge modes follow the composition capability.
The scene preview shows the final result, with optional layer/alpha inspection when designed and supported
for debugging. Report readiness, the failing layer and total resource use.

When A/B is also available, each source can be a scene. Compose its layers before
applying the separate transition mix. Keep scene editing, layer controls and
transition progress distinct; a persistent foreground does not require starting
or holding a transition. Lock structural edits during a take, while permitted
parameter changes identify the scene/layer generation.

Offer only modes and scene sizes supported by the connected profile. Show
combined capacity before deployment: fitting one two-layer scene does not imply
two scenes and a transition fit together. Scene controls arrive with the runtime
composition capability; the initial single-animation editor remains usable.

## Goals and scope

- Make lighting and device programming approachable to beginners while
  supporting text programming and optional source compatibility adapters.
- Keep programs portable across supported boards within advertised capabilities
  and resource limits.
- Let the editor and compiler evolve independently of native firmware releases.
- Provide one logical management API for every editor and transport.
- Preserve editable projects and distinguish temporary deployment from saving.
- Reuse independently authored backgrounds/foregrounds in bounded scenes where
  composition is available; integrate completed scenes with optional A/B playback.
- Support local compilation and USB use without a mandatory cloud account.

The initial release should deliver USB editing on the existing Pico and host
targets, with one usable language frontend and browser preview. Python, C,
blocks and an optional Pixelblaze compatibility frontend can follow in stages.
Network editing follows a native port with networking support. Full Python
compatibility, firmware OTA, collaborative editing and direct browser Matter
commissioning are outside the initial release.

## Current platform

LightVM already has legacy animation API v1, transport-independent commands,
USB CDC framing, resource reporting and RGB snapshots. The POSIX host and
original RP2040 Pico share the wasm3 interpreter and C engine. Configuration and
named compiled-procedure persistence are implemented. Networking, editable
project storage, provisioning and Matter adapters remain planned features.

The existing animation API requires `render(index: i32) -> void` and optionally
calls `beforeRender(delta_ms: i32) -> void`. Programs import `led.pixel_count`,
`led.rgb` and `led.hsv`. Physical wiring, wire order and brightness belong to the
native output backend. Unsupported imports are rejected. These callbacks remain
a compatibility contract, not the foundation of the proposed control ABI.
General triggers, suspended tasks and alpha composition are not implemented
by animation API v1. The current control ABI 2 also produces RGB-only frames;
alpha layers and A/B transitions remain separate future work.

The default engine profile limits uploads to 32 KiB, interpreter heap use to
128 KiB and WASM linear memory to one 64 KiB page. Its cooperative execution
deadline is 500 ms. These limits are a starting target for compiler design;
browser execution alone cannot establish that a program fits or renders fast
enough on a device.

| Current behaviour | Required extension |
|---|---|
| STATUS contains text fields | Structured identity, capability and compatibility descriptor |
| BEGIN unloads the previous program | Explicit upload and activation lifecycle with declared replacement guarantees |
| One active VM plus a persistent named module library, with no source readback | Add editable project/source packaging around the implemented procedure storage |
| RUN supports zero-argument exports | Typed program parameter updates through generated wrappers |
| FRAME pauses playback | Separate passive snapshots from explicit frame stepping |
| Autonomous console output stops playback | Optional bounded logs and runtime events |
| One Session owns an engine and fixed reply destination | Per-connection decoding and per-request reply routing into one engine owner |

The existing protocol can support an initial browser client. Its destructive
upload behaviour must be visible to the user until safer replacement is available.

## Architecture

There are three distinct locations:

| Location | Responsibility |
|---|---|
| Public HTTPS hosting service | Serve the full editor, compiler assets, examples and optional project services |
| User's browser | Run the editor UI, compiler worker, preview and device SDK; connect directly to the device |
| LightVM device | Execute uploaded WASM and expose the management API through USB and, where supported, a small embedded HTTP API server |

The proposed flow is:

```text
Public HTTPS hosting service
    editor and compiler assets
                |
            download
                |
User's browser
    Python / C / blocks / compatibility frontends
                |
       browser compiler worker
                |
       WASM program + manifest
          /                 \
 browser preview        device SDK
                             |
              direct USB / local HTTP / optional BLE
                             |
LightVM device
    USB interface / embedded HTTP API server / BLE service
                             |
                  shared device commands
                             |
                     LightVM runtime
```

Hosting the editor means serving its HTML, JavaScript and compiler binaries from
public infrastructure. Compilation executes in the user's browser; server-side
compilation would be a separate optional service. The public hosting service is
not in the browser-to-device transfer path.

This follows CircuitPython's arrangement: the device's `/code/` page loads the
full editor from `code.circuitpython.org`. LightVM's embedded HTTP API server can
provide a small launch page pointing to the public editor site. The full editor
and compiler distribution remains on the public hosting service.
See [CircuitPython web workflows](https://docs.circuitpython.org/en/latest/docs/workflows.html).

A browser worker keeps compilation and preview execution from blocking the
editor and allows runaway previews to be terminated. Cached assets can support
offline use after the editor has been loaded. USB operation does not require an
embedded HTTP server on the device or a server on the user's computer.

There are three independently versioned contracts:

1. **Compiler contract:** project input and target capabilities produce WASM,
   a manifest, diagnostics and source mappings.
2. **Management API:** identity, transfers, activation, controller state,
   parameters, storage and diagnostics.
3. **Control ABI:** execution contexts, event/task semantics, device capabilities
   and buffer formats available to guest programs. Legacy animation API v1 is
   an independently advertised compatibility contract.

The existing serial framing version remains separate. New management commands
do not require a framing change unless the wire format changes.

Devices advertise ABI signatures, WASM features and limits. The editor uses these
to determine which compiler frontends and projects can target the device; the
device does not need a Python or Pixelblaze parser.

## Language frontends

### Shared execution semantics

Every frontend targets the [control proposal](feature-proposal-control-api.md).
Its project declares bindings and triggers, required device capabilities,
buffer formats, clock policies and task/job/resource bounds. Source-level
functions, events and waits lower to typed operations and bounded execution
steps. The device has one WASM execution owner; no frontend may call guest code
from a physical interrupt or require a native thread per script.

Waits suspend one task and permit other work to run. Publish its successful
output batch before suspension, so a set-red/wait/set-black sequence displays
red while waiting. Continuation state can live in guest memory. The compiler
must diagnose unsupported suspension and excessive bounds rather than turning
wait into a blocking sleep. Exact step/context encodings remain control-ABI
implementation decisions.

Alpha composition bindings need a separate design before adding editor controls.
The current compiler exposes indexed RGB writes/readback and fill, with black at
the start of each frame. Animation state lives independently of frame storage.
Any future frame/filter binding must define alpha, readback and publication
consistently across frontends, and frame drawing must remain bounded.

Preserve explicit retrigger policies, event ordering, cancellation and lifecycle
behaviour. A frame callback is one trigger, not a compulsory entry point for
button, sensor or actuator programs. Numeric syntax may differ by frontend;
capability units and wire encodings must not.

### Python

Start with a documented lighting/control subset of Python: functions, numeric variables,
conditionals, loops, bounded arrays and selected lighting/device helpers. Reject
unsupported syntax with specific diagnostics. Source-level helpers should expose
natural Python names while compiling to the shared control ABI. Event and timer
decorators and resumable waits are explicit frontend features, with bounded
state-machine lowering where supported.

A full Python runtime compiled to WASM is a separate feasibility task, requiring
measurements of module size, memory use, imports and rendering throughput. A
Python runtime that works in browser WASM is not evidence that it fits the current
Pico profile.

### Pixelblaze

Implement an independent compatibility frontend with a published support matrix.
Its initialization, `beforeRender`, per-pixel callbacks, 16.16 numeric behaviour,
colour conventions, helpers and control exports lower to LightVM's own resources
and execution steps. A compiled wrapper can iterate pixels within a frame task;
the core need not adopt that callback contract. Pixel maps, sensors and other
features require corresponding advertised capabilities and explicit lowering.

Running unchanged scripts is a compatibility goal where supported, not a claim
that syntax resemblance establishes compatibility. Test representative patterns
and document unsupported APIs and semantic differences. Source compatibility
does not authorize copying upstream compiler, runtime or helper implementations.

### C

Generate typed declarations and context/result layouts from the same capability
definitions. C authors can write bounded event handlers or explicit state
machines using timers and task state. Ordinary blocking C functions do not become
suspendable simply because the ABI exposes waits. Preserve the existing
freestanding/WASI examples through their advertised execution contracts.

### Blocks

Use Blockly or a similar editor with device blocks and Scratch-like triggers:
on start, on press/release, on a message, every interval, on a frame, on a
threshold crossing and on job completion. Add waits, repeat/forever, cancellation
and broadcast-and-wait over the shared task model. Device descriptors supply
typed reporters and actions for colour, sampled values, duty and calibrated
servo targets.

Initially generate a supported language subset or shared intermediate
representation, including continuation state. Blockly supplies editing and code
generation facilities; LightVM defines execution semantics. A blocks project
records whether repeated triggers queue, restart, ignore an active script or
admit multiple instances. Those choices cannot remain implicit editor behaviour.
Future composition blocks will follow the separate alpha contract. A scene
arrangement binds independent effect artifacts; it must not silently turn them
into competing physical-output writers. Alpha defaults and opaque/transparent
black need clear examples after those semantics have been designed.

Preserve the block workspace as the project's authoritative source. Generated
text can be shown to help users learn, but editing that text should create a text
project unless a restricted reverse conversion is explicitly supported.

## Project and compiler format

A project should preserve its language and language version, source files or block
workspace, entry points, compiler version, parameter definitions and preview
configuration. Include device bindings, trigger definitions, task policies,
buffer formats and clock/pause semantics. It should be exportable as a file
without a cloud account.

Scene projects additionally preserve stable layer IDs, order, source artifact
references, per-layer parameters, contribution/completion policies and required
composition modes. Each source artifact retains its own language/version and
editable source or blocks. Preserve A/B scene bindings and transition artifacts
when present. Hardware pin configuration remains separate from scene structure.

Compilation of each source artifact produces:

- A `.wasm` program.
- A manifest identifying its required control/legacy ABI, imports, capabilities,
  WASM features, bindings, parameters and memory/task/event/job/buffer bounds.
- Diagnostics tied to source locations or block IDs.
- Source mappings or debug metadata where the toolchain supports them.

The scene manifest binds those artifacts by content hash and declares topology,
layer/readback dependencies, required composition capabilities and aggregate
resource needs. The device validates the actual combination, including its
preparation peak, before activation; per-artifact success is insufficient.

The manifest should identify the compiled artifact by a content hash and record
its source/compiler provenance. Compiler estimates of interpreter heap use and
speed are advisory. The device validates the actual WASM, including its imports,
signatures and memory declarations; it must not trust manifest assertions alone.

Source storage on the device is optional and subject to advertised quotas. The
editor must clearly show whether the device stores an editable project, only
compiled WASM, or neither persistently. A compiled program alone cannot recreate
its original source or blocks.

## Device management API

Define logical operations first, then map them to device HTTP routes and USB
commands. Every HTTP route below belongs to the device's embedded HTTP API server,
not the public editor hosting service. The browser sends requests directly to the
device's local address. HTTP uses structured JSON for metadata and binary bodies
for program bytes and RGB snapshots. USB can retain the existing binary framing;
larger descriptors need bounded fragmentation and reassembly beyond its 512-byte
payload limit.

| Operation | Proposed HTTP mapping | Behaviour |
|---|---|---|
| Describe | `GET /.well-known/lightvm` | Identity, API versions, capabilities and endpoint links |
| Read runtime | `GET /api/v1/runtime` | Active program, playback state, frame statistics and last error |
| Begin upload | `POST /api/v1/uploads` | Create a transaction with expected size and integrity metadata |
| Write chunk | `PUT /api/v1/uploads/{id}/chunks/{offset}` | Transfer bytes and acknowledge the accepted offset |
| Read upload | `GET /api/v1/uploads/{id}` | Query progress and validation state after interruption |
| Finish upload | `POST /api/v1/uploads/{id}/finish` | Check completeness, integrity and supported compatibility checks |
| Cancel upload | `DELETE /api/v1/uploads/{id}` | Release the transaction and staging resources |
| Activate program | `POST /api/v1/programs/{id}/activate` | Load and initialize the selected candidate |
| Playback action | `POST /api/v1/runtime/actions` | Play, pause or stop with explicit semantics |
| Controller state | `GET/PATCH /api/v1/controller` | Read/update power, brightness and selected program |
| Program parameters | `GET/PATCH /api/v1/parameters` | Read schema/values and submit typed updates |
| Snapshot | `GET /api/v1/frame` | Read the last successful logical RGB frame without pausing |
| Debug step | `POST /api/v1/runtime/step` | Pause and render with an explicit elapsed time |
| Events | `GET /api/v1/events` | Optional observable state, device/job outcomes, errors and bounded logs; does not consume guest events |
| Device action | `POST /api/v1/devices/{id}/actions/{action}` | Proposed typed action invocation with declared acceptance, retry and job semantics |
| Editable project | `GET /api/v1/programs/{id}/project` | Optional source/block readback |
| Save for startup | `POST /api/v1/programs/{id}/persist` | Optional explicit persistence operation |
| Scene definition | `GET/PUT /api/v1/scenes/{id}` | Optional ordered layer/artifact configuration with expected revision and explicit preparation/activation guarantees |
| Scene/layer action | `POST /api/v1/scenes/{id}/actions` | Optional prepare/activate, hold/resume or remove actions with layer identity/generation, retry rules and A/B lock checks |
| Scene snapshot | `GET /api/v1/scenes/{id}/frame` | Optional completed scene or colour/alpha inspection when supported with frame ID and validity |

These routes are a proposed contract, not existing endpoints. Unsupported
operations return a stable capability error. Polling remains available when event
streams are unsupported.

The descriptor should include a stable device ID shared across transports,
firmware version, management API and control/legacy ABI versions, supported import
signatures, WASM feature profile, resource limits, logical pixel count/topology,
transport limits and authentication method. Optional capabilities include
staging, rollback, persistence, source readback, parameters, snapshots and logs.
Clients should tolerate additive descriptor fields. Include device properties,
actions/event schemas, input quality, output ownership, supported retrigger
policies and event/task/job limits. A compiled driver and a configured instance
are separate facts.
Composition discovery includes scene/layer management version, formats,
alpha format/readback/blending rules, per-scene layers, total resident instances and
aggregate buffer/frame limits. A/B capability alone does not imply layered
sources; combined playback is admitted against actual resources.

Use the shared lifecycle rules: pause freezes program time and pausable work,
retains lighting and applies each actuator's advertised pause policy; native
servicing and safety deadlines continue. Stop cancels owned tasks/jobs/timers and
applies per-capability output policies while retaining the loaded program.
Lighting power-off blanks lighting without changing unrelated devices or losing
the selected program/brightness. Define its animation-time policy consistently
through every adapter. Transport disconnect leaves autonomous work running.

### Upload and activation guarantees

An upload has one owner, an ID, accepted offset, expiry and cancellation policy.
Repeated identical chunks are safe retries; conflicting chunks are rejected.
Other clients cannot overwrite an active transaction. Mutating requests need
defined retry behaviour; a correlation ID alone does not make an action safe
to repeat.

Advertise replacement guarantees explicitly. A device with a single RAM slot may
support only destructive replacement. A device with staging storage can receive
a candidate while the old program runs. Transfer validation, engine compilation
and guest initialization are distinct steps; activation may still fail after a
complete transfer.

Where resources permit, retain the previous program bytes until activation
succeeds and restore them on failure. This need not require two executing WASM
instances. Reloading the previous program resets its guest state unless state
restoration is separately supported. Firmware must not claim uninterrupted
playback, rollback or reboot recovery without implementing those guarantees.

Temporary deployment and persistent saving remain separate so live editing does
not write flash on every change. Persistent activation should preserve a valid
startup program across interrupted writes.

### Parameters and controller state

Controller state belongs to native firmware. Program parameters belong to the
active program and have stable IDs, types, defaults, ranges and UI hints. Colour
parameters and trigger actions need explicit schemas, rather than being inferred
from untyped export names at runtime.

Compiler-generated wrappers translate common parameter updates into each
language's semantics. Apply updates between frames through the engine owner;
guest code and transport callbacks must not mutate interpreter state concurrently.
Parameter requests identify the active program or generation so stale requests
from a previous program cannot update its replacement.
For composed sources, requests also identify scene and layer; changing one
effect's parameters must not restart or overwrite another effect's state.
Scene structure changes are revision-checked at complete frame boundaries and
rejected while locked by an A/B take. Background replacement follows the declared
preparation/replacement guarantee rather than promising uninterrupted playback
on every profile.

HTTP, USB, MQTT and Matter should use the same native controller state and receive
consistent resulting state. They should not implement independent brightness,
power or program-selection rules.

### Diagnostics and responsiveness

Return structured errors such as `incompatible_abi`, `unsupported_import`,
`resource_limit`, `upload_conflict`, `runtime_trap` and `not_supported`, with
human-readable detail. Distinguish compiler errors, upload failures, activation
failures and autonomous rendering errors.

Snapshots are passive reads; debug steps intentionally alter playback. Logs use
bounded buffers, rate limits and drop counters so a disconnected or slow browser
cannot stall rendering. Source-level runtime locations depend on toolchain and
interpreter support; retain useful raw trap details when mapping is unavailable.

Set execution and rendering budgets to preserve device responsiveness. The
current 500 ms deadline is a recovery bound, not an acceptable frame-time target.
Transport writes and queues also need bounds because native blocking I/O is
outside the guest deadline.

## Discovery and browser access

### USB

Use Web Serial for the existing USB CDC firmware. The browser selects a port
through a user gesture, opens a binary stream, handles DTR and implements the
current framing, checksums and acknowledgements. After connecting, it queries
the descriptor to confirm LightVM identity and compatibility. Reconnect handling
must distinguish a continuing session from a device reboot.

Web Serial requires a secure context and browser permission. It is supported by
Chromium browsers and Firefox desktop from version 151. Browser and operating
system support should be checked at connection time. A dedicated WebUSB
interface can be considered later if it offers a concrete benefit.

### HTTP

On network-capable devices, the embedded HTTP API server exposes discovery and
management endpoints. Advertise a proposed `_lightvm._tcp` mDNS service, a stable
`.local` hostname and the descriptor path. mDNS identifies the device's service
address; the descriptor establishes its API and capabilities.

An ordinary webpage cannot directly enumerate mDNS services. Provide manual
device-address entry in the public editor and an optional small device launch
page with an Open Editor link that passes the device address to the public HTTPS
editor site. A device can additionally expose discovered peers through an HTTP
endpoint, following CircuitPython's discovery approach.

The editor code running in the browser connects directly to the device's local
HTTP API. This crosses origins from the public editor site to the device, so it
needs compatible browser local-network policy, device CORS responses and device
authentication. Chrome's Local Network Access permission relaxes mixed-content
blocking for some local HTTP requests, but this does not establish universal
browser support. Validate the direct connection workflow early; the device launch
page does not by itself guarantee that browser access will succeed.

If a supported browser cannot connect directly, USB remains available where
supported. An optional bridge running on the user's computer or local network
could provide additional discovery and connection options. That bridge is a
separate component from both the public hosting service and the device's embedded
HTTP API server. The public hosting service cannot directly reach a user's private
network without a bridge or device-initiated connection.

### Bluetooth and Matter

A custom BLE GATT service can expose the same logical management operations on
capable boards. It needs transfer fragmentation, acknowledgements and flow
control appropriate to the negotiated connection. Web Bluetooth requires
permission and has limited browser/platform coverage, so it is optional.

Matter should initially expose ordinary lighting controls through native
controller state. Program upload remains on USB, HTTP or the optional BLE service.
Direct browser Matter support needs more than Web Bluetooth: commissioning and
operational communication require a Matter stack and appropriate networking.
A local Matter controller or bridge is the practical integration route.

## Native engine changes

Retain one owner for interpreter execution, module lifecycle and resource state.
Each connection gets its own byte decoder or message adapter, while requests
enter a bounded queue carrying their source, identity and reply destination.
Disconnecting a transport must not destroy the running program or leave queued
requests holding invalid transport pointers.

Separate engine state from connection/session state before allowing HTTP and
USB together. Keep authentication and transport details native; ordinary
programs should not import network or vendor APIs. Inputs and API invocations
enter the shared bounded event/task scheduler; physical waveform generation
remains native. Preserve animation API v1 through its adapter and version the
independent control contract and capabilities explicitly.

## Preview and validation

Preview should execute the compiled artifact with the same event ordering,
clock contexts, snapshots, task suspension/retrigger policies, publication and
colour composition as the device. Use the target capabilities, resource bounds
and pixel topology. Simulated buttons, analogue samples, messages and job
outcomes form a timestamped replay trace. Distinguish commanded actuator state
from measured feedback and logical colour from physical brightness.
For future scenes, replay alpha blending, opaque and transparent black,
independent layer clocks and one completed final publication. A/B preview first composes each scene and then applies transition
coverage, including exact endpoints and preserved destination layer state.

Legacy animation preview retains API v1 callback/delta/colour rules. A later
wasm3 browser build can improve engine parity if justified; native browser WASM
can supply the initial preview, but must run the same logical scheduler model.

Device acceptance remains authoritative. Browser preview does not reproduce
wasm3 heap allocation, interpreter performance or hardware output timing.
Compare event traces, published output batches and deterministic colour frames
against the host engine. Measure input latency, native waveforms, frame timing
and resource use on each board with its native services active.

## Delivery phases

| Phase | Deliverable | Acceptance criteria |
|---|---|---|
| 1 | Descriptor, TypeScript USB SDK, browser preview and one text frontend | Connect to the Pico, compile/preview/upload an effect, report errors, and continue playback after disconnect; expose legacy replacement and explicit persistent procedure/configuration saving |
| 2 | Project import/export, staged activation where supported, controller state and parameters | Preserve editable source, adjust controls during playback, reject stale updates, and verify declared replacement guarantees under failed uploads and activation |
| 3 | Remaining language frontends, device/lighting blocks and scene editing where supported | Bindings share the control ABI, triggers/tasks and parameter schema; verify waits, repeated events, actuator ownership, reusable backgrounds, the separately designed alpha/readback contract and A/B scene composition; publish compatibility limits |
| 4 | Network-capable native port and embedded HTTP API server | Demonstrate the public editor connecting directly to the device, discovery/device launch page, pairing and concurrent USB/HTTP request routing on supported browsers and hardware |
| 5 | Optional persistence, BLE and Matter integration | Verify reboot recovery and interrupted saves where supported; validate BLE transfers and consistent Matter/controller state |

Select the first frontend with a small compiler feasibility prototype against
LightVM's own control semantics and board budgets. Do not select the core ABI
to match a source language. Blocks can follow a stable compiler path that can
express resumable tasks; Pixelblaze compatibility is independently staged.

Cross-phase validation should cover partial transfers, retries, disconnects,
oversized modules, unsupported imports, runtime timeouts, slow clients, competing
writers and stale parameters. Network tests should include allowed origins,
authentication and browser permission failures. No capability is advertised until
its implementation and failure behaviour are verified.

## Decisions before implementation

- Choose the first language frontend and define the initial Python/Pixelblaze
  compatibility boundaries.
- Choose shared compiler representation and source mapping conventions.
- Fix the shared control step/context contract, typed parameter/event encodings,
  continuation bounds, pixel formats, separately designed alpha composition/readback and
  separate A/B mix semantics.
- Define scene packaging, layer identities and aggregate preparation/admission
  alongside single-artifact uploads; align controls with runtime delivery.
- Establish replacement guarantees feasible on the Pico and future network boards.
- Select project packaging, device storage quotas and startup recovery policy.
- Select public HTTPS hosting for the editor and compiler assets.
- Define the first supported browser/platform matrix and direct browser-to-device
  HTTP pairing workflow.

## References

- Existing LightVM baseline: `README.md`, `docs/architecture.md`, `src/app.c`,
  `src/session.h`, `src/protocol.h`, `src/config.h` and `tools/client.py`.
- [CircuitPython workflows and device discovery](https://docs.circuitpython.org/en/latest/docs/workflows.html).
- [Pixelblaze language reference](https://electromage.com/docs/language-reference/).
- [Control API, prior art and picolights provenance](feature-proposal-control-api.md).
- [Animation composition and reusable backgrounds](feature-proposal-animation-composition.md).
- [A/B slots and scripted transitions](feature-proposal-ab-transitions.md).
- [Blockly code generation](https://docs.blockly.com/guides/create-custom-blocks/code-generation/overview/).
- [Web Serial and connection signals](https://developer.chrome.com/docs/capabilities/serial).
- [Firefox desktop Web Serial support](https://blog.mozilla.org/en/firefox/firefox-web-serial-adafruit/).
- [Chrome Local Network Access](https://developer.chrome.com/blog/local-network-access).
- [Web Bluetooth access and permissions](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).
- [Matter JavaScript platform requirements](https://github.com/matter-js/matter.js#using-matterjs-in-unsupported-javascript-environments).

Browser support statements reflect the referenced documentation as of
12 October 2026 and should be rechecked when choosing a release support matrix.
