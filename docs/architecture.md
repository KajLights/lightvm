# Multi-platform engine and native ports

> The implementation now uses the [portable bytecode control profile](control-api.md).
> The WASM-specific loader, guest imports and memory limits described below are
> historical. Current storage, cooperative servicing and asynchronous output
> contracts are documented in [control-api.md](control-api.md) and
> [configuration.md](configuration.md); the historical contracts below are not
> the current driver or upload specification.


The [portable bytecode architecture decision](design/architecture-decision-portable-bytecode.md)
selects the next runtime direction: multiple source-language frontends compile
off-device to portable typed bytecode, interpreted directly from flash with
bounded contexts and native buffer views. It records the staged animation API,
numeric policy, memory ownership, live replacement and open design choices.
The [component runtime requirements](design/architecture-request-component-runtime.md)
cover concurrent effects, independent live updates, communications plugins,
multicore execution and hardware offload. The contracts below describe the
current WASM implementation and earlier proposals, not the new bytecode ABI.

LightVM is intended to run portable lighting programs on multiple board families.
Its current unit of portability is the WASM module plus its versioned guest contract.
The implemented contracts are legacy animation API v1 and the independent
[control ABI 2 animation profile](control-api.md). The broader proposed control
API extends portability to event-driven device programs.
Each native firmware rebuilds the same engine/interpreter and supplies the local
hardware and services. Resource budgets and physical capabilities can differ.
An effect should query its bound pixel resource count (or legacy
`led.pixel_count()`) and avoid fixed board pin/count
assumptions; an oversized or unsupported module is rejected with a load/upload
error. Real-time frame throughput must be measured on each target.

The implemented ports are the POSIX host simulator, original RP2040 Pico, Pico W and a
shared ESP-IDF port for XIAO ESP32-C3, ESP32-S3 and ESP32-C6. The XIAO firmwares are
cross-build verified; hardware validation remains pending. Optional Wi-Fi/HTTP adapters coexist with USB; Matter and OTA remain subsequent
native adapters. See [networking](networking.md).

Multi-board support is a maintained requirement from the initial prototype.
Under the existing contract, compatible identical WASM bytes and API semantics
must work across host, Pico, C3, S3 and C6; native wiring/resource profiles may
differ. CI builds every firmware,
tests both host profiles and exercises a portable smoke runner. Record hardware
evidence separately from build success.

## Ownership and source boundaries

| Layer | Owns |
|---|---|
| Guest `.wasm` | Effect state and bounded control steps or legacy callbacks; logical RGB writes and own-step readback |
| `lightvm_core` / `src/` | Loader, wasm3 bindings, RGB/HSV conversion and buffer, scheduler, module lifecycle, limits, logical commands, byte framing |
| Native port / `platforms/<port>/` | Executable or SDK entry point, clock, allocation implementation, physical LED output, transports/event loop, SDK/toolchain setup |
| Board/resource header | Pixel capacity and engine budgets; board identity |
| Future native adapters | Wi-Fi, MQTT, Matter, provisioning and firmware OTA |

The core contains no GPIO, PIO, USB, POSIX or RTOS calls, nor platform-selection
branches. `pixels.c` always stores packed logical RGB and delegates physical output.
Animation effects run as bytecode procedures. All ports start from saved JSON
configuration; each device can name a startup procedure and supply initialization
arguments. Devices without startup remain idle.
The shared snapshot store delegates bounded block I/O to native ports.
See [configuration and persistence](configuration.md).

`src/config.h` includes the selected `LIGHTVM_BOARD_CONFIG` header and supplies
portable defaults. Board headers must contain constants and portable declarations,
without SDK includes. Adjustable engine constants are `PIXEL_CAPACITY`, `MODULE_LIMIT`,
`VM_HEAP_LIMIT`, `VM_STACK_SIZE`, `OUTPUT_LIMIT` and
`FRAME_INTERVAL_US`. Compile the core and entry point with the same header.
Native pin tables describe exposed capabilities; peripheral selection, count,
brightness and order belong to runtime JSON.
Current wasm3 builds enforce one 64 KiB linear-memory page and a function stack
height of 128; these interpreter limits are common to the present ports.

The proposed [build profiles](design/feature-proposal-build-profiles.md)
separate each instance's selected drivers, services, storage layout and budgets
from its board's physical capabilities. HTTP, MQTT and OTA should be optional
build components, with dependencies and capacity validated for the selected
profile. This selection mechanism is not yet implemented.

## Native service contract

