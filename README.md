# LightVM

LightVM is a [Kajlights](https://kajlights.com/) runtime for programmable lighting
on microcontrollers and native hosts. The browser or a computer compiles supported
Python source into a portable `.lbc` program. Controller firmware executes the
program and sends data to the LEDs.

Start with [controllers and the development workflow](docs/architecture.md) for
outputs, animation versions, layers, presets, storage and startup. The browser
supports Edit → Preview → Save animation → optional Compose → Save preset.

The [animation profile](docs/animation-profile.md) defines persistent buffers,
arrays and the `@init` / `@render` callbacks. Caller arguments supply context,
time, delta and pixels; settings are frozen at initialization.
Every animation can run alone or as a scene layer. Frames start transparent.
Ordinary colour writes paint solid pixels; alpha is optional. Use `clear()` to remove paint
from an array, and `fill(rgb(0, 0, 0))` when solid black is required.
The current compiler emits LBC1/4 packages.
Up to eight independent device slots run concurrently. Each output can also run
a [native scene](docs/composition.md) with independent background and transparent
foreground animations. The [PixelArray and sprite SDK](docs/pixelarray-sprites.md)
adds shared artwork, user-defined sprite callbacks, optional channels and native
geometry/colour transforms. Multiple languages and a broader component system
remain future work.

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
- [`examples/scenes/`](examples/scenes/): combinations of those animations on one output.
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
60-second default cycle; its `duration` setting accepts startup arguments such
as `{"duration": 3.3}`.

```sh
build/venv/bin/python -m lightvm.compiler examples/animations/rainbow.py \
  -o build/examples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT --status
build/venv/bin/python tools/client.py --port PORT \
  --store-procedure rainbow build/examples/rainbow.lbc
build/venv/bin/python tools/client.py --port PORT \
  --config examples/config/pico-startup.json --save
build/venv/bin/python tools/client.py --port PORT --activate rainbow
```

Use the configuration matching the board and actual wiring. For XIAO ESP32-C3,
use `examples/config/xiao-c3-startup.json` and add `--no-reset` to serial commands;
S3/C6 use `examples/config/xiao-startup.json`. The device keeps its configured pin,
count, order and brightness when changing programs. Reboot runs each device’s
`startup.procedure` with its `startup.args`; omitted arguments use program defaults. `STATUS` reports `mode=bytecode` and `code_storage=flash`
for both named and transient programs. Uploads share one 256-byte page buffer;
there is no package-sized buffer in an animation instance.

Uploading a new version under the same stored name preserves the current running
version in pinned flash until explicit activation. Activation prepares a separate
candidate and retains the old instance on failure. Startup resolves the current
catalogue entry for each configured procedure name; saving a replacement changes
what runs on the next boot. Root-level `targets` and `startup` are unsupported.

LightVM is a PoC moving toward alpha and supports only the selected current
contracts: LBC1/4, execution ABI 4, `lightvm.program/11`, `animation/1`, internal
binding 3 and premultiplied RGBA contributions. There is no legacy compatibility.
Recompile earlier programs from source and rebuild the browser and firmware
together. Browser libraries reject obsolete artifacts without deleting saved data.
The management wire format is `LVM1`, version 1; earlier framing is rejected.
Storage accepts directory format 4 only. Unsupported committed storage stays
untouched and unavailable for writes, with a repair/reprovision diagnostic.
Back up data and explicitly reprovision such devices; cleanup/build tests never
flash hardware or erase device data. Current-format interrupted-write recovery
and content-pinned running revisions remain supported.

## Firmware builds

Pico and Pico W output use asynchronous PIO/DMA by default. ESP32 uses asynchronous
RMT and prefers DMA on chips that support it, with ordinary RMT for additional
outputs or unavailable DMA resources. Host and RP2040 use the root
CMake project. ESP32 uses its SDK project. Resource budgets remain in board
headers; bytecode contains no GPIO, controller or driver selection.
The [Pico output probe](tests/pico_output/README.md) verifies the production driver.

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

The [browser workspace](docs/web-editor.md) follows Edit → physical Preview → Save
animation → optional Compose → Save preset. It keeps editable animation versions
and exportable presets in a browser library, and saves shared compiled programs
and compact preset recipes on the device. Preview addresses the selected output
over HTTP/USB. The editor and example dropdown share the canonical sources.
See [configuration and management](docs/configuration.md),
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
contracts using fixtures generated by the current compiler. They cover
admission, cooperative execution, faults, buffer ownership, settings validation and generation conflicts,
snapshots, procedure replacement, bank rotation and startup persistence. Effect
replay executes Color, Pattern, and Rainbow through an independent Python caller
harness. Sprite tests check Stars, Rainbow Sprite and Sweep, including transparency,
motion, frame-independent trails and bounded memory. Scene tests cover each layer blend mode.
Rainbow permits one channel level for f32 arithmetic. Memory telemetry includes contexts and replacement peaks.

Measure the current Rainbow on a device with:

```sh
build/venv/bin/python tools/benchmark_device.py --port PORT --board pico_w \
  --pixels 200 --output build/benchmark-pico-w.json
```

The device command measures the current canonical
Rainbow through the integrated firmware's render/output times and playback
rate through the existing API, temporarily configuring the output and restoring
its prior configuration afterward. Flash saves retain existing blocking behavior.

## License

Original code and documentation: [MIT](LICENSE), copyright 2026 Kaj Wik Siebert.
SDK and toolchain notices remain in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and `licenses/`; distribute these with firmware.
