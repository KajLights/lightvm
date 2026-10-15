# Portable bytecode PoC results

Measured 15 October 2026 on the connected XIAO ESP32-C3, 160 MHz, ESP-IDF 5.5.3.
The final flash-resident rainbow renders 200 pixels in **1.208 ms**, against
**1.053 ms** for native C: **1.148× native time**, or approximately **15% overhead**.
All 3,000 matched benchmark frames were byte-for-byte identical. No performance
threshold was imposed.

The PoC compiles **color, pattern, chase, wipe, twinkle and rainbow** from the
adapted Python sources. Every package was uploaded, validated, prepared and
activated from flash. Rainbow remains installed and playing on D1/GPIO3 at
brightness 16/255. Output is configured for 200 WS2812 pixels in GRB order.
Electrical/optical strip behaviour has not been independently observed; the
measurements verify software output and actual RMT completion.

## Native comparison

Three 1,000-frame runs, alternating which implementation runs first on each
frame. Values below are means of elapsed render-call time within each run.
Interrupts are included; LED transmission, USB handling between frames and
scheduler waiting between frames are excluded.

| Run | Flash bytecode | Native C | Ratio | Pixel mismatches |
|---|---:|---:|---:|---:|
| 1 | 1,208.231 µs | 1,052.935 µs | 1.1475× | 0 |
| 2 | 1,208.308 µs | 1,052.895 µs | 1.1476× | 0 |
| 3 | 1,208.233 µs | 1,052.900 µs | 1.1475× | 0 |

The comparison includes simulation, preparation and shading with matching f32
operations, default speed 0.1 and scale 1.0, phase initialization and RGB8 colour
quantization. The native implementation is ordinary optimized C, compiled with
`-O3 -ffp-contract=off`; the interpreter uses the same flags. Native rendering is
split into 32-pixel calls. The device interpreter runs 64-instruction slices,
which cover approximately 32 rainbow pixels in its optimized shader.

Both implementations use the versioned native HSV/colour conversion. Rainbow's
phase, parameters and pixel expression remain compiled bytecode. The result is
specific to this workload and API: it does not establish 15% overhead for arbitrary
integer loops, other effects or general Python. The earlier approximately 20×
component-runtime result used a different integer workload and is not a matched
speedup comparison.

## Optimisation evidence

| Version measured on C3 | Bytecode/frame | Native/frame | Ratio |
|---|---:|---:|---:|
| First PoC implementation | 2,514 µs | 1,147 µs | 2.19× |
| Aligned decode/static-check cleanup | 2,467 µs | 1,146 µs | 2.15× |
| Combined arithmetic and HSV-return operations | 1,970 µs | 1,152 µs | 1.71× |
| Specialized colour helper, bitwise finite checks, shorter slices | 1,208 µs | 1,053 µs | 1.15× |

The last step improves the shared native helper as well as the interpreter.
Disassembly inspection showed that the native loop had specialized constant
saturation/value, while the interpreter's colour case still called a general
helper. Making that specialization explicit removes redundant conversions without
changing the colour result. IEEE binary32 finite checks use exponent bits rather
than software floating-point comparisons. Arithmetic fusion preserves separate
multiply/add rounding; FMA contraction is disabled.

The final rainbow package is **328 bytes**, with 21 declared registers and 12
instructions across its entry points. Its pixel path uses two generic operations:
integer-to-float/multiply/add, and full-saturation HSV return. There is no
rainbow-effect opcode or embedded native implementation selected by package name.
The independent native function exists solely as the benchmark/visual reference.

## Playback and services

Five-second windows with real asynchronous RMT output and roughly 50 USB pings
per second. Output rate is derived from the recorded submission gaps.

| Metric | Bytecode | Native |
|---|---:|---:|
| Output rate | 59.996 fps | 59.987 fps |
| Render time, median / p95 | 1,215 / 1,232 µs | 1,048 / 1,065 µs |
| Maximum render slice | 214 µs | 204 µs |
| Output gap, p95 / maximum | 17.599 / 17.618 ms | 17.647 / 17.688 ms |
| Maximum service gap | 1.223 ms | 1.176 ms |
| USB round-trip, median / maximum | 0.646 / 1.724 ms | 0.603 / 1.752 ms |
| Missed logical tick deadlines | 0 | 0 |
| Runtime/retention errors | 0 | 0 |