Every port implements [platform.h](../src/platform.h):

| Service | Contract |
|---|---|
| `platform_info()` | Stable descriptor with short whitespace-free platform, board and output identifiers; diagnostic pin (-1 if inapplicable) and output brightness 0..255 |
| `platform_time_us()` | Monotonic 64-bit microseconds, advancing during idle waits and guest execution; used for deadlines and frame cadence |
| `platform_pins()` | Advertise exposed, usable board pin names and aliases |
| `platform_pixels_open/close/show` | Initialize/tear down a configured instance and submit complete packed RGB; return failures rather than abort |
| `platform_pixels_submit/poll/cancel` | Submit autonomous output, poll for completion/fault, and synchronously release all borrowed references on cancellation |
| `platform_storage_available/read/write/erase/sync` | Native bounded 512 KiB block store; 4 KiB erase and 256-byte program units; durable sync |
| `platform_calloc`, `platform_realloc`, `platform_free` | Compatible C allocation family with `max_align_t` alignment, zeroed calloc storage, overflow checks, and failed realloc leaving its old allocation intact |

Synchronous `show` consumes RGB before returning. Autonomous `submit` borrows an
immutable RGB frame until `poll` returns complete/fault or `cancel` releases it.
Pico/Pico W use PIO-paced DMA with retained wire-format SRAM, one DMA channel per
output, FIFO/shift-register drain and a reset-low guard. Stalled DMA is aborted
and the PIO is reset before retry. ESP32 uses asynchronous RMT, preferring DMA on
capable chips and falling back to ordinary RMT when DMA resources are unavailable.
The supported S3 has one DMA-capable RMT transmit channel; further outputs retain
ordinary asynchronous RMT. C3/C6 use ordinary asynchronous RMT.

Native services are ready before configuration initializes outputs with black
pixels. `show_us` measures elapsed time until output completion is observed; it
includes other rendering and polling delays for asynchronous output. It does not
measure CPU occupancy or wire duration. Output failure retains the last completed
snapshot and retries on later frames; initialization failure leaves active hardware
empty. See the [hardware measurements](../experiments/pico-async-output/README.md).

Allocation budget accounting, including headers, remains in `vm_allocator.c`.
The port can select an appropriate allocator or memory pool without changing the
engine. SDK allocations, C/RTOS stacks and static buffers are outside that budget.
Networking dynamic memory and PSRAM behavior still require hardware measurement.

`platform_identity()` supplies a stable device ID and a fresh boot nonce on every
port, including USB-only profiles. The IDs scope editor connections; they are not
authentication tokens. New native ports must implement this hook alongside their
other native services.

## Entry points and transport adapters

The platform owns initialization, blocking waits, native queues, watchdogs and
shutdown. For byte-stream transports:

```c
static App app;
static Session session;
/* Initialize native services first. */
app_init(&app, send_message, transport_context);
session_init(&session, &app, 1, send_message, transport_context);
/* In the sole engine owner's loop: */
app_tick(&app);
session_feed(&session, received_bytes, received_size);
/* On shutdown, before releasing transport/output resources: */
session_destroy(&session);
app_destroy(&app);
```

Optional [boot-sequence detection](boot-sequence.md) initializes before saved
startup configuration and is serviced first in `app_tick`. Native policy can query
`app_boot_sequence_triggered(&app)`; it is false in builds without
`LIGHTVM_BOOT_SEQUENCE`. Detection alone does not alter startup policy. Enabled
ports implement the independent `platform_boot_storage_*` block hooks using two
4096-byte erase sectors and 256-byte program units, with durable synchronization.

`send_message` receives a logical reply and the original transport context. A
serial/pipe adapter uses `write_frame` and handles native flushing. Decoder errors
and application replies use the same context. The host and Pico entry points show
the complete working loops.

A message-based transport can deliver validated commands through `app_dispatch`
and encode replies through `SendMessage`; it need not wrap them in serial framing.
With multiple transports, native code must retain the request's source and route
replies to it. The adapter owns transport authentication, ordering, buffering and
backpressure.

**One task owns engine execution.** The current executing-interpreter pointer,
and tracked allocator totals remain global. Module state and target buffers are
instance-owned. Calls to session/app functions
must be serialized; neither several concurrent Sessions nor reentrant callbacks
are supported. Future ESP32/RTOS network callbacks should enqueue commands for one
engine task. They must not invoke wasm3, mutate its module, change the RGB buffer
or enter the command handler concurrently. Reply/output callbacks must not reenter
the engine. Native blocking writes use their own transport deadlines.

