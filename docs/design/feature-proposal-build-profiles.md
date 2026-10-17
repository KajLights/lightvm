# LightVM Build Profiles Feature Proposal

Status: Proposed

Date: 12 October 2026

LightVM should let users choose the drivers and native features included in
firmware for a particular board instance. HTTP, MQTT, OTA and individual device
drivers should be selectable at build time, according to the user's needs and
the installation's flash and RAM capacity.

Two instances of the same board model may need different firmware. One may run
offline over USB with a few basic devices; another may use MQTT and OTA while
omitting the HTTP management server and unused drivers. Support for a feature
on a board establishes that it can be selected, rather than requiring every
instance of that board to include it.

This complements the [control API](feature-proposal-control-api.md),
[device support](feature-proposal-device-support.md) and
[web editor](feature-proposal-web-editor.md) proposals. The profile format,
feature catalogue and build options below are proposed, not implemented.

## Configuration layers

Keep the following layers distinct:

| Layer | Responsibility |
|---|---|
| Native port | SDK, processor support and native implementations available to the build |
| Board definition | Physical capabilities, pin aliases, reserved resources and available flash/RAM |
| Build profile | Drivers, services, transports, storage layout and resource limits selected for an instance's firmware |
| Hardware configuration | Named peripheral instances, wiring and settings using drivers present in that firmware |
| Program execution contract | One selected current guest contract and its declared capabilities |

A build profile is reusable, but its assignment is a choice for each physical
board instance. Selecting a profile for one XIAO ESP32-S3 must not change the
default for all XIAO ESP32-S3 boards. Deployment records should associate a
device's stable identity with its selected profile and firmware artifact.
Profiles need no credentials or hard-coded device serial numbers.

Runtime configuration can instantiate an included driver or enable an included
service. Adding an excluded driver or service requires rebuilding and installing
native firmware. Uploading current bytecode remains independent of that process.

## Selectable features

Retain a small shared runtime and select optional components through a common
feature catalogue:

| Selection | Examples |
|---|---|
| Device drivers | WS2812B/RGBW, digital input/output, analogue input/output where supported, PWM, tone, servo, individual I2C sensors and displays |
| Control capabilities | Addressable pixel buffers, bounded animation composition, A/B mixing, event capture, timers, bounded tasks/jobs and optional feedback/stream operations |
| Management transports | USB, embedded HTTP API, optional BLE management |
| Network services | MQTT, Matter, discovery and provisioning |
| Native dependencies | Wi-Fi or another supported network interface, TLS and shared bus drivers |
| Storage and updates | Persistent programs, editable source storage and firmware OTA |
| Development conveniences | Native boot demo, diagnostic logs and optional debug operations |

Select [animation composition](feature-proposal-animation-composition.md) and
[A/B transitions](feature-proposal-ab-transitions.md) independently. A minimal
build can run one animation; a composition build can run Rainbow plus Stars
without a transition program; an A/B build can switch between single-animation
sources without general layer management. Combining them permits layered scene
sources only within the declared joint limits.

Composition depends on indexed frame buffers and the shared scheduling,
publication and ownership machinery. Alpha representation and blending are
implemented for the current scene contract; profile options must match its aggregate limits.
Resolve limits for layers per scene, total resident colour/transition instances,
frame/alpha/staging bytes and the aggregate frame budget. Feature selection must
not imply arbitrary layer capacity or additional output formats.

Excluding a feature must remove its registration entries, native implementation,
dedicated buffers and dependencies that no remaining feature needs. A runtime
flag that leaves the code and reserved memory in the firmware does not satisfy
this requirement. Shared dependencies remain when another selected feature
requires them.

HTTP management, MQTT and OTA should be independently selectable, subject to
their actual dependencies. An MQTT installation need not run an HTTP management
server. OTA needs an update transport and storage scheme supported by the port;
an HTTPS download client does not imply an embedded HTTP server. Conversely, a
networked instance may deliberately omit OTA to retain more application space.

