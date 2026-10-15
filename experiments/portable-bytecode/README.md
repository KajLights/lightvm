# Portable bytecode PoC

> Historical standalone experiment. The current deliverable is integrated into the
> normal firmware; use the [repository instructions](../../README.md) and
> [bytecode profile](../../docs/control-api.md). Do not deploy the standalone
> firmware below for normal device operation: it replaces the device services.

This opt-in implementation compiles six Python lighting effects into portable
`LBC1` packages, runs them directly from mapped flash on a XIAO ESP32-C3, and
compares rainbow with native C. It does not change the production firmware or
compiler. See [REPORT.md](REPORT.md) for measured results and limitations.

## Run on the host

Python 3.9+, a C11 compiler and a POSIX host are sufficient. No Python source is
executed by the compiler. The original checked-in Python samples are executed
only by the independent regression oracle.

```sh
python3 experiments/portable-bytecode/build.py
python3 experiments/portable-bytecode/test_poc.py
build/portable-bytecode/test-runtime build/portable-bytecode/rainbow.lbc
build/portable-bytecode/host build/portable-bytecode/rainbow.lbc 200 5000 bench

python3 experiments/portable-bytecode/build.py --sanitize
python3 experiments/portable-bytecode/test_poc.py --sanitize
build/portable-bytecode/test-runtime-sanitized build/portable-bytecode/rainbow.lbc
```

`host` maps the package read-only and interprets those bytes. Without `bench` it
writes the final RGB8 frame to stdout. Package output is under ignored
`build/portable-bytecode/`. Compile an individual source with:

```sh
python3 experiments/portable-bytecode/compiler.py \
  experiments/portable-bytecode/samples/rainbow.py \
  -o build/portable-bytecode/rainbow.lbc
```

`--no-fusion` disables arithmetic and colour instruction fusion for comparisons.
The `.json` sidecar describes parameters and register IDs; the device validates
the binary independently and does not trust the sidecar.

## Device build and demo

The standalone firmware uses ESP-IDF **5.5.3**, the XIAO ESP32-C3 at 160 MHz,
200 WS2812 pixels on **D1/GPIO3**, GRB wire order and brightness **16/255**.
Wiring is firmware/deployment data, absent from the program package. USB client
scripts require `pyserial==3.5`; there is no WASM toolchain or runtime dependency.

```sh
python3 experiments/portable-bytecode/build.py --device
# Override --idf and --idf-tools for other SDK locations.
# If CMake is not on PATH, pass --cmake-bin /path/to/cmake/bin.
```

Before installing this experimental firmware, save a full board-flash backup.
The original experiment backup is retained locally under
`build/portable-bytecode/backup/`, with a size and SHA-256 record. Keep full flash
images out of Git: they may contain private device configuration.

Flash the generated `build/portable-bytecode/firmware/flash_args` with esptool
from that directory. Firmware installation is a one-time prerequisite; subsequent
program uploads use only the resident USB service and program flash slots.

```sh
build/venv/bin/python experiments/portable-bytecode/device_client.py \
  --upload build/portable-bytecode/rainbow.lbc --command play

# Change the executable program without rebuilding/reflashing firmware.
build/venv/bin/python experiments/portable-bytecode/device_client.py \
  --upload build/portable-bytecode/pattern.lbc --command play

# Native comparison requires the default rainbow package and default parameters.
build/venv/bin/python experiments/portable-bytecode/device_client.py \
  --upload build/portable-bytecode/rainbow.lbc --benchmark 1000 \
  --measure 5 --output build/portable-bytecode/measurement.json

# Full sequence: all six effects, three native comparisons, live replacement,
# corrupt/invalid package rejection, interrupted upload, reboot persistence.
build/venv/bin/python experiments/portable-bytecode/exercise_device.py
```

`--port` overrides `/dev/cu.usbmodem1101`. `--command play` resumes autonomous
playback; `stop` holds the last submitted frame. Measurement stops playback so
samples can be read consistently. The full exercise leaves rainbow running.

For native visual comparison: stop, send `mode native`, then play. `mode bytecode`
returns to the uploaded program. Native mode is the reference rainbow with default
speed/scale. Both paths use the same buffer, transformation, brightness and RMT
submission. `rate 30` or `rate 60` changes presentation frequency while logical
updates remain 60 Hz. Parameter writes use `parameter REGISTER RAW_U32_BITS`;
float values use IEEE binary32 bits. Writes during an invocation are rejected
and can be retried at an idle boundary. Parameter persistence is out of scope.

## Source profile and retained effects

These are explicit adaptations of `samples/python/`, with `lightvm_poc` imports
marking the new numeric/lifecycle contract. `lightvm_poc` is a compiler-recognized
namespace, not a Python runtime installed on the controller.

- `rainbow`: the moving HSV gradient, with speed and spatial scale.
- `chase`: a coloured moving marker, retaining scalar position and signed speed.
- `wipe`: growing paint from both ends, retaining progress.
- `twinkle`: the original polynomial pixel/seed variation and cubic fading.
- `pattern`: repeat/stretch a palette stored in flash; an ordinary bytecode loop
  writes a checked view of the native allocation.
- `color`: a bounded native fill of the same allocation.

