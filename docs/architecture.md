# Multi-platform engine and native ports

LightVM is intended to run portable lighting programs on multiple board families.
Its unit of portability is the WASM module plus the versioned animation API.
Each native firmware rebuilds the same engine/interpreter and supplies the local
hardware and services. Resource budgets and physical capabilities can differ.
An effect should query `led.pixel_count()` and avoid fixed board pin/count
assumptions; an oversized or unsupported module is rejected with a load/upload
error. Real-time frame throughput must be measured on each target.

The implemented ports are the POSIX host simulator and the original RP2040 Pico.
XIAO ESP32-S3 and ESP32-C6 are planned targets. This separation prepares their
ports; it does not implement ESP-IDF, networking, Matter or OTA.

## Ownership and source boundaries

| Layer | Owns |
|---|---|
| Guest `.wasm` | Effect state, optional `beforeRender`, per-pixel `render`, logical colour requests |
| `lightvm_core` / `src/` | Loader, wasm3 bindings, RGB/HSV conversion and buffer, scheduler, module lifecycle, limits, logical commands, byte framing |
| Native port / `platforms/<port>/` | Executable or SDK entry point, clock, allocation implementation, physical LED output, transports/event loop, SDK/toolchain setup |
| Board/resource header | Pixel count and engine budgets; native wiring, brightness and board identity |
| Future native adapters | Wi-Fi, MQTT, Matter, provisioning, persistence and firmware OTA |

The core contains no GPIO, PIO, USB, POSIX or RTOS calls, nor platform-selection
branches. `pixels.c` always stores packed logical RGB and delegates physical output.
The native rainbow's colour generation is shared; starting it at boot is Pico
entry-point policy. The host starts idle.

`src/config.h` includes the selected `LIGHTVM_BOARD_CONFIG` header and supplies
portable defaults. Board headers must contain constants and portable declarations,
without SDK includes. Adjustable engine constants are `PIXEL_COUNT`, `MODULE_LIMIT`,
`VM_HEAP_LIMIT`, `VM_STACK_SIZE`, `OUTPUT_LIMIT`, `RUN_TIMEOUT_US` and
`FRAME_INTERVAL_US`. Compile the core and entry point with the same header. Native
constants such as Pico's `PIXEL_PIN` and `PIXEL_BRIGHTNESS` are consumed by its port.
Current wasm3 builds enforce one 64 KiB linear-memory page and a function stack
height of 128; these interpreter limits are common to the present ports.

## Native service contract

Every port implements [platform.h](../src/platform.h):

| Service | Contract |
|---|---|
| `platform_info()` | Stable descriptor with short whitespace-free platform, board and output identifiers; diagnostic pin (-1 if inapplicable) and output brightness 0..255 |
| `platform_time_us()` | Monotonic 64-bit microseconds, advancing during idle waits and guest execution; used for deadlines and frame cadence |
| `platform_show_rgb(rgb, count)` | Receive a complete successful frame of packed logical RGB; apply physical brightness and wire order in the backend |
| `platform_calloc`, `platform_realloc`, `platform_free` | Compatible C allocation family with `max_align_t` alignment, zeroed calloc storage, overflow checks, and failed realloc leaving its old allocation intact |

The output backend must consume or copy RGB before returning. A DMA backend needs
its own retained buffer because the core reuses the logical buffer on the next
render. Native initialization must complete before `session_init`, which emits a
black frame. `show_us` measures the output callback's duration; an asynchronous
backend should document how that relates to physical transmission. Driver failure
reporting is not yet part of this synchronous output interface.

Allocation budget accounting, including headers, remains in `vm_allocator.c`.
The port can select an appropriate allocator or memory pool without changing the
engine. SDK allocations, C/RTOS stacks and static buffers are outside that budget.
Neither networking memory nor PSRAM behavior is validated by the existing ports.

## Entry points and transport adapters

The platform owns initialization, blocking waits, native queues, watchdogs and
shutdown. For byte-stream transports:

```c
static Session session;
/* Initialize native services first. */
session_init(&session, send_message, transport_context);
/* In the sole engine owner's loop: */
session_tick(&session);
session_feed(&session, received_bytes, received_size);
/* On shutdown, before releasing transport/output resources: */
session_destroy(&session);
```

`send_message` receives a logical reply and the original transport context. A
serial/pipe adapter uses `write_frame` and handles native flushing. Decoder errors
and application replies use the same context. The host and Pico entry points show
the complete working loops.

A message-based transport can deliver validated commands through `app_command`
and encode replies through `SendMessage`; it need not wrap them in serial framing.
With multiple transports, native code must retain the request's source and route
replies to it. The adapter owns transport authentication, ordering, buffering and
backpressure.

**One task owns engine execution.** The current executing-interpreter pointer,
tracked allocator totals and RGB buffer are global. Calls to session/app functions
must be serialized; neither several concurrent Sessions nor reentrant callbacks
are supported. Future ESP32/RTOS network callbacks should enqueue commands for one
engine task. They must not invoke wasm3, mutate its module, change the RGB buffer
or enter the command handler concurrently. Reply/output callbacks must not reenter
the engine. Native blocking writes are outside the cooperative guest deadline.

Matter/MQTT lighting controls should feed a shared controller-state API when that
API is implemented. Vendor networking imports should not become prerequisites for
ordinary portable animations. Firmware OTA, provisioning and storage stay native;
animation uploads remain independent program data. The current upload slot is in
RAM and is lost on reboot.

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
(for example a future ESP-IDF `app_main`). Native build flags/SDK configuration
are the port's responsibility; the shared build does not install an ESP32 SDK or
select a FreeRTOS task. Preserve distribution notices when packaging firmware.

Validate a new port by running the same uploaded modules, comparing logical RGB
frames against the host and checking unknown imports, upload/heap limits, timeout
recovery and allocation release. Then measure frame/output timing and memory with
its native services active. The test backend and 175-pixel host profile exercise
these boundaries without claiming ESP32 hardware support.
