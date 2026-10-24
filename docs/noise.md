# Seeded gradient noise

LightVM 1.4.0 provides pure native Perlin and simplex sampling in two and three
dimensions. These are core mathematical operations available to every frontend;
they contain no animation, timer, input-driver or event-delivery policy. The
shared C implementation is built on host, WebAssembly and embedded targets.

Python imports these names from `lightvm`; JavaScript uses the same names. Calls
take positional arguments, with trailing defaults as shown:

```python
perlin2(x, y, seed=0, repeat_x=256, repeat_y=256)
perlin3(x, y, z, seed=0, repeat_x=256, repeat_y=256, repeat_z=256)
simplex2(x, y, seed=0)
simplex3(x, y, z, seed=0)
```

Coordinates are f32 in **[-65536, 65536]**, inclusive, in lattice units. Negative
coordinates use floor-based cells. Seeds are i32: every bit participates in the
hash. Repeat periods are integer **1..65536**, independently for each Perlin
axis. Periods need not be powers of two. Simplex has no user-selected Cartesian
repeat period. Zero, negative and oversized periods fault rather than clamp.
Python requires integer seeds/periods; JavaScript explicitly converts them to
integers as part of frontend lowering. Blocks expose both dimensions and an
integer seed, using the default Perlin periods.

Results are finite f32 in **[-1, 1]**, with canonical positive zero. Map to a
brightness with `(n + 1.0) * 0.5`. Noise is spatially correlated, not a source of
independent random numbers. Changing coordinates smoothly moves through a field;
changing the seed selects a different field and may jump. Perlin is zero at
integer lattice points, so sample fractional coordinates for a useful field.
The bound does not mean every field attains both endpoints.

Nonfinite or out-of-range coordinates fault before writing the result. Subnormal
input coordinates are explicitly zero; subnormal results become positive zero.
This avoids target-dependent denormal behavior. Other results are compared to
independent binary64 equations with absolute tolerance **2e-5**. Scalar and
prepared execution on the same build are bit-identical. Builds disable fused
contraction and fast-math; cross-target bit identity is tested, not assumed from
host results alone. The field/hash contract is independent of libc random state.

## Inputs, motion and triggers

[The noise example](../examples/animations/noise.py) captures `scale`, `phase`,
`field` and `level` once per render invocation. An input ramp supplies smooth
motion; another source can supply the same phase. Time, pause and input snapshot
ownership stay with the normal LightVM scheduler. Sampling neither advances time
nor consumes RNG state, so reordering pixels, splitting work or yielding does
not change a sample. Independent instances can use different seeds concurrently.

For a trigger that selects a new field, publish a latched integer occurrence
count to the `field` input. Repeated snapshots give the same field; a skipped
count selects the latest field without replaying intermediate occurrences.
This is appropriate for reselection. It does **not** provide exactly-once event
handling: general queued trigger callbacks remain proposed. A boolean pulse can
be missed between frames, so do not use a sampled boolean when every occurrence
must be delivered. Future trigger handlers can pass an explicit seed or position
through ordinary owner state without changing these mathematical primitives.

Fractal sums, turbulence, ridges and envelopes remain author-level composition
of these samplers. Keep octave counts bounded and respect the per-pixel work
budget. Noise does not introduce an implicit reseed operation or global table.

## Native and bytecode contract

`src/noise.h` exposes `lvm_noise_eval(binding, args, out)` over typed 32-bit
register words. Failure leaves `out` unchanged. The native call record carries
the following exact signatures (all arguments are required on the wire):

| Binding | Name | Arguments | Result | Work |
| --- | --- | --- | --- | --- |
| 59 | perlin2 | f32 x, f32 y, i32 seed, i32 repeat_x, i32 repeat_y | f32 | 128 |
| 60 | perlin3 | f32 x, f32 y, f32 z, i32 seed, i32 repeat_x, i32 repeat_y, i32 repeat_z | f32 | 192 |
| 61 | simplex2 | f32 x, f32 y, i32 seed | f32 | 128 |
| 62 | simplex3 | f32 x, f32 y, f32 z, i32 seed | f32 | 192 |