These are observed values, not worst-case bounds. The roughly millisecond frame
jitter includes the FreeRTOS tick and cooperative yielding. The driver consumes
the same backing allocation written by bytecode and transformed by the core.
The final sequence verified **849 completed output holds** without buffer mutation
before the reboot tests.

## Flash and replacement

Successful evidence includes:

- Six distinct compiled effect packages installed and run from flash.
- Live replacement between different programs without a firmware reflash/reboot.
- A transport-CRC failure and a well-formed upload containing an invalid opcode
  rejected without replacing the active program.
- Valid bytecode that faults while preparing its first frame rejected while the
  existing rainbow continues (separate `prepare-failure.json`).
- Restart after an actual partial flash upload, retaining the previous active
  rainbow revision; a further restart again boots that revision from flash.
- On-device validation and initialization before persistent activation, bounded
  upload staging, and no full package copy or expanded instruction stream in RAM.

**Persistent updates still interrupt smooth playback.** In the mixed replacement
sequence the maximum output gap was **92.214 ms**, the maximum USB service gap
was **43.008 ms**, and 15 tick deadlines were dropped across the sequence. Erases
and writes wait for RMT completion so cache stalls do not interrupt a live refill,
but the CPU still stalls during the actual flash operation. This implementation
does not claim uninterrupted effects/services during persistent installation.
Pre-erasing during a separately scheduled maintenance window or platform-specific
flash strategies would require further work and measurements.

The restart test covers interruption during upload, not exhaustive electrical
power cuts at every byte of the activation record. CRC-protected records and
fallback validation are implemented; this is not a production power-loss audit.
State migration is not implemented: a replacement starts from its initial values.

## Memory

| Resource on C3 | Measured/accounted size |
|---|---:|
| One bounded interpreter context | 548 bytes |
| Complete effect object including program metadata/context/view | 636 bytes |
| One native 200-pixel buffer | 600 bytes |
| Rainbow package in flash | 328 bytes |
| Upload chunk payload | 128 bytes |
| Linker-accounted shared instruction/data RAM | 73,932 bytes |
| Steady-state accounted whole-image RAM | 99,692 bytes (97.4 KiB) |
| Conservative SDK-accounted peak through replacement | 101,052 bytes (98.7 KiB) |

Whole-image accounting is static instruction/data RAM plus total internal heap
minus current/minimum free internal heap. The peak uses SDK low-watermarks rather
than claiming an exact simultaneous allocation census. It includes runtime and
SDK stacks/queues, old/new effect coexistence, mapping bookkeeping and buffers.
It also includes instrumentation: 8 KiB of timing sample arrays, separate native
benchmark buffers/context, and USB buffers. It is not a minimal release footprint.
The complete experimental application image is 186,368 bytes according to the SDK
size report; packaged `.bin` size is recorded separately in provenance.

The fixed context reserves 128 register slots even though rainbow needs only 21.
Further context compaction is possible; this PoC has not optimized that allocation.
Flash mappings/cache resources are platform costs; they are not a per-package
expanded RAM instruction representation.

## Verification and reproduction

The host sanitizer run passed eight Python test groups plus the C runtime tests.
Checks include 72 effect/count/frame combinations against the original Python
samples, fused/unfused agreement, 5,000-frame native equality, malformed headers
and instructions, dynamic palette/pixel bounds, division faults, infinite-loop
work exhaustion, sealed/stale native views and resumable execution. Matching
simulation ticks with 30/60 Hz rendering produce matching state and frames.
These are regression/adversarial tests, not a security audit.

The host and C3 execute identical package bytes. A second MCU family has not yet
been exercised. The PoC does not implement the complete architecture: general
helper calls/call frames, richer resource manifests, multiple simultaneous effects,
network services, composition, production recovery and application integration
remain separate work. No WASM comparison or runtime was added.

[README.md](README.md) contains build and demo commands. The `results/` directory
contains final raw samples, previous optimization measurements, memory reports,
preparation-failure evidence and artifact/source SHA-256 digests. Full flash
backups remain local under ignored `build/portable-bytecode/backup/`.
