# Multicore validation — 20 October 2026

The implementation uses both Pico cores, defaults to readiness-driven refresh,
and retains an optional refresh cap. The [architecture and resource guide](multicore-rendering.md)
describes ownership, bounded rendering epochs, buffering and build options.

## Hardware and method

Tests used the attached Pico 2 W, SDK 2.3.1, Arm GNU 13.2.Rel1, Release builds,
and the SDK's configured 150 MHz system clock. Wi-Fi, HTTP, WebSocket, MQTT and
the boot journal were enabled. The radio remained in setup-AP mode; commands
used USB. GP16 and GP17 drove simulated WS2812 outputs with no strips attached.
These measurements exercise PIO/DMA completion, including reset time, but do
not constitute electrical or visible-LED verification.

The original complete flash was saved and verified before installation.
The installed final UF2 SHA256 is
`23f44b712318e555f913adc574910a52ccbfd5be46c3ff749cec322b43f9a7e1`.
The original empty output configuration was restored and temporary benchmark
programs were removed.

Comparisons use identical bytecode, geometry and resource budgets. The one-core
comparison uses the new scheduler with `LIGHTVM_RENDER_WORKERS=1`, retaining
the same 192 KiB VM heap, 1024-byte instruction caches and stack reservations.
DCP is enabled in both core-count comparisons. The separate software-math
comparison disables `LIGHTVM_PICO2_DCP` with two render cores.

## Refresh and rendering

The same explicitly seeded rainbow bytecode produced:

| Firmware / pixels | Completed output FPS | Median render time |
|---|---:|---:|
| Original firmware, 200 pixels | 59.90 | 868 µs |
| New firmware, 200 pixels | 155.24 | 573.5 µs |
| New firmware, 500 pixels | 64.45 | 1085.5 µs |
| New firmware, 1024 pixels | 31.71 | 1939 µs |

Each autonomous interval lasted about four seconds. Render timing uses 20
samples after five warm-up frames. The longer chains demonstrate that the
default follows the output's capacity, including rates below 60 Hz.

For 200-pixel kernel workloads, splitting one render across two cores reduced
median pure-map execution from 1020 to 702 µs, and transformed-image execution
from 2665 to 1899 µs. Their output rates remain near the wire limit.

For compute-heavy scalar bytecode, completed **per-layer renders per second**
provide a better comparison than composite presentation counts:

| Scene | One core | Two cores |
|---|---:|---:|
| Four layers on one output | 5.20 each | 9.08 each |
| Two layers on each of two outputs | 5.16–5.41 | 8.80–9.30 |
| Two layers in each A/B slot | 5.19–5.43 | 8.58–8.91 |

Those intervals lasted approximately four seconds with one core and six with
two. They show about 1.7× aggregate rendering throughput. Layers publish
independently, so composition FPS can increase whenever a subset of layers
finishes; it should not be reported as a CPU speedup. The fixed-output workloads
also make deterministic frame comparisons possible.

With two cores, DCP reduced median transformed-image rendering from 2497 to
1899 µs, approximately 24%. It is therefore enabled by default on Pico 2 W.
The initial link experiment was ineffective because an SDK helper supplied
interface-only options. The final implementation uses executable linker
options and the build gate checks the actual linked arithmetic wrappers.

In the final five-scene run, USB status p95 latency was 3.15–4.53 ms while
rendering. This is a short-run measurement, not a worst-case response guarantee.
Raw results also retain worker assignments, worker elapsed time, stack watermark,
bytecode hashes and deterministic frame CRCs.

## Correctness and build checks

- The full host Python suite passed: 274 tests, one skipped. The additional
  enabled-DCP linkage regression test passed in the nine-test Pico build suite.
- All 16 full-feature CTest cases and all 10 one-worker/100 Hz capped cases passed.
- The real concurrent executor contract passed ThreadSanitizer. It compares
  VM registers and frames, nested jobs, and repeated initialization/shutdown.
- All 32 transform/math frames matched byte-for-byte between the host,
  Pico software double arithmetic and Pico DCP. Cases cover reflections,
  fractional scale/alpha/origin, wrap boundaries, large signed offsets,
  colour-wheel trigonometry and hue conversion.
- Hardware workloads cover pure maps, resident image transforms, scalar layers,
  two outputs and A/B composition. Repeated program writes/replacements and
  VM resets during playback passed, including twelve cycles per benchmark run.
- Paired map/image VMs passed exact autonomous-versus-manual frame comparisons,
  exercising kernels inside simultaneous VM jobs. Peak observed worker stack
  use was 2544 bytes against the default 8192-byte reservation. Restoring the
  empty output configuration returned admitted VM heap usage to zero.
- Builds and ELF gates passed for Pico W and Pico 2 W with USB only, Wi-Fi
  without HTTP, and full HTTP/WebSocket/MQTT features. One-core and DCP-disabled
  Pico 2 variants passed as well.

Reservation results include a 16 KiB native operating reserve and both stacks:

| Profile | VM ceiling | Bytes left after reservations |
|---|---:|---:|
| Pico W, USB | 128 KiB | 22,736 |
| Pico W, Wi-Fi without HTTP | 96 KiB | 35,052 |
| Pico W, all network features | 64 KiB | 2,148 |
| Pico 2 W, USB | 256 KiB | 154,124 |
| Pico 2 W, Wi-Fi without HTTP | 256 KiB | 129,240 |
| Pico 2 W, all network features | 192 KiB | 120,612 |
| Pico 2 W, custom profile | 256 KiB | 84,044 |

The custom profile uses 512 pixels/output, two HTTP clients, a 2048-byte
instruction cache and a 4096-byte worker stack. It demonstrates independent
resource tuning; it is not the default.

## Reproduction and remaining physical checks

Run `tools/benchmark_controller.py --port DEVICE --board pico2_w --pixels 200`
for the rainbow comparison. Use `tools/benchmark_multicore.py --port DEVICE
--seconds 6 --output results.json` for maps, native drawing, layers and A/B
cases. `tools/check_render_math.py --port DEVICE --output math.json` records
the numerical vectors; repeat with `--process build/host/lightvm` and compare
the JSON documents exactly.

The committed [measurement data](benchmarks/pico2-multicore-2026-10-20.json)
retains the comparative results. Local flash backups, logs and individual runs
are under `build/multicore-hardware/`.

Physical RP2040 testing, connected WS2812/APA102 signal verification, sustained
Wi-Fi/WebSocket/MQTT load, and long-duration endurance remain outside this
attached-board run. SRAM code placement, a shared module cache, extra PIO
allocation and hardware interpolators remain profiling candidates; they were
not needed for the measured gains and are not enabled speculatively.

The subsequent [original Pico W validation](multicore-rp2040-validation.md)
covers RP2040 hardware with a connected GP16 strip, a simulated GP17 output,
and direct performance comparisons against these Pico 2 W results.
