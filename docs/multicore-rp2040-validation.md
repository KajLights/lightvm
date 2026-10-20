# Original Pico W multicore validation — 20 October 2026

The RP2040 board benefits from the same multicore firmware. The attached
200-pixel rainbow increased from 59.95 to 149.50 completed output frames per
second. Compared with the new scheduler restricted to one core, two cores
reduced pure-map rendering time by 40% and transformed-image rendering time by
38%, and increased four-layer scalar throughput by 70%.

## Hardware and method

The attached original-generation board identifies as **Pico W**, with an RP2040
and the SDK's configured 125 MHz clock. GP16 has a 200-pixel WS2812 GRB strip;
GP17 is unconnected and was used for the second output and longer simulated
strips. Benchmark brightness was limited to 32/255. No oscilloscope or external
optical measurements were taken.

Firmware is commit `48b98d0`, built in Release mode with SDK 2.3.1 and Arm GNU
13.2.Rel1. Wi-Fi, HTTP, WebSocket, MQTT and the boot journal remain enabled.
Commands used USB. Both new builds use the same 64 KiB VM ceiling, 256-byte
instruction cache, 16 KiB core-0 stack, 8 KiB core-1 reservation and 500 µs VM
slice target. The one-core build sets `LIGHTVM_RENDER_WORKERS=1`; the final
build uses two workers. RP2040 does not enable the RP2350 DCP path.
The existing station-network configuration was retained; the final reboot
check found Wi-Fi connecting and MQTT disconnected. Network connectivity and
traffic were not controlled benchmarks.

All workload playback intervals last approximately six seconds. Rainbow
render timings use 20 samples after five warm-up frames. The seven composition
cases use three manual frames, followed by autonomous playback with USB status
polling. Bytecode hashes and frame CRCs match between the original, new
one-core and new two-core firmware. Autonomous frames match the corresponding
manual reference.

## Refresh and rendering

| Rainbow workload | Original Pico W firmware | New Pico W, one core | New Pico W, two cores | Previous Pico 2 W, two cores |
|---|---:|---:|---:|---:|
| 200 pixels, output FPS | 59.95 | 149.19 | 149.50 | 155.24 |
| 200 pixels, median render µs | 2805.5 | 2901 | 1937.5 | 573.5 |
| 500 pixels, output FPS | — | — | 62.61 | 64.45 |
| 1024 pixels, output FPS | — | — | 30.96 | 31.71 |

The 200-pixel rainbow is output-limited with either new core configuration.
Using the second core reduces CPU time while the readiness-driven scheduler
provides most of the refresh gain. Longer chains settle below or near 60 FPS
without a fixed refresh target.

| Composition workload, 200 pixels | Original output FPS | New one-core output FPS | New two-core output FPS |
|---|---:|---:|---:|
| Pure map | 56.23 | 132.90 | 150.27 |
| Transformed image | 45.18 | 68.83 | 109.12 |
| Two map layers | 45.54 | 73.22 | 117.99 |
| Two transformed-image layers | 28.65 | 37.09 | 59.06 |

Pure-map median execution fell from 5833 to 3483 µs between the new one-core
and two-core builds; transformed-image execution fell from 12125 to 7570 µs.
Paired-layer cases also exercise native kernels inside simultaneous VM jobs.

For scalar workloads, compare **completed renders per layer per second**.
Composition output FPS can count partial updates from independently finishing
layers and is not a CPU-throughput measurement.

| Scalar scene | Original Pico W | New Pico W, one core | New Pico W, two cores | Previous Pico 2 W, two cores |
|---|---:|---:|---:|---:|
| Four layers on one output | 2.09 | 1.65 | 2.81 | 9.08 |
| Two layers on each of two outputs | 2.08 | 1.64 | 2.63 | 8.80–9.30 |
| Two layers in each A/B slot | 2.07 | 1.65 | 2.49–2.98 | 8.58–8.91 |

The new one-core scheduler pays a throughput cost for shorter cooperative work
slices. Two cores more than recover that cost: four-layer throughput is 34%
above the original firmware and 70% above the new one-core build. For the
scalar scenes, status-request p95 latency fell from approximately 54–97 ms
originally to 5.74–5.76 ms with two cores. Across all seven two-core scenes,
p95 latency ranged from 5.74 to 16.46 ms; native image operations can exceed
the nominal VM slice duration.

The Pico 2 W comparison uses the earlier measurements, not a simultaneous
retest. Its 150 MHz RP2350, DCP, larger instruction caches and memory profile
differ from this RP2040 configuration. Simple scenes approach the same output
limit, while the Pico 2 W retains substantially more CPU capacity.