Matter/MQTT lighting controls should feed a shared controller-state API when that
API is implemented. Vendor networking imports should not become prerequisites for
ordinary portable animations. Firmware OTA and provisioning stay native.
Snapshot storage is shared code over native block adapters. Transient uploads stay
in RAM; named procedure saves are independent from activation. Applying hardware
configuration destroys the instance, while RESET retains hardware and storage.

Control animation source receives its assigned contribution as `ctx.pixels`.
Manifest schema 3 describes RGB8 format and parameters, without a slot or
hardware name. Native deployment selects a slot (currently A). `control_step`
commits only that instance's reserved buffers; the scheduler's presenter owns
physical output. Multiple offscreen instances therefore have independent state
and contributions for subsequent scene/A/B composition.

## Proposed control model

The [control API and execution model](design/feature-proposal-control-api.md)
defines LightVM's next guest contract independently, with documented picolights
lineage. It separates resources/capabilities, properties/samples, actions/jobs,
events/triggers, tasks and clocks. Pixelblaze compatibility is a frontend or
adapter above that model. The contracts below continue to describe implemented
animation API v1.

The proposed core serializes event delivery and bounded guest steps. Native
interrupts and transport callbacks capture/enqueue work; only the engine owner
enters WASM. Input sampling, guest frame/control cadence and native PWM/servo
waveforms have independent schedules. A suspended task permits other work to
run and publishes its successful batch before waiting; a failing step cannot
roll back earlier commits or general guest state.

New resources declare snapshot/readback semantics, publication, units, ownership
and lifecycle. Addressable RGB frame steps start black; effects redraw their
current output from animation state. Alpha transparency and
[scene composition](design/feature-proposal-animation-composition.md)
will be designed separately. Completed scenes may later feed an A/B mixer,
subject to jointly advertised limits. Native brightness applies once after final
mixing. Ordinary actuators initially have an exclusive writer. The legacy adapter
keeps API v1's black-at-frame-start and callback/delta rules. General control
contexts, event records and task continuation layouts remain proposed; no
GPIO/ADC/PWM/servo support is implied by the current driver registry.

The [device proposal](design/feature-proposal-device-support.md) describes
definitions, native driver servicing and resource planning. Aggregate admission
must include event queues, tasks, subscriptions, timers, jobs, continuation state
and pixel buffers in addition to interpreter and native port storage. Layered
profiles also budget every colour instance, frame/staging/readback storage,
completed scenes and preparation peaks; neither two layer slots nor an A/B buffer
budget establishes capacity for their combination.

## API compatibility and capability discovery

The current animation contract is **API v1**, separately versioned from framed
**protocol v1**. It uses the same signatures on every current port:

- `render(index: i32) -> void` is required for animation playback.
- `beforeRender(delta_ms: i32) -> void` is optional and runs once before the pixels.
- `led.pixel_count() -> i32` returns the profile's logical pixel count.
- `led.rgb(r, g, b)` takes i32 channels 0..255, valid only during `render`.
- `led.hsv(h, s, v)` takes i32 values; hue wraps modulo 65,536 and s/v are 0..255,
  valid only during `render`.

Before each frame the buffer is black. Indices cover 0 through count-1 in order;
the last colour write wins for a pixel. Frames use integer millisecond deltas,
carry fractional milliseconds between scheduled frames, and cap long gaps at
1,000 ms. Invalid colour/context/signature or a guest trap prevents physical
output of that frame. Autonomous playback stops on failure, preserving the last
frame submitted to the backend. No guest linear-memory pixel array is required.

`console.write` and the limited WASI `fd_write`/`proc_exit` bindings remain shared.
Console output is allowed for explicit RUN/FRAME and PLAY initialization;
autonomous render output traps to keep transport responses usable. Unsupported
imports are rejected. API-breaking changes require changing the animation API
version; transport layout changes require changing the protocol version separately.

STATUS advertises `api`, `protocol`, `platform`, `board`, `output`, `pixels`,
`pin`, `brightness`, `frame_interval_us` and upload/heap/linear-memory limits,
alongside execution state and statistics. These describe the current core's
baseline capabilities; there is no advertised network/Matter/OTA support.
The desktop client uses the advertised module-size limit before beginning an
upload. Profiles can change resource capacity without changing API semantics.

## ESP32 native port