`@step` mutates persistent scalar state at logical ticks. `@prepare` computes
frame-local globals. `@shade` reads stable state and returns a colour for each
pixel. Alternatively, `@kernel` writes `pixels[index]` or calls `pixels.fill()`.
Simulation and rendering have separate entry APIs; tests compare 30/60 Hz
presentation at matching ticks. The C3 profile drops overdue wall-clock deadlines
and slows logical simulation during overload rather than taking an unbounded
catch-up loop or a giant delta. A retained frame is never partially published.

Numbers are explicit **wrapping i32**, **IEEE f32**, bool and packed RGB8. Integer
floor division/remainder follow Python's sign rules; division by zero and
`INT_MIN / -1` fault. Float-to-integer conversion truncates and traps outside i32.
Float division/remainder by zero fault. Non-finite colour inputs fault. Overflow
in other float arithmetic follows binary32. HSV keeps the original quantized
hue/channel conversion, using f32 inputs. The sample replay tests allow at most
one RGB channel level for rainbow/twinkle versus the original f64 Python profile;
the matched f32 native rainbow must be byte-for-byte identical.

The subset supports scalar declarations, parameters, immutable RGB palettes,
assignments, arithmetic, comparisons, conditionals, and `range(bound)` loops in
kernels. Helpers include rgb/hsv, hash01, min/max, int/float and len(palette).
There are no general objects, allocation, user-defined helper calls, recursion,
exceptions or source imports on the device. Registers default to typed zero;
render kernels must redraw the full output and do not receive preserved RGB
history after the native output transform.

## Interpreter and ownership

The compiler emits typed register bytecode; constants and register initialization
templates stay in flash. The interpreter retains bounded registers, PC, work
counter and native-view metadata. No code expansion or full package RAM copy is
performed. Programs are shared: the benchmark creates another independent
instance referencing the active flash package.

Admission validates type/role descriptors, all operand types, destination
permissions, branch targets, palettes, entry boundaries and final termination
instructions. Execution retains dynamic checks for arithmetic, palette/pixel
indexes, view generation/ownership and total work. Guest integers cannot become
pointers. Single-worker scheduling keeps the native view stable within each
slice, allowing one ownership check per slice instead of per channel store.

Each resume runs at most 64 instructions in the device profile. Native fill is
limited to 32 pixels per dispatch. Ordinary suspension retains the exact PC and
registers; the two-million-instruction invocation limit is a fault, not a yield.
Faults stop playback without publishing the unfinished buffer. General state
rollback after a fault/cancellation is not implemented; a new installation/reset
starts from its declared initial values.

Optimisations are generic: fixed-width aligned decode, register operands,
admission-time static checks, combined integer-to-float/multiply/add with separate
f32 rounding, and HSV-return specialization for constant full saturation/value.
There is no rainbow-effect opcode, native code generation or device JIT.

The core owns one RGB allocation per active/preparing effect. Bytecode stores,
native fill, RGB-to-GRB/brightness and asynchronous RMT all use that allocation.
Submission seals it until RMT completion; a checksum verifies it was unchanged
while held. Replacement waits for the old render/transfer to finish before freeing
its allocation and unmapping its package. The driver may have its own peripheral
symbol storage; this is not a claim that the entire device uses only one buffer.

## Persistent installation

The two 64 KiB partitions contain a 4 KiB metadata sector followed by the package.
Uploads use one 128-byte staging buffer. Only required sectors are erased.
Erase/write operations wait for the current LED transfer to complete, avoiding
flash-cache stalls during RMT refills. Execution and USB continue between writes;
individual flash operations still stall the CPU and can interrupt frame pacing.

The inactive package is CRC checked, independently validated in slices and run
through a complete preparation frame before activation. Its 24-byte activation
record contains a generation, package length/CRC, record version and record CRC.
The record is written last. Activation occurs at a render/output boundary.
Boot checks both records and packages, selecting the newest usable generation.
Failed/incomplete uploads preserve the active record. Generation exhaustion is
rejected rather than wrapped. The test covers restart during a partial upload;
it does not establish every possible electrical power-loss timing.

## Package encoding

All fields are little endian, and the mapped base is four-byte aligned.
The fixed header is 64 bytes:

| Offset | Field |
|---|---|
| 0 | `LBC1` magic |
| 4 | format version 1 |
| 8, 12, 16 | register, instruction and palette-colour counts |
| 20, 24, 28 | step, prepare and render instruction offsets |
| 32 | render mode: 1 = shade, 2 = buffer kernel |
| 36–63 | reserved zero |

Follow with 8-byte register descriptors (type byte, role byte, two reserved zero
bytes, initial u32 bits), 8-byte instructions (opcode/a/b/c bytes and immediate
u32), and packed u32 palette colours. Limits: 128 registers, 2,048 instructions,
1,024 palette entries, 20,000 package bytes. Numeric opcode IDs are in `vm.h` and
`compiler.py`; verifier tests cover their contract. This is a versioned experiment,
not the final production package format or complete architecture manifest.

The RMT encoder is reused from the repository's ESP-IDF-derived encoder. Preserve
its Apache-2.0 notice and the repository's `THIRD_PARTY_NOTICES.md` and licenses
when distributing firmware.