## Correctness, resources and observed limitation

- All 32 transform/math frames match the host and the previous Pico 2 W
  software-math and DCP runs byte-for-byte.
- Each complete original/one-core/two-core workload run passed twelve program
  replacements and VM resets during playback.
- A 60-second two-output pure-map run completed at 104.84 FPS on GP16 and
  104.89 FPS on GP17, with no animation errors. Three live VM resets passed;
  both frame CRCs matched, and admitted VM memory stayed at 12,496 bytes.
  Status-request p95 latency during this run was 10.53 ms.
- Peak observed core-1 stack use was 2708 bytes against an 8192-byte reservation.
  Both RP2040 builds passed the RAM and flash-overlap gates. The full-feature
  RAM budget leaves 2148 bytes after accounting for the VM ceiling, both
  stacks, maximum driver allocations and 16 KiB native operating reserve.
- The updated benchmark tools also passed host workload, rainbow and math
  runs. Board identity now comes from capabilities, which remains available
  when the existing scene is in transition mode.

**Scene preparation remains synchronous.** Replacing two transformed-image
layers with four scalar layers while the old scene continues playing took
11.072 seconds with one core and 6.966 seconds with two. The first one-core
attempt exceeded the client's five-second timeout; the board continued
rendering and recovered on reconnect. The successful comparison used a
30-second timeout, now exposed by `benchmark_multicore.py --timeout`.
This changes the benchmark's allowance, not firmware preparation latency.
Bounded or asynchronous scene preparation is a remaining responsiveness
improvement, separate from autonomous playback throughput.

## Storage and reproduction

The original board uses **1 MiB storage**, so both new builds require
`-DLIGHTVM_STORAGE_BYTES=1048576`. An initial build using the 512 KiB default
could not mount the existing catalog. The verified complete flash backup was
restored before repeating the tests with the matching layout. No storage
migration was added.

Use the commands in [the previous validation report](multicore-validation.md),
with `--board pico_w` for the rainbow benchmark, `--pixels 200`, and
`--seconds 6`. Use GP17 for the 500- and 1024-pixel simulated chains.

The [raw measurements](benchmarks/pico-w-multicore-2026-10-20.json) retain
workload hashes, frame comparisons, timings and RAM reports. Local flash
backups, logs and individual runs are in `build/multicore-rp2040-hardware/`.
The performance-run firmware UF2 SHA256 was
`527e465e60744edbfedbb863465d5e1c81ac700af821b0993c553cd6a87aebfb`.

After testing, the board was rebooted into the final two-core firmware.
The original active/saved GP16 configuration, all eight programs, preset
catalog and network configuration were verified unchanged. The original
rainbow preset resumed and advanced 267 output frames over the two-second
check, with no boot or animation errors. GP17 was released.

Sustained network traffic, long-duration endurance, APA102 output and electrical
signal quality remain unmeasured in this run.

## Follow-up: Home Assistant preset-switch freeze

The subsequent HA test exposed a main-stack overflow missed by the USB
benchmarks. An authenticated MQTT command selecting the existing `stars`
preset froze rendering and USB responses on the deployed firmware.

The compiler allocated 6328 bytes for the HA message callback and 2592 bytes
for its enclosing HA tick. These remained live while command dispatch and
preset loading entered another 5400-byte manifest-parser frame, exceeding the
16 KiB main stack once the intervening calls were included.

HA JSON parsing now completes in a separate, non-inlined helper before engine
dispatch. The message callback uses 608 bytes on both Pico W and Pico 2 W;
the main-stack reservation, VM budget and dual-core rendering remain unchanged.
MQTT-enabled Pico builds now check compiler stack-usage reports for this
callback, its parser and HA tick. The gate rejects the actual pre-fix report.
These individual frame budgets are a regression check, not a complete
whole-program stack bound.

The fixed Pico W passed 40 alternating rainbow/stars switches, brightness and
colour updates, and ten OFF/ON cycles through its real broker, including
interruptions during the presets' 1500 ms fades. All 62 commands received
matching MQTT state publications; measured acknowledgement latency was
258 ms median and 434 ms maximum. USB remained responsive. A further 40-second
idle check retained MQTT connectivity and advancing animation frames, with
no offline publications, animation errors or reboot.

All 47 targeted MQTT, light-control, preset and Pico-build tests passed.
Both Pico W and Pico 2 W firmware builds passed RAM and HA-stack checks.
The original programs, presets and configuration were preserved, and the
original rainbow/brightness/colour settings were restored.