The separate `platforms/esp32/` SDK project selects `xiao_esp32c3`,
`xiao_esp32s3` or `xiao_esp32c6` through `LIGHTVM_BOARD`. It derives and validates the SDK target,
loads the corresponding portable header and keeps generated SDK configuration
in each board's build directory. `LIGHTVM_BOARD_CONFIG` can replace the resource/
wiring header. SDK/toolchain version 5.5.3 and wasm3 v0.5.0 are pinned; host/Pico
still use the repository's ordinary CMake entry point.

The IDF main component calls the shared core helper after SDK dependency
discovery. SDK compile options apply to the interpreter and engine libraries;
wasm3's intentional type aliasing is compiled with `-fno-strict-aliasing` on all
GNU/Clang native ports. No architecture branches enter the shared engine.

`app_main` owns one session on a 16 KiB native stack and yields every millisecond,
including under continuous input. Guest execution uses bounded instruction
slices that yield and resume, without an instruction cutoff or timed watchdog.
The port supplies `esp_timer_get_time`, ordinary C allocators, USB Serial/JTAG
and synchronous RMT output. PSRAM, radio stacks and automatic light sleep are
not enabled. USB pins are rejected as strip outputs. Native driver failures
are fatal through SDK checks because the current output contract has no error
return; recoverable output-failure reporting is separate work.

The RMT backend retains one scaled GRB buffer, emits 800 kHz bits plus a 300 µs
reset and waits for transmission before returning. Thus `show_us` includes
physical transmission/latch plus scheduling overhead. It uses one hardware
channel with the target's native memory-block size and requires no DMA.

USB uses raw driver bytes rather than translated stdio; console/boot logs are
disabled on the protocol channel. Partial writes are retried within a finite
deadline, and a failed reply suppresses further output until input resumes.
An interrupted transfer may require a new client connection; no infinite USB
wait is allowed to stop animation. The client's optional `--no-reset` mode
deasserts DTR/RTS before opening to avoid intended reset/download transitions.
It drains pre-request stale/ROM boot bytes until quiet, bounded to half a second.
Real OS/driver opening behavior still requires hardware verification.

See [README](../README.md) for exact build/flash commands and the dated validation
matrix. `tools/smoke_device.py` uses the same commands and identical WASM bytes
over serial or pipes, comparing explicit logical RGB frames to a matching host
profile. Serial runs additionally verify state through close/open and autonomous
playback while disconnected. This does not validate physical colour order,
wiring or real-time throughput without observed hardware checks.

## Building another port

For a port compatible with the repository's ordinary CMake executable, add a
directory with `board_config.h` and `CMakeLists.txt`. Optionally add
`pre_project.cmake` for SDK/toolchain imports before compiler detection. The port
manifest attaches native sources/libraries and output packaging to `lightvm`;
it is included in that target's CMake directory scope. Select it with
`-DLIGHTVM_PLATFORM=<name>`; `LIGHTVM_PLATFORM_DIR` can point to an external port.
Missing ports fail configuration explicitly.

SDKs that own the firmware entry point and build graph can reuse the core without
the repository's top-level project:

```cmake
# Within the native SDK's project/component setup:
set(LIGHTVM_BOARD_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/board_config.h")
set(WASM3_SOURCE_DIR "/absolute/path/to/wasm3-0.5.0") # optional offline dependency
include("/absolute/path/to/lightvm/cmake/lightvm_core.cmake")
lightvm_add_core()
target_link_libraries(your_native_firmware PRIVATE lightvm_core)
```

The native firmware supplies `platform.h` implementations and an SDK entry point
(for example ESP-IDF `app_main`). Native build flags/SDK configuration
are the port's responsibility; the shared build does not install an ESP32 SDK or
select a FreeRTOS task. Preserve distribution notices when packaging firmware.

Validate a new port by running the same uploaded modules, comparing logical RGB
frames against the host and checking unknown imports, upload/heap limits, timeout
recovery and allocation release. Then measure frame/output timing and memory with
its native services active. The test backend and 175-pixel host profile exercise
these boundaries without claiming ESP32 hardware support.


## Multiple command paths

Sessions borrow one App and own independent decoders and reply destinations.
`app_dispatch` scopes each command's sender and connection identity while the
engine owner executes it. IDs can overlap across paths. Session destruction
releases its staging ownership; it does not destroy the runtime. Direct message
adapters use `app_dispatch`, not concurrent calls into `app_command` or the VM.
Network callbacks enqueue requests. The native owner drains ingress and the
portable HTTP service captures bounded replies; native senders handle backpressure
without running the engine. Network configuration and the radio recovery state
machine live outside the guest ABI and hardware configuration lifecycle.