The catalogue should record each feature's stable ID, supported ports/boards,
dependencies, conflicts, build sources, SDK dependencies, registration hooks
and resource requirements. Device definitions can supply driver metadata to
this catalogue rather than maintaining a second list of device types.

Validate the complete selection before compiling. Reject unknown IDs,
unsupported features, conflicting choices and explicitly disabled required
dependencies. Show the resolved dependency set and explain what requires each
component. Do not silently enable a feature the user excluded, or downgrade a
selected transport's authentication or encryption to make a build fit.

## Profile format and build selection

Use a versioned, declarative profile file with a readable ID, explicit feature
choices, selected drivers, resource limits and optional storage/update settings.
The build resolves it against the separately selected board definition. An
illustrative profile for an MQTT installation on a network-capable board is:

```yaml
schema: lightvm.build-profile/1
id: hallway-mqtt-ota
features:
  usb_management: true
  wifi: true
  http_api: false
  mqtt: true
  tls: true
  ota: true
  program_storage: false
  source_storage: false
  ble_management: false
  matter: false
drivers:
  - ws2812b
  - digital-input
  - digital-output
ota:
  transport: https-pull
  layout: dual-slot
limits:
  module_bytes: 32768
  vm_heap_bytes: 131072
```

These names and limits illustrate the contract; they do not establish that the
selection fits a particular board. The OTA layout reserves its own update
metadata even when persistent program storage is excluded. Pin choices, pixel
count and network credentials belong to the appropriate hardware or provisioning
configuration.
Profiles enabling composition or A/B must also resolve their scene/layer,
resident-instance and buffer limits. Validate any declared simultaneous scene
capacity against those aggregate limits. A per-scene maximum does not promise
that every A/B source can use that maximum together; individual projects still
require runtime admission against their actual needs.

Supply a few starter profiles, such as USB minimal, HTTP managed and MQTT with
OTA, then let users save their own selections. Presets should be starting points
for instance choices. Resolve omitted values through documented, versioned
defaults and save every resulting choice in the artifact manifest.

A proposed `LIGHTVM_BUILD_PROFILE` option should select the file for both the
host/Pico CMake entry point and native SDK projects. Generate consistent compile
definitions, registration tables, SDK configuration and partition settings from
the resolved profile. The shared engine and native adapters must see the same
selection; SDK-specific configuration stays in the native port.

Use a separate build directory for each board/profile combination and detect
stale configuration when switching profiles. Use the current firmware feature selection as an explicit starter profile.
Custom profile files should be usable outside the repository.

## Capacity planning and OTA

Check the resulting firmware against the selected storage layout. Total flash
capacity alone is insufficient: an OTA scheme may reserve bootloader space,
update metadata, a second application slot or a staging region alongside program
and configuration storage. Every required application slot or staging region
must accommodate its intended image, including headers, alignment and selected
verification data. Report usable image capacity and remaining headroom.

Choose layouts supported by the native port and board. Enabling OTA must select
an explicit update strategy and its interruption, recovery and rollback
guarantees. Reject an unsupported strategy or an image that exceeds its usable
capacity. Do not silently fall back to an in-place update when the chosen scheme
does not fit. Omitting OTA should allow a supported layout with more space for
the application.

RAM planning must include static data, engine and guest budgets, native task
stacks, device/pixel buffers, event queues, subscriptions, guest tasks,
timers/jobs, continuation storage, networking/TLS allocations and update buffers. The
existing interpreter heap cap is only one part of the total. Report linker
measurements separately from declared dynamic bounds and measured peaks under
service load. Reserve explicit operating headroom and reject configurations
that exceed the board's usable resources. Unknown dynamic costs must remain
visible rather than being presented as a proven fit.

Emit a capacity report alongside each artifact: image size, application/staging
limits, storage reservations, static RAM, declared dynamic budgets and remaining
headroom. Identify the largest optional components where the toolchain can
attribute their cost. This lets users decide whether to drop HTTP, MQTT, OTA or
drivers to meet their needs.