The installed corrected Pico W UF2 SHA256 is
`32686ed6e33d395dac6d5271ce7ed6ead0ad09d4d9ff385e9fc1fcbe6f249cd8`.
[Regression measurements](benchmarks/pico-w-ha-regression-2026-10-20.json)
record the command counts, keepalive observations and compiler frame sizes.

## Follow-up: stars rendering regression

The live `stars` preset exposed a mixed-workload regression that the initial
uniform-layer benchmarks missed. Output presentations were advancing around
103 FPS while the stars layer itself completed only 5.39 FPS. The background
and an invisible outgoing rainbow kept producing work.

Code-only flashes of the verified original backup allowed comparison without
replacing storage. These runs use the unchanged saved programs, 200-pixel GP16
configuration and stars settings: capacity 20, duration 2 seconds, probability
5 births/second and white stars over the dim colour background. Each sample
followed seven seconds of warm-up and lasted about twelve seconds.

| Firmware | Stars layer FPS | Output presentations/s | USB status p95 |
|---|---:|---:|---:|
| Original single-core | 22.91 | 22.99 | 33.07 ms |
| Regressed multicore, with HA stack fix | 5.39 | 103.20 | 13.27 ms |
| Corrected multicore | 26.48 | 99.68 | 10.03 ms |

Stars births are stochastic, so these are observed rates rather than an exact
speedup guarantee. Output presentations include updates that reuse the previous
stars contribution; they must not be reported as the stars animation rate.

The correction:

- Lets child compositions run while the transition's complete back buffer waits
  for DMA. A prepared composition can also finish an already-started slow layer.
  It does not start additional fast-layer frames while blocked. The buffers
  currently being read remain pinned.
- Stops autonomous work on the hidden source of a completed preset recall.
  Manual A/B takeover resumes both existing instances without resetting them.
- Uses smaller serial native chunks when both cores already have VM work,
  instead of reserving a large chunk on the assumption that it will split.
- Packs solid fill colours once and reuses colour-transform results for adjacent
  identical input pixels. Per-pixel reference tests verify exact results.
- Selects a 2000 µs default VM slice for Pico W. The original 500 µs slice spent
  too much time cycling through service and presentation for this RP2040 scene.
  Pico 2 W retains 500 µs, and `LIGHTVM_RENDER_SLICE_US` overrides either default.
  This is a cooperative work quantum, not a frame interval or refresh cap.

All eight hardware workloads passed manual/autonomous frame comparisons and
twelve flash-replacement/reset cycles. The 32 transform/math frames still match
the host exactly. Final output rates were 147.80 FPS for one pure map, 89.60 for
one transformed image, 109.51 for two map layers and 47.56 for two transformed
layers. Scalar layers completed 3.28–3.45 FPS with four layers, 3.27 FPS on two
outputs, and 3.44 FPS in both A/B slots.

The added fixed twelve-sprite/background workload produces identical pixels on
the original firmware, corrected firmware and host. Its sprite layer measured
29.26 FPS originally and 26.72 FPS after correction, with status p95 improving
from 17.35 to 9.78 ms. This remains a throughput tradeoff for bounded scheduling;
the change does not provide a universal speedup. The image workloads also remain
slower than the initial multicore measurements, although faster than the original
single-core firmware. Network traffic was not controlled between those runs.

The benchmark now clears output runtimes between cases: applying the same
hardware configuration deliberately preserves scenes, which otherwise carried
the preceding A/B scene into the new mixed-layer comparison.

All 130 targeted Python tests, 16 native CTest checks and 10 capped-build checks
passed. Pico W and Pico 2 W builds passed RAM and HA-stack gates. Observed worker
stack use was 2684 bytes; Pico W's RAM reservation margin remains 2148 bytes.
The Pico 2 W build was checked but was not hardware-tested in this follow-up.

The final authenticated HA run passed 40 preset switches, ten OFF/ON cycles and
62 acknowledged commands. Acknowledgements measured 282 ms median and 464 ms
maximum. A further 40-second check retained MQTT connectivity, the same boot ID
and advancing stars, with no offline publications or animation errors. Stars
averaged 31.16 layer FPS during this check. The original configuration, programs,
presets and network settings were verified preserved; `stars` was left running.
The temporary local broker credential file was removed after testing.

The installed firmware SHA256 is
`6950b5e5ddab18d5f0a15edc71c0ddc4a689e270bec0300ca1110ceb0014e812`.
[Credential-free measurements](benchmarks/pico-w-stars-regression-2026-10-20.json)
include the slice experiments, workload results and original program hashes.
