# LightVM

LightVM is a [Kajlights](https://kajlights.com/) runtime for programmable lighting
on microcontrollers and native hosts. The browser or a computer compiles supported
Python, JavaScript and visual blocks into portable `.lbc` programs. Controller firmware executes the
program and sends data to the LEDs.

Start with [controllers and the development workflow](docs/architecture.md) for
outputs, animation versions, layers, presets, storage and startup. The browser
supports Edit → Preview → Save animation → optional Compose → Save preset.

The [animation profile](docs/animation-profile.md) defines persistent buffers,
arrays and the `@init` / `@render` callbacks. Caller arguments supply context,
time, delta and pixels; settings are frozen at initialization.
Every animation can run alone or as a composition layer. Frames start transparent.
Ordinary colour writes paint solid pixels; alpha is optional. Use `clear()` to remove paint
from an array, and `fill(rgb(0, 0, 0))` when solid black is required.
Programs declare a minimum LightVM release; compiler, editor and firmware share
one [application SemVer contract](docs/versioning.md).
Multiple independent outputs run concurrently, subject to controller resources. Each output can also run
a [native composition](docs/composition.md) with independent background and transparent
foreground animations. The [PixelArray and sprite SDK](docs/pixelarray-sprites.md)
adds shared artwork, user-defined sprite callbacks, optional channels and native
geometry/colour transforms. Python, JavaScript and Blocks share the same checked
compiler and runtime. A broader component system remains future work.

[Motion helpers](docs/motion.md) add compiled easing, bounce progress, fitting
bounds and periodic drawing. The [15-pixel rainbow sprite](examples/animations/rainbow_sprite.py)
cycles hue in 10 seconds and traverses the strip in 20 seconds per direction.
The compiler accelerates [independent pixel loops](docs/pixel-maps.md) through a
generic bulk backend; Rainbow uses ordinary pixel expressions.

## Repository layout

- [`docs/`](docs/README.md): current guides and API references, with design
  proposals, future requirements in [`docs/design/`](docs/design/).
- [`examples/animations/`](examples/animations/): the six canonical Python effects,
  shared by the editor, tools and tests.
- [`examples/compositions/`](examples/compositions/): combinations of those animations on one output.
- [`examples/config/`](examples/config/): board startup and network configuration
  examples.
- [`tests/pico_output/`](tests/pico_output/): production DMA capture and lifecycle probe.

## Build and exercise on the host

Python 3.9+, a C11 compiler and CMake are sufficient for compilation and the host
runtime. pySerial is needed for USB. No WASM assembler/runtime is required.

```sh
python3 -m venv build/venv
build/venv/bin/pip install -r requirements.txt
cmake -S . -B build/host -DLIGHTVM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host --parallel
build/venv/bin/python tools/build_examples.py
build/venv/bin/python tools/smoke_controller.py --process build/host/lightvm \
  --reference build/host/lightvm --board simulator
```

The smoke runner temporarily configures an output, uploads all six effects and
compares their RGB output with a host running identical bytecode. It restores the
original active configuration, stops playback and leaves saved configuration and
programs unchanged.

Enable the existing HTTP and power-recovery profiles with
`-DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_BOOT_SEQUENCE=ON`.
Add `-DLIGHTVM_WEBSOCKET=ON` for the optional plain
[WebSocket command server](docs/websocket.md); it is excluded by default.
Use `-DLIGHTVM_BOARD_CONFIG="$PWD/tests/boards/host_175.h"` in a separate build
directory to exercise the 175-pixel, 16 KiB program-budget profile.

## Compile, save and run on a controller

The examples are Color, Pattern, Rainbow, Stars, Rainbow Sprite, and Sweep. The source
API initializes context once and renders each frame using the actual elapsed delta.
Rainbow paints the full strip. Rainbow Sprite demonstrates a moving 15-pixel image.
Stars supplies the transparent foreground for a twinkle effect over any background.
[Sweep](docs/motion.md#shared-sweep-animation) uses one sprite definition for
moving eyes, chase, inward wipes and timed trails. Run multiple eyes in one batch
to share frames, or use one eye per layer with independently selected blending.
Each source's opening comments describe its settings and modes; wipe is a Sweep
preset rather than a separate animation.

Each effect has one source in `examples/animations/`, shared by the editor, build tools and
current animation tests. `examples/animations/rainbow.py` is the picolights gradient with a
60-second default cycle. Its live phase input selects a timer, while spatial scale
is another live input. The separate [Eye](examples/animations/eye.py) demo exposes
position, width, full RGB colour and local intensity. Preview it and use the editor's **Live inputs**
panel to try these controls without restarting the sprite. See [inputs](docs/inputs.md)
for recipes and the management API.

```sh
build/venv/bin/python -m lightvm.compiler examples/animations/rainbow.py \
  -o build/examples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT --status
build/venv/bin/python tools/client.py --port PORT \
  --store-program rainbow build/examples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT \
  --config examples/config/pico-startup.json --save
build/venv/bin/python tools/client.py --port PORT --activate rainbow
```

Use the configuration matching the board and actual wiring. For XIAO ESP32-C3,
use `examples/config/xiao-c3-startup.json` and add `--no-reset` to serial commands;
S3/C6 use `examples/config/xiao-startup.json`. The controller keeps its configured pin,
count, order and brightness when changing programs. Under Configure → Output & startup, choose **Off**, **Restore last state**, or
**Selected preset** per output. The board examples default to off; save a preset
to the controller before choosing it for startup. Restore last remembers preset
selection and on/off, including OFF, across power loss. `STATUS` reports `mode=bytecode` and `code_storage=flash`
for both named and transient programs. Uploads share one 256-byte page buffer;
there is no package-sized buffer in an animation instance.

Uploading a new version under the same stored name preserves the current running
version in pinned flash until explicit activation. Activation prepares a separate
candidate and retains the old instance on failure. Startup resolves the current
definition of its configured or remembered preset; saving a replacement changes
what runs on the next boot. Root-level `targets` and `startup` are unsupported.

The application version comes from `lightvm/release.json`, currently `1.1.0`.
A program's `requires` field is its minimum release within that major: `1` means
1.0.0 and `1.2` means 1.2.0. Later compatible firmware accepts it without exact
compiler/schema matching. Unsupported requirements fail before replacing the
running animation. The editor preserves editable libraries across releases.
See [versioning and development](docs/versioning.md) for declarations, prereleases,
feature floors, internal format markers and the pre-baseline cutover.

The management frame and storage layouts remain fixed internal encodings.
Unsupported committed storage stays untouched and unavailable for writes, with a
repair/reprovision diagnostic. Back up data before explicitly reprovisioning such
controllers. Current-layout interrupted-write recovery and content-pinned running
revisions remain supported.

## Firmware builds

Pico, Pico W and Pico 2 W output use asynchronous PIO/DMA by default. ESP32 uses asynchronous
RMT and prefers DMA on chips that support it, with ordinary RMT for additional
outputs or unavailable DMA resources. Host, RP2040 and RP2350 use the root
CMake project. ESP32 uses its SDK project. Resource budgets remain in board
headers; bytecode contains no GPIO, controller or driver selection.
The [Pico output probe](tests/pico_output/README.md) verifies the production driver.
[Storage](docs/storage.md) supports interchangeable backends and typed names;
`-DLIGHTVM_STORAGE_BYTES=1048576` selects a 1 MiB native region instead of the
default 512 KiB. Stored program and preset counts follow available capacity.

For Pico/Pico W/Pico 2 W, use Pico SDK 2.3.1 and Arm GNU Toolchain 13.2.Rel1.
Initialise its `lib/tinyusb`, `lib/cyw43-driver` and `lib/lwip` submodules.
The SDK uses picotool 2.3.1 for UF2 generation, building it automatically if
needed. Use fresh build directories after upgrading the SDK.

```sh
cmake -S . -B build/pico-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w \
  -DPICO_SDK_PATH=/path/to/pico-sdk -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_BOOT_SEQUENCE=ON
cmake --build build/pico-w --parallel
```

Install the resulting `lightvm.uf2` through the existing BOOTSEL flow. For the
original Pico, select `-DPICO_BOARD=pico` and omit Wi-Fi/HTTP. Preserve the reserved
storage region when using flashing tools.

For **Pico 2 W**, select `-DPICO_BOARD=pico2_w` and a separate directory such as
`build/pico2-w`; this selects RP2350 Arm Cortex-M33. Hold BOOTSEL while connecting
USB and copy its `lightvm.uf2` to the **RP2350** drive. See the
[Pico 2 W guide](docs/pico2-w.md) for complete build commands, memory layout,
clean provisioning and hardware acceptance. Pico W images cannot run on Pico 2 W.

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

`LIGHTVM_ESP32_PROFILE=AUTO` selects the matching **c3**, **c6** or **s3**
execution profile: strict `-O3` for the shared core, instruction RAM for hot
drawing kernels, memory-mapped flash reads, one render worker and 2 ms
slice/epoch targets. This is also one actual worker on the dual-core S3.
Explicit resource options override these defaults. CPU/flash clocks, numeric
precision and bytecode cache size retain their ordinary values. The default
VM heap automatically uses available RAM after operating reserves; see
[memory budgeting](docs/multicore-rendering.md#resource-profiles).
Use `-DLIGHTVM_ESP32_PROFILE=standard` for a comparison build, or an explicit
`c3`, `c6` or `s3` profile to require that chip. A mismatched profile is an error.
See the [ESP32 campaign](docs/design/esp32-optimisation-2026-10-24.md) for
measurement conditions, thermal monitoring and open service-latency gates;
the 2 ms targets are scheduling budgets, not hard real-time deadlines.
The [earlier C3 comparison](docs/design/c3-performance-2026-10-24.md) remains
historical evidence with its original firmware and observer.

Board temperature, supported supply-voltage readings and other
[board-status measurements](docs/board-status.md) are available as native animation
and transition input sources.

## Browser workspace and controller APIs

```sh
npm ci
python3 tools/serve_editor.py
```

The [browser workspace](docs/web-editor.md) has Editor, Configuration, Mixer and
Audio Playground tabs. Editor follows Edit → physical Preview → Save locally →
Deploy to device for animations and transitions. Editable source and settings are
imported/exported as external files; there is no browser library or filesystem
integration. Configuration manages device presets, outputs, Wi-Fi and MQTT. Mixer
cues presets on A/B buses with transition selection, manual mixing, cuts, timed fades
and swaps. Audio Playground tunes signal input windows and output ranges.
[MQTT control and Home Assistant lights](docs/mqtt.md) provide optional power and preset selection
through an existing broker, with one light exposing power, master brightness, colour and preset effects per output.
Pico W also supports the [Pico RGB Keypad](docs/mqtt.md#keypad-and-rgb-indicators):
sixteen HA/Matter-style button event inputs (tap, multi-press and hold) and sixteen
independently controlled RGB indicators,
using generic TCA9555 and APA102 drivers under the same HA controller device.
Build with `-DLIGHTVM_MQTT=ON` (and Wi-Fi on embedded boards).
See [configuration and management](docs/configuration.md),
[networking](docs/networking.md) and [power recovery](docs/boot-sequence.md).

## Verification and measurements

For the browser build, install Emscripten 4.0.7 and set `EMCC` if needed.
The [teaching guide](docs/teaching.md) covers the local preview and the
[twelve beginner lessons](examples/lessons/README.md).

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
contracts using fixtures generated by the current compiler. They cover
admission, cooperative execution, faults, buffer ownership, settings validation and generation conflicts,
snapshots, program replacement, bank rotation and startup persistence. Effect
replay executes Color, Pattern, and Rainbow through an independent Python caller
harness. Sprite tests check Stars, Rainbow Sprite and Sweep, including transparency,
motion, frame-independent trails and bounded memory. Composition tests cover each layer blend mode.
Rainbow permits one channel level for f32 arithmetic. Memory telemetry includes contexts and replacement peaks.

For repeatable optimisation work across every supported target, follow the
[optimisation protocol](docs/optimisation-protocol.md). It includes frozen
workloads, historical timings, matched-run commands and regression gates.

Measure the current Rainbow on a controller with:

```sh
build/venv/bin/python tools/benchmark_controller.py --port PORT --board pico_w \
  --pixels 200 --output build/benchmark-pico-w.json
```

The controller command measures the current canonical
Rainbow through the integrated firmware's render/output times and playback
rate through the existing API, temporarily configuring the output and restoring
its prior configuration afterward. Flash saves retain existing blocking behavior.

## License

Original code and documentation: [MIT](LICENSE), copyright 2026 Kaj Wik Siebert.
SDK and toolchain notices remain in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and `licenses/`; distribute these with firmware.

The current vocabulary and breaking API names are defined in
[Controller concepts](docs/architecture.md#controller-concepts). Earlier
experimental installs require the documented
[clean installation](docs/architecture.md#clean-installation-after-experimental-changes).

See [JavaScript and blocks](docs/javascript-and-blocks.md) for the editor modes, language contracts and visual toolbox.

## Shared device services

[shared/device-core](shared/device-core/README.md) provides the Wi-Fi, HTTP,
discovery, configuration and persistence services used by LightVM and kaj-fi.
LightVM binds its output/animation policies through small application adapters;
the audio firmware links the same core without the lighting engine. Discovery
includes device kind and capabilities, and the editor selects light-output peers.