Changing a profile may change the partition layout or bootloader requirements.
An instance without OTA needs a supported local flashing path to install an
OTA-enabled profile. A layout transition may also require local flashing or an
explicit migration procedure even when the current firmware supports OTA.

## Artifact identity and capability discovery

Record the profile ID and a hash of its resolved content in the firmware and
artifact manifest. Include the board/port, firmware revision, SDK/toolchain
versions, selected features/drivers, effective limits and storage layout.
Use board and profile identity in artifact names so builds for two installations
of the same board are distinguishable. Profile identity describes the build;
firmware authenticity needs the update mechanism's own verification.

The device should advertise the capabilities actually compiled into its
firmware, together with profile identity and effective resource limits. Keep
compiled driver types separate from configured peripheral instances and service
availability: an included network adapter may still need provisioning.

The web editor should use this descriptor when offering transports, hardware
forms, persistence and firmware updates. Explain an excluded feature as
requiring another firmware profile. Required imports and device capabilities
must be checked against the connected instance, rather than assumed from its
board model. Select one current guest and management contract; reject superseded PoC interfaces.

Before a profile-changing update, compare the candidate manifest with the
physical board, current boot/storage layout, hardware configuration and saved
program requirements. Report drivers or APIs that would disappear and require
an explicit migration or safe deactivation of affected configuration/programs.
Removing a management transport must make the remaining access and recovery
path clear. Accept a different profile ID when those compatibility checks pass;
users must be able to change their choices over time.

## Current platform and implementation path

Today, `LIGHTVM_PLATFORM`, `LIGHTVM_BOARD` in the ESP32 project and
`LIGHTVM_BOARD_CONFIG` select native ports and board/resource headers. The shared
core has a fixed source list; the output driver and management transport are
selected by the native port. ESP32 defaults use a single application layout.
HTTP management is implemented. There is no general instance build profile, MQTT or OTA
service. Build profiles should make those future additions optional from their
first implementation.

Start with profile parsing, validation, a resolved manifest and the current
baseline firmware. Add conditional driver and adapter registration as those
components arrive. Integrate capacity reports and OTA layout selection before
advertising OTA profiles. A graphical profile chooser or hosted build service
can follow the file-based workflow; neither is required initially.

## Acceptance criteria

- Build two different profiles for the same board model in separate directories,
  without modifying the board definition or each other's artifacts.
- Verify excluded components are absent from registration tables, linked symbols
  and dedicated memory allocations, including unused SDK dependencies.
- Demonstrate USB-only, HTTP without MQTT/OTA, MQTT without an HTTP server and
  OTA-disabled builds as their adapters become available. Verify OTA with a
  supported transport can omit the HTTP management server.
- Reject unknown features, disabled dependencies, unavailable drivers,
  incompatible layouts and excessive flash/RAM reservations with useful errors.
- Reject firmware that fits total flash but exceeds the selected OTA image
  capacity. Exercise interrupted updates and the declared recovery guarantees.
- Confirm capability discovery, editor choices and program/configuration
  validation match each instance's actual build.
- Exercise single-animation, composition-only, A/B-only and combined profiles
  as those capabilities arrive. Omitted features reserve no dedicated buffers;
  advertised layer/mode/instance limits match runtime admission.
- Account for all layer colour/alpha, staging, filter inputs and scene frames
  in combined RAM and frame budgets; the minimal A/B `10N` storage figure must
  not be reported as the cost of layered scenes.
- Rebuild from the same resolved profile and pinned inputs with identical feature
  selections, limits and layout; record any toolchain limits on binary identity.
- Run identical current bytecode on different profiles with unchanged import
  semantics, and measure timing/RAM while selected native services are active.

Before implementation, settle the profile schema/default rules, feature
catalogue ownership, supported storage strategies and how ports account for
dynamic memory. Keep board support and user-selected firmware composition
separate throughout that work.