Use opcode 38 (`NATIVE`) with its call-record index, normal register types and
zero effect rights. Loader validation requires a manifest floor of 1.4.0 even
for a typed callback or unused call record. The container layout is unchanged.
The operation catalogue generates signatures and work costs for C and Python.
No declaration-expression encoding is added: noise runs in callbacks.

Prepared kernels admit these calls after verifying all operands, dependencies,
permissions and forward control flow. Plans capture register identities once;
workers never read call records from the bytecode store. Prepared instructions
are 12 bytes (four bytes more per instruction), charged to the existing VM heap.
There is no per-instance noise allocation, cache, permutation table or scratch
resource. Bounded stack locals hold at most eight Perlin corners or four simplex
corners. Each call reserves its full work cost before execution, including faults;
the existing 1024-unit first-pixel budget and ordered fallback still apply.

## Mathematical construction and provenance

The implementation is independently written from the gradient-noise equations:
Ken Perlin's [improved noise construction](https://cs.nyu.edu/~perlin/noise/)
uses quintic interpolation, while simplex sampling uses a skewed lattice and
compact radial corner weights. No third-party implementation or permutation
table is included. The exact fields are LightVM's own contract.

The hash applies an unsigned 32-bit avalanche to `seed ^ 0x9e3779b9`, then to
each successive xor with x, y and z multiplied by `0x85ebca6b`, `0xc2b2ae35`,
and `0x27d4eb2f`. The avalanche is xor-right-16, multiply `0x7feb352d`,
xor-right-15, multiply `0x846ca68b`, xor-right-16. Its value modulo 12 selects
the cube-edge gradients listed in `src/noise.c`; 2D projects them onto xy.
Perlin hashes wrapped cell corners, interpolates with `6t^5-15t^4+10t^3`, then
scales by one half. Simplex hashes unwrapped integer vertices, uses the standard
2D/3D skew and unskew constants, radial cutoffs 0.5/0.6, fourth-power weights
and scales 70/32. Final results clamp to [-1,1]. Independent Python equations
and explicit wire fixtures exercise the contract without importing runtime code.

Recompile noise programs and install a 1.4.0 core before deploying them. There is
no storage-format change or migration. Existing programs retain their own feature
floors; upgrading the compiler does not require noise support for other programs.

## Validation — 24 October 2026

- 2,848 samples checked against independent binary64 equations, within 2e-5;
  native and WebAssembly sampler results matched bit for bit.
- Eleven noise tests passed on the host VM, the full WebAssembly VM, and with
  AddressSanitizer/UndefinedBehaviorSanitizer. These cover malformed call records,
  all seven operands, invalid domains, lazy faults, work limits, scalar/prepared
  agreement, yields, RNG independence and input snapshot changes.
- All 23 CTest suites passed on the host and with sanitizers. Actual worker-thread
  tests include concurrent noise instances and compare complete output/state.
- Python regression discovery: 333 tests, 11 environment-dependent skips, no
  failures. Web frontend/UI suite: 163 passed, 11 skips. TypeScript and production
  web builds passed. Generated metadata and bytecode fixtures are current.
- Pico W/RP2040 Release firmware built with GCC 13.2.1 and SDK 2.3.1, networking
  disabled. SRAM reservation gate passed with 20,336 bytes remaining after the
  configured reserves. Closed software stack accounting passed: main 11,496 of
  16,384 bytes including interrupt allowances; worker 5,104 of 8,192. The compiled
  noise evaluator's own frame is 224 bytes. Hardware timings and stack watermarks
  were not measured for this change.

The example uses 1,226 package bytes, 24 registers and 28 instructions, including
one prepared pixel map. Older runtime tests were adjusted for the new application
release. The sweep schedule fixture now resets after STOP, so wall-clock rendering
between activation and STOP cannot alter the explicit schedule's starting time.

