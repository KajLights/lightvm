# Focused runtime experiment

Read [REPORT.md](REPORT.md) for measurements and limits. All additions are
experimental; `LIGHTVM_RUNTIME_BENCHMARK` defaults off. The browser workflow and
ordinary firmware defaults are unchanged.

## Build the portable inputs

Use the repository Python requirements and Zig 0.13.0:

```sh
python experiments/runtime/build_kernels.py --zig /path/to/zig
python experiments/mqtt/build.py --zig /path/to/zig
python experiments/mqtt/build.py --zig /path/to/zig --bulk-memory \
  --output build/runtime-bench/service-bulk.wasm
```

The optional bulk variant uses standard `memory.copy` and `memory.fill` instructions.
It retains the one-page memory declaration and all MQTT application code. Build
JSON records hashes; original and variant artifacts have different output paths.

## Matched host builds

```sh
cmake -S . -B build/runtime-wasm3-host -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_FLASH_EXPERIMENT=ON -DLIGHTVM_INTERPRETER=wasm3 \
  -DLIGHTVM_RUNTIME_BENCHMARK=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_MQTT_EXPERIMENT=ON
cmake --build build/runtime-wasm3-host --parallel 4

cmake -S . -B build/runtime-toy-host -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_FLASH_EXPERIMENT=ON -DLIGHTVM_INTERPRETER=toywasm \
  -DLIGHTVM_RUNTIME_BENCHMARK=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_MQTT_EXPERIMENT=ON
cmake --build build/runtime-toy-host --parallel 4
```

An existing pinned checkout can be provided through `WASM3_SOURCE_DIR` or
`TOYWASM_SOURCE_DIR`; otherwise CMake fetches the pinned dependency. Run `ctest`
and Python tests using `LIGHTVM_RECEIVER` as described in the MQTT experiment.
HTTP/socket regressions require access to local loopback sockets.

```sh
python experiments/runtime/benchmark.py --process build/runtime-toy-host/lightvm \
  --label host-smoke --samples 3 --repetitions 1 --seconds 0.3 \
  --output build/runtime-bench/host-smoke.json
```

Host runs validate the harness and outputs; they are not MCU performance evidence.

## C3 builds and measurement

Activate ESP-IDF 5.5.3. Set `IDF_COMPONENT_MANAGER=0` in the build environment as
well as the project configuration so the bootloader sub-build stays offline.
Use separate build directories for the interpreter variants:

```sh
export IDF_COMPONENT_MANAGER=0
idf.py -C platforms/esp32 -B "$PWD/build/runtime-wasm3-c3" \
  -D LIGHTVM_BOARD=xiao_esp32c3 -D LIGHTVM_WIFI=ON -D LIGHTVM_HTTP=ON \
  -D LIGHTVM_BOOT_SEQUENCE=ON -D LIGHTVM_MQTT_EXPERIMENT=ON \
  -D LIGHTVM_FLASH_EXPERIMENT=ON -D LIGHTVM_INTERPRETER=wasm3 \
  -D LIGHTVM_RUNTIME_BENCHMARK=ON build
```

For Toywasm, change the build directory and `LIGHTVM_INTERPRETER=toywasm`.
The initial build uses `LIGHTVM_TOY_JUMP_TABLES=OFF` and
`LIGHTVM_TOY_ASSERTIONS=ON`. The short dispatch trial uses jump tables ON and
assertions ON; the measured release variant uses both jump tables ON and assertions
OFF. These toggles apply to the interpreter library; SDK assertions are unaffected.

Before flashing, record settings and preserve a full 4 MiB backup in a new ignored
directory, following [the MQTT backup/restore workflow](../mqtt/README.md).
Do not overwrite an earlier backup. Flash the build's `flash_args`; it includes
the experimental code partition. The runner temporarily replaces the animation
and hardware configuration, then attempts cleanup, but does not restore firmware.
Always restore and verify the complete original image after the experiment,
including on failures.

```sh
python experiments/runtime/benchmark.py --port /dev/cu.usbmodem1101 \
  --label c3-wasm3-flash --output build/runtime-bench/c3-wasm3-flash.json
```

Defaults are 21 samples, three repetitions and five-second playback windows.
Samples are checkpointed into JSON. `runtime_bench` accepts only the four kernel
exports, 1–10,000 iterations and 1–21 samples. Playback and the service must be
stopped. Every native/guest result is compared. The native 1 KiB volatile buffer is
an additional static benchmark cost common to these builds.

To isolate MQTT preparation costs, use the diagnostic probe:

```sh
python experiments/runtime/benchmark.py --port /dev/cu.usbmodem1101 \
  --label c3-bulk-probe --service-only --probe \
  --service build/runtime-bench/service-bulk.wasm \
  --output build/runtime-bench/c3-bulk-probe.json
```

The runner first attempts ordinary startup under the unchanged 2 ms limit. It then
stops animation/service scheduling before the optional diagnostic probe, whose
explicit 500 ms ceiling measures initialization separately. The probe targets
device loopback, records three initializations and 21 disconnected steps, closes
the socket and is followed by unload. It is not a live broker/traffic benchmark
and must not be presented as meeting the normal service deadline.

Firmware hashes in `results/firmware.json` identify the actual measured images.
The original release image was read back from the device before the diagnostic
build replaced it. Full-flash backup and restoration logs are kept under ignored
`build/runtime-bench-c3-backup-20261014/`; the report contains no credentials.
