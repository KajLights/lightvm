# LightVM

LightVM is a [Kajlights](https://kajlights.com/) project developing a native WebAssembly
runtime for programmable lighting across microcontrollers and native hosts, under the
[Kajlights GitHub organisation](https://github.com/Kajlights).

The goal is one portable animation API and the same `.wasm` programs across
supported boards, within each board's resource limits. The shared C engine and
wasm3 interpreter are rebuilt for each processor; native ports supply the clock,
LED output, memory allocation, transports and startup policy. Adding a board
should not require board-specific imports or pin assumptions in an effect.

The current Pico prototype receives a `.wasm` file over USB CDC,
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
| Seeed XIAO ESP32-S3 | Planned native port; no ESP-IDF build or hardware validation yet |
| Seeed XIAO ESP32-C6 | Planned native port; no ESP-IDF build or hardware validation yet |

| Location | Responsibility |
|---|---|
| `src/` / `lightvm_core` | WASM loading/bindings, animation scheduling, RGB/HSV, allocation budgets, commands and framing |
| `src/platform.h` | Native service contract: identity, monotonic clock, RGB output and allocation hooks |
| `src/session.h` | Shared session lifecycle and byte decoding; driven by a native event loop |
| `platforms/host/` | POSIX entry point, pipes, clock and host memory allocation |
| `platforms/pico/` | Pico entry point, SDK bootstrap, USB, PIO, wiring and boot demo policy |
| `cmake/lightvm_core.cmake` | Reusable engine build for ports with their own SDK project/entry point |

The command handler has a transport-independent `SendMessage` callback.
`session_feed` accepts framed bytes; message-based adapters can call `app_command`
directly. Native networking callbacks must queue work for the engine owner. There
is currently one resident module, one RGB buffer and global interpreter/allocator
state; concurrent engine execution is unsupported.

Wi-Fi, MQTT, Matter, storage, provisioning and firmware OTA belong in native
platform adapters. They are planned features. Firmware OTA replaces the native
engine and services; an animation upload replaces a RAM-resident WASM program.
The porting contract and extension rules are in [docs/architecture.md](docs/architecture.md).

## WS2812B demo on GP16

The current test-board configuration is 100 RGB WS2812B pixels on **GP16**
(physical Pico pin 21), GRB wire order at 800 kHz. Native output uses the official
Pico examples' four-instruction PIO program through the Pico SDK. The driver
feeds its FIFO synchronously and allows 300 microseconds of reset time after
the final pixel. Output brightness is fixed at 32/255 (about 12.5%). Pin, count
and brightness are constants in `platforms/pico/board_config.h`; PIO and output
scaling live in `platforms/pico/platform.c`.

After flashing `build/pico/lightvm.uf2`, a native scrolling rainbow starts at
boot and targets 60 frames per second. It runs without a USB host or a WASM
module. This gives a direct check of the output driver and strip wiring.

Upload the independent WASM pattern for a rainbow with moving brightness waves:

```sh
build/venv/bin/python tools/build_samples.py
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX --animate build/samples/rainbow.wasm
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX --status
```

The upload replaces the resident module, and the Pico calls its renderer on
each scheduled frame. Closing the serial connection leaves the animation
running. Reboot restores the native rainbow; the uploaded program lives in RAM.

```sh
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX --stop
build/venv/bin/python tools/client.py --port /dev/cu.usbmodemXXXX --native-demo
```

`--stop` clears the strip while retaining the loaded module. `--animate` without
a file resumes that module. Starting a new upload or RESET stops playback and
clears the strip. `--native-demo` switches back to the native rainbow.

### Portable animation API v1

An animation exports `render(index: i32) -> void`. It may also export
`beforeRender(delta_ms: i32) -> void`, called once before all pixels each frame.
The index runs from 0 through `led.pixel_count() - 1` (0..99 on the default Pico).
Elapsed time is measured by the port's monotonic clock;
fractional milliseconds carry into later frames, and long gaps are capped at
1,000 ms. These callbacks are inspired by Pixelblaze's rendering model.

| Import from `led` | Signature | Meaning |
|---|---|---|
| `pixel_count` | `() -> i32` | Number of logical pixels in the board profile |
| `rgb` | `(r: i32, g: i32, b: i32) -> void` | Set the current pixel; channels 0..255 |
| `hsv` | `(h: i32, s: i32, v: i32) -> void` | Set the current pixel; hue wraps modulo 65,536, saturation/value 0..255 |

RGB and HSV writes are valid during `render(index)`. HSV conversion is native,
using integer arithmetic. The engine owns one packed RGB buffer of **300 bytes**;
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
render/output times in microseconds and any animation error. The host reports
`output=memory` and brightness 255: its snapshots have no output scaling.
For 100 pixels, wire transmission plus reset takes about 3.3 ms per frame.

### Desktop preview and debugging

```sh
build/venv/bin/python tools/client.py --process build/host/lightvm --frame-ms 16 build/samples/rainbow.wasm
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

`-DLIGHTVM_PICO=ON` remains a compatibility alias. Use a separate build directory
for each platform/profile. To change wiring or engine budgets, provide a complete
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
| 10 | NATIVE_DEMO | Empty; start the native rainbow |
| 128 | OUTPUT reply | Unmodified console bytes |
| 129 | STATUS reply | `u32 status, i32 value, UTF-8 message` |
| 130 | PIXELS reply | Packed RGB snapshot, chunked if needed; followed by STATUS |

Uploads require ordered chunks; an identical already-received chunk is accepted
as a retry. FINISH checks completeness and the whole-module checksum, then
parses, links and compiles the module into wasm3's internal interpreter
representation. RUN executes it. Firmware execution status is separate from
the function's integer result: a return of 42 is successful status 0 with value
42. Status codes are 0 success, 1 protocol error, 2 upload error, 3 load error,
4 execution/export error, 5 execution/output limit, and 6 nonzero WASI exit.

An accepted BEGIN unloads the previous module before overwriting the single
upload slot. Failed uploads leave no executable module. RESET unloads everything
and cancels a partial upload. Programs live in RAM and are lost on reboot.

## Memory and execution limits

These are the default profile's limits. A board header can set pixel count,
upload/heap/operand-stack/output budgets, deadline and frame interval. The native
allocator chooses where interpreter memory is obtained; budget accounting stays
in the core. WASM linear memory remains limited to one page across current ports.

- Native pixel buffer: 300 bytes for 100 RGB pixels; no per-frame allocation.
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
transitions and network/control adapters are subsequent work.

## Verification

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
The refactored firmware has not been flashed or tested on hardware. ESP32, Matter,
MQTT and OTA are not implemented by this change.

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
the native rainbow and the separately uploaded WASM rainbow/brightness waves on
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