Reproduce the host checks with a normal CMake build and CTest, then:

```sh
LIGHTVM_RECEIVER="$PWD/build/host/lightvm" PYTHONPATH=.:tools:tests \
  python3 -m unittest discover -s tests -p 'test_*.py'
LIGHTVM_NUMERIC_RUNTIME="$PWD/build/host/test_platform" npm test
npm run build
```

For a standalone WebAssembly sampler comparison with Emscripten and Node:

```sh
emcc -O2 -ffp-contract=off -Isrc src/noise.c tests/noise_driver.c \
  -sENVIRONMENT=node -o build/noise-wasm.cjs
LIGHTVM_NOISE_WASM="node build/noise-wasm.cjs" python3 tests/test_noise.py
```

The full VM run uses the root CMake project with Emscripten's toolchain, builds
`test_platform`, and enables `-sNODERAWFS=1 -sENVIRONMENT=node
-sSTACK_SIZE=524288 -sINITIAL_MEMORY=33554432 -sEXIT_RUNTIME=1`. Set
`LIGHTVM_NUMERIC_RUNTIME` to an executable wrapper that runs that module with
Node, forwarding its arguments, then run `tests/test_noise.py`. The wrapper's
module must use CommonJS (`.cjs` or a directory with `type: commonjs`).

## Hardware performance and live input measurements

`tools/benchmark_noise.py` freezes eight packages: one sample per pixel for each
function, with scalar and prepared (`bulk`) execution of the same source. A phase
Ramp animates the normal timing lane. The live lane publishes scale, phase and a
latched field count at 10 Hz while polling STATUS at 20 Hz. Separate settled
replays check every RGB byte against frozen host hashes, including repeated and
skipped field counts, the sign bit of the seed and restoring the original field.

The initial host validation passed 192 reference comparisons across 200, 500 and
1,024 logical pixels, plus 24 timing and 24 live-input windows. These validate
the harness, not embedded performance. Hardware measurements require the board
optimisation branch's `tools/board_monitor.py` and filtered temperature telemetry.
It checks temperature on the same connection every two seconds, including warmup,
and stops outputs at 80 °C or on invalid/stale readings. This is a test threshold.

Prepare once with the same host source as the board images, keeping these package
bytes and references unchanged throughout the hardware cohort:

```sh
python3 tools/benchmark_noise.py --prepare --process build/host/lightvm \
  --packages build/noise-hardware/packages
python3 tools/benchmark_noise.py --port /dev/cu.REPLACE_WITH_DEVICE \
  --board xiao_esp32c3 --pin D1 --firmware /absolute/path/lightvm.bin \
  --packages build/noise-hardware/packages --label c3-noise \
  --result build/noise-hardware/c3.json \
  --temperature-log build/noise-hardware/c3-temperature.jsonl \
  --pixels 200 500 1024 --samples 5 --seconds 10 --events 8
```

Select S3/C6 with their board IDs and verified output pins. Firmware flashing and
device ownership are explicit operator steps; the runner never flashes devices.
Keep firmware hashes, SDK/configuration, rig details and the raw JSON alongside
the [optimisation protocol](optimisation-protocol.md) record. An earlier result
path is refused so failed attempts remain evidence. Existing configuration is
restored and temporary programs removed; thermal failures leave outputs stopped.

Report completed renders/s, render cost (including VM, coordinate and RGB work),
actual publisher cadence, command acknowledgement latency, input-to-composite
readback latency, and the firmware's distinct `dirty_to_display_us` separately.
The last two are different boundaries; neither is an optical measurement. The
current reuse proof accepts straight-line expressions only, so these pixel loops
and prepared maps report `render_reusable=false` even with unchanged inputs.
Completed render rate can therefore exceed the live publisher rate and is not a
count of distinct input values consumed. Logical pixels do not establish physical
strip length. No per-call microbenchmark or isolated numeric-cycle claim is made.
