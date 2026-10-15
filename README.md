# LightVM

LightVM is a [Kajlights](https://kajlights.com/) project developing a portable
runtime for programmable lighting across microcontrollers and native hosts, under the
[Kajlights GitHub organisation](https://github.com/Kajlights).

The goal is one portable, language-neutral control API and identical program
packages across supported boards, within each board's resource limits. The current
implementation distributes `.wasm` programs. The implemented guest contracts are
legacy animation API v1 and the independent
[control ABI 2 animation profile](docs/control-api.md), with a restricted Python
compiler, typed live parameters and RGB8 frames redrawn each step. The shared C engine and
wasm3 interpreter are rebuilt for each processor; native ports supply the clock,
LED output, memory allocation, transports and startup policy. Adding a board
should not require board-specific imports or pin assumptions in an effect.

The [portable bytecode architecture decision](documents/architecture-decision-portable-bytecode.md)
selects the next component-runtime direction: multiple source-language frontends,
off-device compilation to shared typed bytecode, direct interpretation from flash,
and explicit state and buffer budgets. It also defines the staged animation API
direction and numeric policy for these microcontrollers. This is planned work;
the WASM upload paths and existing API semantics documented below remain current.

The Pico and preliminary ESP32 firmwares receive a `.wasm` file over USB CDC,
loads it with wasm3, calls an exported function, and returns console bytes,
an integer result and execution status. The firmware contains no sample WASM.
Flash the firmware once; upload and replace programs as independent data.

WASM core has no stdout or native string return type. This project supplies:

- `console.write(offset, length)`: output bytes from WASM linear memory.
- WASI Preview 1 `fd_write`: stdout/stderr output through the same console stream.
- WASI Preview 1 `proc_exit`: a guest exit code returned in the status reply.
- A callable export such as `run() -> i32` or `_start() -> void`.

Console bytes are preserved, including NULs and line endings. The sender writes
them to its stdout and reports execution status to stderr. Returning a string
would require an offset/length convention; console output is the smaller API.

## Platforms and architecture

| Port / board | Current support |
|---|---|
| POSIX host (macOS/Linux) | Implemented: pipes, monotonic clock, logical RGB snapshots and integration tests |
| Original RP2040 Pico | Implemented: USB CDC and WS2812 PIO output; previous hardware checks are recorded below |
| RP2040 Pico W | Implemented: USB, optional Wi-Fi/HTTP and WS2812 PIO; radio hardware validation pending |
| Seeed XIAO ESP32-C3 | USB, Wi-Fi/HTTP and web-editor WASM execution verified on a board; WS2812 RMT implemented, physical strip check pending |
| Seeed XIAO ESP32-S3 | Preliminary: ESP-IDF cross-build verified; USB Serial/JTAG and WS2812 RMT implemented; hardware validation pending |
| Seeed XIAO ESP32-C6 | Preliminary: ESP-IDF cross-build verified; USB Serial/JTAG and WS2812 RMT implemented; hardware validation pending |

| Location | Responsibility |
|---|---|
| `src/` / `lightvm_core` | WASM loading/bindings, animation scheduling, RGB/HSV, allocation budgets, commands and framing |
| `src/platform.h` | Native service contract: identity, monotonic clock, RGB output and allocation hooks |
| `src/session.h` | Shared session lifecycle and byte decoding; driven by a native event loop |
| `platforms/host/` | POSIX entry point, pipes, clock and host memory allocation |
| `platforms/pico/` | Pico entry point, SDK bootstrap, USB, PIO and reserved flash storage |
| `platforms/esp32/` | ESP-IDF project, three XIAO profiles, USB Serial/JTAG, RMT output and optional Wi-Fi/HTTP and one FreeRTOS engine owner |
| `cmake/lightvm_core.cmake` | Reusable engine build for ports with their own SDK project/entry point |

The command handler has a transport-independent `SendMessage` callback.
`session_feed` accepts framed bytes; message-based adapters can call `app_command`
directly. Native networking callbacks must queue work for the engine owner. There
is currently one resident `VmInstance` and one owned RGB target. The execution
pointer and aggregate allocator accounting remain serialized; concurrent engine
execution is unsupported.

Storage has native adapters on host, Pico and ESP32. Optional [Wi-Fi and HTTP management](docs/networking.md), including `lightvm.local` device discovery and AP mode for initial setup or standalone use, coexist with USB. MQTT,
Matter and firmware OTA remain planned native services. Firmware OTA replaces the native
engine and services; an animation upload replaces a RAM-resident WASM program.
Optional [power-cycle sequence detection](docs/boot-sequence.md) counts power-ons
"1, 2, 3". With Wi-Fi and HTTP enabled, the third short boot opens a temporary
recovery AP and keeps the startup program idle, preserving saved settings. Enable
the detector with `LIGHTVM_BOOT_SEQUENCE=ON`; it defaults to `OFF`. Detector-only
builds leave startup unchanged.
An optional [separate MQTT WASM experiment](experiments/mqtt/README.md) is built
with `LIGHTVM_MQTT_EXPERIMENT=ON` and defaults to off. It includes a portable
coreMQTT serializer guest, HA speed/discovery sample, bounded native TCP ABI,
independent service uploads and buffered benchmarks. Its measured capacity
results are recorded separately from normal firmware support.
The porting contract and extension rules are in [docs/architecture.md](docs/architecture.md).

The [control API and execution model proposal](documents/feature-proposal-control-api.md)
defines the next API independently, with documented roots in picolights. Named
resources expose typed properties/samples, actions/jobs and events. Triggers
schedule bounded handlers and resumable tasks; addressable RGB pixels, digital/analogue IO,
PWM and servos share timing, publication and ownership conventions. Input
sampling, guest execution and native waveforms have independent cadences.
The first single-program control profile implements start/frame handlers,
deployment-injected `ctx.pixels`, program time, typed parameters, black RGB at
the start of each step and successful-step publication. Device events, resumable tasks and composition remain proposed;
they are not supplied by the legacy API v1 adapter.

Planned language bindings target that common contract. Pixelblaze source
compatibility belongs in an independent frontend/adapter. A legacy adapter must
preserve existing API v1 module behaviour as the new contract is introduced. The
[device](documents/feature-proposal-device-support.md),
[editor](documents/feature-proposal-web-editor.md) and
[A/B transition](documents/feature-proposal-ab-transitions.md) proposals follow
the common model. The
[animation composition proposal](documents/feature-proposal-animation-composition.md)
adds bounded ordered scenes with reusable backgrounds and foregrounds, such as
Rainbow beneath Twinkle. The earlier presence/retention design has been withdrawn;
proper alpha transparency and composition will be designed and implemented
separately. A/B transitions also remain proposed; production currently renders
one RGB source in slot A.

The [build profiles proposal](documents/feature-proposal-build-profiles.md)
describes selecting drivers, HTTP, MQTT, OTA and resource/storage settings for
each board instance. Instances of the same board model can use different feature
sets according to user needs and available capacity.

Multi-board portability is a maintained requirement. In the current implementation,
identical compatible WASM must run on host, Pico, XIAO C3, XIAO S3 and XIAO C6 with
native board configuration changes only. CI builds every firmware and tests both
host profiles on pushes and PRs.
The portable smoke runner checks the same behavior over pipes or USB; successful
cross-builds alone do not establish physical USB, output or timing correctness.

## WS2812B demo on GP16

### Python animation authoring

LightVM's original Python frontend, licensed under the MIT License, compiles a
documented subset directly to small, memoryless WASM. It uses the feature notes'
resource/handler model. Six starter effects live in `samples/python/`; polynomial
easing is implemented from equations.
No picolights/Adafruit imports or GPL easing source are used.

```sh
python -m lightvm.compiler samples/python/rainbow.py -o build/samples/rainbow_control.wasm
python tools/client.py --port PORT --store-procedure rainbow-control build/samples/rainbow_control.wasm
python tools/client.py --port PORT --activate rainbow-control --slot A
python tools/client.py --port PORT --set-parameters '{"speed":0.3}'
```

Animations receive their assigned contribution as `ctx.pixels`; source and
manifest contain no destination name. Deployment selects the slot; this first
runtime exposes only A. Configure its output first using the workflow below.
The [control guide](docs/control-api.md) documents source syntax, compiler limits, ABI, pause/resume, passive snapshots and
frame reset and own-step RGB readback. The compiler requires
Python 3.9+; the device contains no Python interpreter. Scenes and waits follow later.

The [static device workspace](docs/web-editor.md) is built with React, TypeScript,
Redux Toolkit and Vite. Start with a device connection, then use separate
**Develop** and **Configure** views. Development compiles the existing Python
subset in a Pyodide worker, shows inline diagnostics and uploads WASM over USB
CDC or HTTP. Configuration handles output, startup and networking. The workspace
also manages stored programs and live parameters. Its Firmware page installs
XIAO ESP32-C3/S3/C6 images over Web Serial and Pico/Pico W images from BOOTSEL
over WebUSB. The Pico UF2 files can also be downloaded for manual installation.
USB and HTTP can stay connected together for management.
Install npm dependencies, then run the local editor server and open it in desktop
Chrome or Edge. Compilation stays in the browser.

The [GitHub Pages workflow](.github/workflows/pages.yml) automatically publishes
the editor at **https://kajlights.github.io/lightvm/** from public `main` pushes
once [Pages is enabled](docs/web-editor.md#build-and-publish).

The example configuration drives 100 RGB WS2812B pixels on **GP16**
(physical Pico pin 21), GRB wire order at 800 kHz, with brightness limited to
32/255. These settings are uploaded data in
[examples/pico-startup.json](examples/pico-startup.json), rather than firmware
constants. The driver uses Pico SDK PIO and synchronous output.

Fresh firmware starts idle. Store the independent WASM sample, apply and save
configuration, then select the procedure:

```sh
build/venv/bin/python tools/build_samples.py
build/venv/bin/python tools/client.py --port PORT --store-procedure rainbow build/samples/rainbow.wasm
build/venv/bin/python tools/client.py --port PORT --config examples/pico-startup.json --save
build/venv/bin/python tools/client.py --port PORT --activate rainbow
```

Closing serial leaves playback running. Reboot loads the saved configuration and
its named startup WASM. `--stop` clears output while retaining the loaded instance;
`--animate` resumes it. Legacy `--animate FILE` remains a transient replacement.
RESET clears runtime/staging without erasing the library or configuration.
All animation effects, including rainbow, run from uploaded WASM procedures.

The [configuration guide](docs/configuration.md) documents the schema, client,
management operations, limits and snapshot recovery. Host storage uses
`--storage PATH`; the Pico and XIAO ESP32 ports have a reserved 512 KiB store.
Configuration can also be tested in RAM without saving.

### Portable animation API v1

An animation exports `render(index: i32) -> void`. It may also export
`beforeRender(delta_ms: i32) -> void`, called once before all pixels each frame.
The index runs from 0 through `led.pixel_count() - 1` (0..99 in the supplied Pico example).
Elapsed time is measured by the port's monotonic clock;
fractional milliseconds carry into later frames, and long gaps are capped at
1,000 ms. This prototype historically adopted Pixelblaze-style callbacks;
it remains the legacy compatibility contract, not the definition of the
independently designed control API.

| Import from `led` | Signature | Meaning |
|---|---|---|
| `pixel_count` | `() -> i32` | Number of logical pixels in the assigned target |
| `rgb` | `(r: i32, g: i32, b: i32) -> void` | Set the current pixel; channels 0..255 |
| `hsv` | `(h: i32, s: i32, v: i32) -> void` | Set the current pixel; hue wraps modulo 65,536, saturation/value 0..255 |

RGB and HSV writes are valid during `render(index)`. HSV conversion is native,
using integer arithmetic. The example owns one packed RGB buffer of **300 bytes**;
the pattern does not allocate an array or need WASM linear memory. Other profiles
use three bytes per pixel. The native backend owns physical wire order and
brightness; the Pico driver packs GRB words as it writes the FIFO.
The buffer is cleared before rendering, so unwritten pixels are black. Only
complete successful frames are sent to the strip; a trapped frame stops playback
and preserves the last displayed frame.

The animation API version (`api=1`) is independent of framed protocol version
(`protocol=1`). STATUS reports both, the `platform`, `board`, output backend,
resource limits, `pin` (-1 when inapplicable), `pixels`, `brightness`,
`frame_interval_us`, `mode` (`native`, `wasm` or `idle`), frame count, last
render/output times in microseconds and any animation error. The host reports `output=memory`; pin/count/brightness reflect configuration.
Snapshots remain logical RGB regardless of the brightness limit.
For 100 pixels, wire transmission plus reset takes about 3.3 ms per frame.

### Desktop preview and debugging

```sh
build/venv/bin/python tools/client.py --process build/host/lightvm --storage build/demo-store.bin \
  --store-procedure rainbow build/samples/rainbow.wasm
build/venv/bin/python tools/client.py --process build/host/lightvm --storage build/demo-store.bin \
  --config examples/pico-startup.json --save
build/venv/bin/python tools/client.py --process build/host/lightvm --storage build/demo-store.bin --frame-ms 16
```

`--frame-ms` pauses playback and renders one frame with an explicit delta. It
returns an RGB snapshot and prints the first five pixels. The same command works
over USB. Snapshots contain logical RGB bytes before output brightness scaling
and use a separate reply type from console output.

Console writes work during explicit RUN/FRAME requests and module initialization
for PLAY. A console write during autonomous rendering stops playback with an
animation error; use explicit frames to inspect pattern logs. This keeps command
responses usable while the animation runs with the serial connection closed.


## Desktop proof

Requires Python 3.8+, CMake 3.18+, a C/C++ compiler and a build tool. Commands
below are run from this directory. Build directories are ignored by Git.

```sh
python3 -m venv build/venv
build/venv/bin/python -m pip install -r requirements.txt
cmake -S . -B build/host -DLIGHTVM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
build/venv/bin/python tools/build_samples.py
build/venv/bin/python tools/client.py --process build/host/lightvm build/samples/hello.wasm
```

The module produces:

```text
Hello from dynamically uploaded WASM!
```

The sender reports `Status 0, result 42: executed`. Try
`build/samples/wasi_stdout.wasm` for the WASI stdout path. `--process` runs the
same C command handler, protocol, allocator and wasm3 interpreter as the firmware,
with pipes replacing the Pico USB driver. Wasmtime is only a desktop WAT compiler;
it is not the interpreter used by the receiver.

## Pico build and USB upload

The verified target is the original RP2040 Pico, using Pico SDK 1.5.1,
its matching TinyUSB submodule and Arm GNU Toolchain 13.2.Rel1. An existing SDK
checkout can be used; initialize its submodules before building. CMake downloads
the pinned wasm3 v0.5.0 archive and verifies its SHA-256. For offline builds,
pass `-DWASM3_SOURCE_DIR=/absolute/path/to/wasm3-0.5.0`.

```sh
git clone --branch 1.5.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git build/pico-sdk
git -C build/pico-sdk submodule update --init lib/tinyusb
cmake -S . -B build/pico \
  -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico \
  -DPICO_SDK_PATH="$PWD/build/pico-sdk" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/pico
```

The ARM compiler must be on `PATH`, or set
`-DPICO_TOOLCHAIN_PATH=/absolute/path/to/arm-toolchain`. The UF2 is
`build/pico/lightvm.uf2`. Enter BOOTSEL and copy that file onto `RPI-RP2`
to install the native firmware, replacing the firmware currently on that Pico.
Reconnect normally and identify its USB serial port.
The browser workspace's **Firmware** page can install the packaged Pico or Pico W
UF2 directly from BOOTSEL in Chrome or Edge. Select the exact model first; both
boards expose the same RP2040 bootloader identity.

`-DLIGHTVM_PICO=ON` remains a compatibility alias. Use a separate build directory
for each platform/profile. Wiring changes use runtime configuration. To change
resource ceilings or engine budgets, provide a complete
board header with `-DLIGHTVM_BOARD_CONFIG=/absolute/path/to/board_config.h`;
unspecified engine budgets take defaults from `src/config.h`.

```sh
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX --status
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX build/samples/hello.wasm
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX build/samples/wasi_stdout.wasm
```

Linux normally uses `/dev/ttyACM0`; Windows uses a port such as `COM3`.
Each invocation can upload new bytecode without rebuilding, reflashing or
restarting the Pico. The resident module survives closing the serial port.
The command line uploads before running; library callers can issue repeated
`RUN` commands to retain module globals, as demonstrated by `counter.wat`.
The client queries STATUS before uploading and uses the receiver's `module_limit`.

Use `--function _start` for a suitable WASI export. Only `fd_write` and
`proc_exit` are provided from WASI; modules requiring other imports are rejected.
This is not a complete WASI environment. A regular terminal cannot upload raw
WASM using this framed protocol; use the sender instead.

## XIAO ESP32-C3, ESP32-S3 and ESP32-C6 build and upload

The preliminary profiles target the original XIAO ESP32-C3, ESP32-S3 and ESP32-C6, using
[ESP-IDF v5.5.3](https://github.com/espressif/esp-idf/releases/tag/v5.5.3).
All three load saved configuration and optional startup WASM, and accept the existing
USB protocol and animation API. Flashing deliberately resets the board; subsequent WASM uploads
are data transfers and do not require reflashing.

| `LIGHTVM_BOARD` | SDK target | Example WS2812 data | Example pixels | Example brightness | Flash |
|---|---|---|---:|---:|---:|
| `xiao_esp32c3` | `esp32c3` | D1 / GPIO3 | 100 | 32/255 | 4 MiB |
| `xiao_esp32s3` | `esp32s3` | D0 / GPIO1 | 100 | 32/255 | 8 MiB |
| `xiao_esp32c6` | `esp32c6` | D0 / GPIO0 | 100 | 32/255 | 4 MiB |

Mappings follow Seeed's [C3 guide](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/),
[S3 guide](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
and [C6 guide](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/).
Use D1 for the C3 example: its D0/GPIO2 is a boot strap pin. Connect the selected
data pin to strip data input, use the strip's rated supply and common ground,
and use a suitable 3.3 V-to-strip data level shifter. Power 100 pixels from a
separate adequately rated supply, not the XIAO's 3V3 output. Onboard indicators
are separate from this strip output.

Install the SDK/toolchains once, then build from the repository root:

```sh
git clone --branch v5.5.3 --depth 1 --recursive --shallow-submodules \
  https://github.com/espressif/esp-idf.git build/esp-idf
build/esp-idf/install.sh esp32c3,esp32s3,esp32c6
. build/esp-idf/export.sh
export IDF_COMPONENT_MANAGER=0

idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c3" \
  -D LIGHTVM_BOARD=xiao_esp32c3 build
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32s3" \
  -D LIGHTVM_BOARD=xiao_esp32s3 build
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c6" \
  -D LIGHTVM_BOARD=xiao_esp32c6 build
```

The component manager is unnecessary: the SDK is pinned and wasm3 uses the
shared verified-archive helper. Disable it for the SDK bootloader sub-build too.
`WASM3_SOURCE_DIR` remains available for offline builds. The board selects the
toolchain; an inconsistent `IDF_TARGET`, or reusing one build directory for
another board, fails explicitly. Generated `sdkconfig`, binaries and dependency
copies live in the build directory. For different wiring/budgets, pass a complete
portable header through `-D LIGHTVM_BOARD_CONFIG=/absolute/path/to/board_config.h`
at first configuration.

Initial settings use DIO flash at 40 MHz and a single 1 MiB application partition.
PSRAM and radio stacks are excluded. The interpreter uses internal RAM with the
same budgets as Pico. Output is synchronous RMT without DMA, with a retained GRB
buffer and 300 µs latch interval. One 16 KiB main task owns the engine and yields
every millisecond. Logs do not share the binary protocol channel; USB writes have
bounded waits so a disconnected host cannot block animation indefinitely.

Flash the selected build, replacing `PORT` with its USB serial device:

```sh
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c3" -p PORT flash
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32s3" -p PORT flash
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c6" -p PORT flash
```

If automatic flashing fails, hold BOOT while connecting USB, release it and retry.
Close the IDF monitor before using the client. Use `--no-reset` for each XIAO:
it deasserts DTR/RTS before serial open and briefly drains stale/ROM boot bytes
before issuing a request. Pico retains its default control behavior.

```sh
build/venv/bin/python tools/build_samples.py
build/venv/bin/python tools/client.py --port PORT --no-reset --status
build/venv/bin/python tools/client.py --port PORT --no-reset build/samples/hello.wasm
build/venv/bin/python tools/client.py --port PORT --no-reset --store-procedure rainbow build/samples/rainbow.wasm
build/venv/bin/python tools/client.py --port PORT --no-reset --config examples/xiao-startup.json --save
build/venv/bin/python tools/client.py --port PORT --no-reset --activate rainbow
build/venv/bin/python tools/client.py --port PORT --no-reset --stop
```

For the C3, use `examples/xiao-c3-startup.json` in place of
`examples/xiao-startup.json`.

STATUS reports `platform=esp32`, the selected board, `output=ws2812`, actual GPIO
and shared resource limits. Closing serial retains the instance; resetting loads
the saved startup procedure. ESP32 physical behavior remains unvalidated until
the checks below pass. Wi-Fi/HTTP is optional; BLE, Thread/Zigbee, MQTT, Matter
and OTA remain later work.

Existing ESP32 build directories retain SDK configuration: switch to a fresh
build directory when adopting the custom partition table, or explicitly select
`CONFIG_PARTITION_TABLE_CUSTOM` and `partitions.csv` before reconfiguring. The
`lightvm` data partition starts at `0x110000` and does not overlap the application.

## C authoring example

With an LLVM Clang distribution and `wasm-ld` installed, compile the freestanding
sample independently from the ARM firmware:

```sh
clang --target=wasm32 -O2 -nostdlib \
  -Wl,--no-entry -Wl,-z,stack-size=4096 \
  -Wl,--initial-memory=65536 -Wl,--max-memory=65536 \
  samples/hello.c -o build/samples/hello_c.wasm
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX build/samples/hello_c.wasm
```

The sample imports `console.write`, prints a message and returns 7. Apple’s
system Clang may lack a WASM backend or linker; use an LLVM distribution with
both. Ordinary libc `printf` additionally requires a suitable WASI toolchain
and any other imports that toolchain generates.

## Protocol

Each serial frame has a 16-byte little-endian header:

| Field | Size |
|---|---:|
| Magic `WASM` | 4 bytes |
| Protocol version, currently 1 | 1 byte |
| Message type | 1 byte |
| Request ID, echoed in replies | 2 bytes |
| Payload length, at most 512 | 4 bytes |
| CRC-32 of header bytes 4..11 followed by payload | 4 bytes |

The CRC matches Python's `zlib.crc32`. Payloads follow the header.

| Type | Command | Payload |
|---:|---|---|
| 1 | STATUS | Empty |
| 2 | BEGIN | `u32 byte_count, u32 module_crc32` |
| 3 | CHUNK | `u32 offset, byte[] data` |
| 4 | FINISH | Empty |
| 5 | RUN | UTF-8 export name; empty selects `run` |
| 6 | RESET | Empty |
| 7 | PLAY | Empty; start the resident render(index) animation |
| 8 | STOP | Empty; stop playback and clear pixels |
| 9 | FRAME | u32 elapsed milliseconds, 0..1000; pause and render once |
| 10 | Reserved | Rejected as an unknown command |
| 128 | OUTPUT reply | Unmodified console bytes |
| 129 | STATUS reply | `u32 status, i32 value, UTF-8 message` |
| 130 | PIXELS reply | Packed RGB snapshot, chunked if needed; followed by STATUS |

Commands 11..15 add structured management and independent object staging; DATA
reply 131 carries fragmented metadata/readback. See [management v1](docs/configuration.md#usb-management-v1).

Uploads require ordered chunks; an identical already-received chunk is accepted
as a retry. FINISH checks completeness and the whole-module checksum, then
parses, links and compiles the module into wasm3's internal interpreter
representation. RUN executes it. Firmware execution status is separate from
the function's integer result: a return of 42 is successful status 0 with value
42. Status codes are 0 success, 1 protocol error, 2 upload error, 3 load error,
4 execution/export error, 5 execution/output limit, and 6 nonzero WASI exit.

An accepted BEGIN unloads the previous module before overwriting the single
upload slot. Failed uploads leave no executable module. RESET unloads everything
and cancels a partial upload. Legacy uploads live in RAM; named procedures and
configuration saved through management survive reboot.

## Memory and execution limits

These are the default profile's limits. A board header can set pixel capacity,
upload/heap/operand-stack/output budgets, deadline and frame interval. The native
allocator chooses where interpreter memory is obtained; budget accounting stays
in the core. WASM linear memory remains limited to one page across current ports.

- Pixel capacity: 1024; runtime count is configured. One RGB buffer uses 3N bytes,
  allocated when applying configuration, with no per-frame allocation.
- Configuration: at most 8 KiB; module/config staging is separate from resident bytes.
- Persistent store: 512 KiB, two banks, up to eight named procedures within capacity.
- Module/upload slot: 32 KiB, retained for the loaded module's lifetime.
- Interpreter heap: 128 KiB including tracked allocator headers; STATUS reports
  live usage and the lifetime high-water mark.
- WASM linear memory: at most one 64 KiB page, included in the interpreter heap.
- WASM operand stack: 8 KiB, included in that heap.
- Console output: at most 16 KiB per RUN, emitted in chunks instead of buffered.
- Cooperative execution deadline: 500 ms, checked at function calls and loop
  iterations. It bounds guest execution, not compilation or a blocking USB write.

CMake adds a loop-yield check to a build-local copy of pinned wasm3 v0.5.0.
The dependency checkout stays unchanged. Our `m3_Yield` implementation returns
a timeout trap so an infinite loop produces status 5 and the receiver remains
usable. Interpreter allocations are accounted through its allocator entry points.
The reported heap does not include SDK allocations, C stacks or static buffers.

This prototype supports one resident WASM module and autonomous animation
callbacks, with WS2812B output in the Pico port. Concurrent A/B instances,
ordered scene layers, transitions and network/control adapters are subsequent work.

## Verification

Configuration/storage verification on 2026-10-12 passed all **58 Python tests**
on both host profiles, the C platform-contract/fault-injection test, and both
host smoke runs. Pico, XIAO ESP32-S3 and ESP32-C6 cross-builds succeeded; the Pico
linker also rejected a deliberately overlapping firmware boundary, and both ESP32
partition tables reserve the expected 512 KiB region.

The new Pico firmware was flashed and checked on the existing 100-pixel GP16
setup. Procedure save/readback, configuration apply/save, RESET retention,
console/WASI, timeout recovery, RGB snapshots and serial reconnect passed. Reloading
the same UF2 preserved storage and automatically started the saved WASM rainbow.
The sample used 11,870 bytes of interpreter heap, about 1,687 us rendering and
3,301 us output; saving its 303-byte module took about 2.1 seconds. Interrupted
writes were simulated in C tests, without physically cutting power. No ESP32
hardware was attached for this verification; its physical output, storage and
reboot behavior remain unverified. Earlier checks below describe previous builds.

```sh
ctest --test-dir build/host --output-on-failure
build/venv/bin/python -m unittest discover -s tests -v
```

Tests use real wasm3 and the wire protocol: console/WASI output, integer and exit
results, repeated calls and module replacement, corrupt transfers, chunk retries,
memory limits, invalid exports, native buffer bounds, start-function output/exit
status and recovery after a timeout. Pixel tests additionally cover RGB/HSV,
callback signatures, frame deltas, persistent state, idle playback, frame clearing,
RGB snapshots, console isolation and animation fault recovery.
Set `LIGHTVM_RECEIVER=/absolute/path/to/receiver` to test another host build.
The C port-contract test links the shared engine against a fake native backend
and checks clock-driven scheduling/timeouts, output buffers, allocation failure
and recovery, plus reply routing for valid and malformed framed commands.

To check the same API with a different pixel count and smaller upload budget:

```sh
cmake -S . -B build/host-175 -DLIGHTVM_PLATFORM=host \
  -DLIGHTVM_BOARD_CONFIG="$PWD/tests/boards/host_175.h" -DCMAKE_BUILD_TYPE=Release
cmake --build build/host-175
ctest --test-dir build/host-175 --output-on-failure
LIGHTVM_RECEIVER="$PWD/build/host-175/lightvm" build/venv/bin/python -m unittest discover -s tests -v
```

This profile has 175 pixels (525-byte RGB snapshots, spanning reply frames), a
16 KiB upload limit and a 10 ms frame interval. The sample WASM is unchanged.
On 2026-10-12 the platform refactor passed 37 Python integration tests and the
C port-contract test with both host profiles, and the Pico firmware cross-build.
That platform-refactor firmware was not flashed or tested on hardware. Its
initial verification predates the ESP32 implementation described above.

### Multi-board regression and hardware checks

The [CI workflow](.github/workflows/build.yml) runs both host profiles' C/Python
tests and portable smoke checks, then cross-builds Pico and all three XIAOs. It keeps
firmware, build-size reports, ESP32 flash arguments/maps/configuration and license
notices. Physical validation is a separate recorded step.

```sh
build/venv/bin/python tools/smoke_device.py --process build/host/lightvm --board simulator
build/venv/bin/python tools/smoke_device.py --process build/host-175/lightvm \
  --board test_175 --reference build/host-175/lightvm
build/venv/bin/python tools/smoke_device.py --port PORT --board pico
build/venv/bin/python tools/smoke_device.py --port PORT --board xiao_esp32c3
build/venv/bin/python tools/smoke_device.py --port PORT --board xiao_esp32s3
build/venv/bin/python tools/smoke_device.py --port PORT --board xiao_esp32c6
```

The smoke runner **replaces the resident program and stops playback**. It checks
identity, uploads, console/WASI, retained state, replacement, upload/heap/memory
limits, malformed transfers, timeout recovery and memory release. Fresh RGB/HSV/
rainbow modules are stepped against a host reference with identical WASM bytes
and deltas. Serial checks also close/reopen the connection and verify retained
state and autonomous frames. XIAO identities select no-reset opening automatically.
The host reference must match pixel count; select `--reference` for custom profiles.

On each freshly flashed board, confirm idle management, provision the example
configuration and stored rainbow, then reboot and observe autonomous startup.
Check physical order with the first three pixels red, green, blue, repeating:

```sh
build/venv/bin/python tools/client.py --port PORT --no-reset \
  --frame-ms 0 build/samples/primary_colors.wasm
```

Omit `--no-reset` for Pico. The stepped snapshot is logical RGB before brightness
scaling; observing the strip checks the physical GRB backend. Then run the smoke runner.
Confirm STOP extinguishes the strip, PLAY resumes it and power cycling restores
the saved startup WASM procedure. Record SDK/profile, pin/count, sustained frame cadence,
render/output times and stable heap/peak. Run identical compatible WASM on Pico,
C3, S3 and C6 before recording physical multi-board proof.

### Compare board performance

With the same 100-pixel strip attached to each board, run the benchmark after
flashing. It applies a temporary configuration, uploads identical rainbow WASM,
samples device-measured render and output times, and counts autonomous frames.
The original active configuration is restored afterward, but the resident WASM
is replaced and playback stops. Stored configuration and procedures are untouched.

```sh
build/venv/bin/python tools/benchmark_device.py --port PORT --board pico --output build/bench-pico.json
build/venv/bin/python tools/benchmark_device.py --port PORT --board xiao_esp32c3 --output build/bench-c3.json
build/venv/bin/python tools/benchmark_device.py --port PORT --board xiao_esp32s3 --output build/bench-s3.json
build/venv/bin/python tools/benchmark_device.py --port PORT --board xiao_esp32c6 --output build/bench-c6.json
```

The C3 default is D1/GPIO3; the S3 and C6 defaults are D0. Pass `--pin` if wired
differently, and keep `--pixels` equal on every board. Results include median and
95th-percentile render/output microseconds, autonomous frames per second and peak
interpreter heap. Compare the WASM hash in the JSON files to confirm the workload
matches. Host output uses a memory buffer, so its `show_us` is not a physical-strip
comparison.

| Target | Latest recorded build/check result | Hardware validation |
|---|---|---|
| Host, 100 pixels | 39 Python tests, C contract test and portable smoke passed | Not applicable |
| Host, 175 pixels | 39 Python tests, C contract test and portable smoke passed | Not applicable |
| Original Pico | Cross-build passed | Earlier pre-refactor checks below; current build pending |
| XIAO ESP32-C3 | 2026-10-13: ESP-IDF 5.5.3 USB build passed (295,104-byte app); Wi-Fi/HTTP with boot sequence passed (948,704-byte app) | Board accepted USB and HTTP uploads. A web-editor-compiled Python rainbow ran 306 frames in 5.119 s (59.78 fps) with 100 logical pixels and no animation error; 20,559-byte tracked interpreter heap peak. Physical strip output pending. |
| XIAO ESP32-S3 | ESP-IDF 5.5.3 cross-build passed; app image 255,216 bytes | Pending |
| XIAO ESP32-C6 | ESP-IDF 5.5.3 cross-build passed; app image 208,800 bytes | Pending |

The workflow is added for future push/PR runs; these results are local checks.

The C3 runtime check used a temporary D1 configuration and restored the original
output settings afterward. A 64 KiB linear-memory WASM sample failed to load
with `memory allocation failed` while Wi-Fi/HTTP was active; the editor's
memoryless control-ABI rainbow loaded and ran. The interpreter heap figures do
not include all ESP-IDF and Wi-Fi allocations, so test other WASM workloads on
the C3 before relying on them.

The earlier Pico firmware was cross-compiled successfully. That build had
150,244 bytes of text and 37,644 bytes of static BSS. Physical USB operation was
verified on an original Pico on 2026-10-12: independent module uploads, console
and WASI stdout, integer results, persistent counter state, module replacement,
and memory release after RESET all passed. An infinite loop returned status 5
after 0.501 seconds, and the receiver accepted another module afterward.
These hardware smoke checks used the initial Pico SDK double implementation.
The current WS2812B build includes the compiler-double configuration from the
license review. Both earlier builds succeeded and 36 desktop integration tests passed.
That pre-refactor firmware was flashed and tested on 2026-10-12. Kaj confirmed both
the now-removed native rainbow and the separately uploaded WASM rainbow/brightness waves on
the 100-pixel GP16 strip. Over a 2.012-second sample with serial closed between
status reads, the device produced 121 frames (approximately 60 fps). The last
reported frame took 1,951 us to render and 3,310 us to output. Interpreter heap
usage and peak were both 11,870 bytes and remained unchanged during playback;
the native RGB buffer is a separate 300 bytes. The uploaded sample is 303 bytes
and declares no WASM linear memory. This measures the supplied demo on 100 pixels.

## License

LightVM's original code and documentation are licensed under the
[MIT License](LICENSE), copyright 2026 Kaj Wik Siebert. Third-party dependencies
retain their own licenses; their terms and notices are preserved in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `licenses/`.
The Pico build uses the compiler's double-precision implementation to exclude
an SDK fallback restricted to Pico devices. Include the copied notices and
license directory when distributing firmware or executables.
