# LightVM

LightVM is a [Kajlights](https://kajlights.com/) runtime for programmable lighting
on microcontrollers and native hosts. The current proof of concept replaces WASM
with a limited custom bytecode interpreter. Python effects compile off-device to
one controller-independent `.lbc` package.

The replacement runs inside the existing firmware. Device drivers, configuration,
Wi-Fi, HTTP/serial protocols, procedure storage and startup policy are retained.
There is no separate PoC firmware or upload protocol to install. Stored programs
are read from flash through the existing native storage API, and the runtime owns
pixel memory. Procedures can be replaced independently of firmware.

The [animation profile](docs/animation-profile.md) defines persistent buffers,
arrays and the `@init` / `@render` callbacks. Caller arguments supply context,
time, delta and pixels; settings are frozen at initialization.
The current compiler emits LBC1/4 packages. Together they implement a limited part of the
[portable bytecode architecture decision](documents/architecture-decision-portable-bytecode.md).
Up to eight independent device slots run concurrently. Multiple languages,
composition and a broader component system remain future work.

## Build and exercise on the host

Python 3.9+, a C11 compiler and CMake are sufficient for compilation and the host
runtime. pySerial is needed for USB. No WASM assembler/runtime is required.

```sh
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
build/venv/bin/python tools/build_samples.py
build/venv/bin/python tools/smoke_device.py --process build/host/lightvm \
  --reference build/host/lightvm --board simulator
```

The smoke runner temporarily configures an output, uploads all six effects and
compares their RGB output with a host running identical bytecode. It restores the
original active configuration, stops playback and leaves saved configuration and
procedures unchanged.

Enable the existing HTTP and power-recovery profiles with
`-DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_BOOT_SEQUENCE=ON`.
Use `-DLIGHTVM_BOARD_CONFIG="$PWD/tests/boards/host_175.h"` in a separate build
directory to exercise the 175-pixel, 16 KiB program-budget profile.

## Compile, save and run on a device

The samples preserve color, pattern, chase, wipe, twinkle and rainbow. The source
API initializes context once and renders each frame using the actual elapsed delta.

Each effect has one source in `samples/`, shared by the editor, build tools and
current animation tests. `samples/rainbow.py` is the picolights gradient with a
60-second default cycle; its `duration` setting accepts startup arguments such
as `{"duration": 3.3}`.

```sh
build/venv/bin/python -m lightvm.compiler samples/rainbow.py \
  -o build/samples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT --status
build/venv/bin/python tools/client.py --port PORT \
  --store-procedure rainbow build/samples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT \
  --config examples/pico-startup.json --save
build/venv/bin/python tools/client.py --port PORT --activate rainbow
```

Use the configuration matching the board and actual wiring. For XIAO ESP32-C3,
use `examples/xiao-c3-startup.json` and add `--no-reset` to serial commands;
S3/C6 use `examples/xiao-startup.json`. The device keeps its configured pin,
count, order and brightness when changing programs. Reboot runs each device’s
`startup.procedure` with its `startup.args`; omitted arguments use program defaults. `STATUS` reports `mode=bytecode` and `code_storage=flash`
for both named and transient programs. Uploads share one 256-byte page buffer;
there is no package-sized buffer in an animation instance.

Uploading a new version under the same stored name preserves the current running
version in pinned flash until explicit activation. Activation prepares a separate
candidate and retains the old instance on failure. Startup resolves the current
catalogue entry for each configured procedure name; saving a replacement changes
what runs on the next boot. Root-level `targets` and `startup` are unsupported.

Existing stored WASM programs must be recompiled; the loader rejects them. This
preserves saved configuration. Legacy storage directories migrate automatically
to immutable payload extents and alternating directory commits; older firmware
cannot read the new directory format. `RUN`, WASI and arbitrary
WASM exports are outside this limited animation profile. PLAY, FRAME, STOP,
parameters, snapshots and named procedure management remain available. The wire
framing retains its historical `WASM` magic for client compatibility.

## Firmware builds

Pico output uses PIO with DMA; ESP32 uses asynchronous RMT. Host and RP2040 use the root
CMake project. ESP32 uses its SDK project. Resource budgets remain in board
headers; bytecode contains no GPIO, controller or driver selection.

For Pico/Pico W, use Pico SDK 1.5.1 and an Arm GNU embedded toolchain:

```sh
cmake -S . -B build/pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_BOOT_SEQUENCE=ON
cmake --build build/pico-w --parallel
```

Install the resulting `lightvm.uf2` through the existing BOOTSEL flow. For the
original Pico, select `-DPICO_BOARD=pico` and omit Wi-Fi/HTTP. Preserve the reserved
storage region when using flashing tools.

For XIAO ESP32-C3, S3 or C6, use ESP-IDF 5.5.3 and its selected toolchains:

```sh
. /path/to/esp-idf/export.sh
export IDF_COMPONENT_MANAGER=0
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c3" \
  -D LIGHTVM_BOARD=xiao_esp32c3 -D LIGHTVM_WIFI=ON -D LIGHTVM_HTTP=ON \
  -D LIGHTVM_BOOT_SEQUENCE=ON build
idf.py -C platforms/esp32 -B "$PWD/build/xiao_esp32c3" -p PORT flash
```

Use a separate directory and `xiao_esp32s3` or `xiao_esp32c6` for those boards.
The normal partition tables and flash store are retained.

## Browser workspace and device APIs

```sh
npm ci
python3 tools/serve_editor.py
```

The existing [browser workspace](docs/web-editor.md) compiles Python to bytecode
in its worker, downloads `.lbc`, uploads over HTTP/USB, and manages saved programs
and live parameters. Preparation settings restart an animation explicitly;
the editor and example dropdown share the canonical sources. Firmware installation, discovery and configuration flows
are unchanged. See [configuration and management](docs/configuration.md),
[networking](docs/networking.md) and [power recovery](docs/boot-sequence.md).

## Verification and measurements

```sh
ctest --test-dir build/host --output-on-failure
LIGHTVM_RECEIVER="$PWD/build/host/lightvm" \
  build/venv/bin/python -m unittest discover -s tests -v
LIGHTVM_RECEIVER="$PWD/build/host/lightvm" node --test tests/test_editor.mjs
npm run build
npm test
```

HTTP tests require a host build with HTTP enabled and permission to open local
loopback sockets. The tests preserve the existing driver/storage/configuration
contracts and use bytecode fixtures in place of WASM fixtures. They cover
admission, cooperative execution, faults, buffer ownership, parameter conflicts,
snapshots, procedure replacement, bank rotation and startup persistence. Effect
replay executes the same six sample sources through an independent Python caller
harness. There are no historical sample copies or compatibility test suites.
Rainbow/Twinkle permit
one channel level for f32 arithmetic. See the animation profile for measured
memory, including contexts and replacement peaks.

Measure the current Rainbow on a device with:

```sh
build/venv/bin/python tools/benchmark_device.py --port PORT --board pico_w \
  --pixels 200 --output build/benchmark-pico-w.json
```

The device command measures the current canonical
Rainbow through the integrated firmware's render/output times and playback
rate through the existing API, temporarily configuring the output and restoring
its prior configuration afterward. Flash saves retain existing blocking behavior.
The earlier standalone experiment's device timings are not measurements of this
integrated firmware.

## License

Original code and documentation: [MIT](LICENSE), copyright 2026 Kaj Wik Siebert.
SDK and toolchain notices remain in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and `licenses/`; distribute these with firmware. Historical WASM dependency
notices are preserved although those dependencies are no longer linked.
