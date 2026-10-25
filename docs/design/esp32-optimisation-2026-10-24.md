# ESP32 optimisation campaign — 24 October 2026

The C3 worktree now includes main's optimisation protocol and native board
telemetry. Matching experimental `c3`, `c6` and `s3` profiles are implemented and
selected by `LIGHTVM_ESP32_PROFILE=AUTO`. All measured dynamic animation cases
improve in the five paired runs. **All three protocol comparisons nevertheless
fail service criteria:** relative USB p95 increases, and C3/S3 retain occasional
steady ingress gaps above 10 ms. These are throughput-oriented experimental
defaults with a documented latency tradeoff, not new all-gates-passing baselines.
Use `standard` when reproducing the denominator or investigating that tradeoff.

[Structured results](../benchmarks/esp32-optimisation-2026-10-24.json) contain
per-window medians, ranges, service counts and gate dispositions.
[Raw evidence](../benchmarks/esp32-optimisation-2026-10-24.tar.gz) retains successes,
failed attempts, packages, final images, ELFs, maps, configurations and tool sources.
[Screening firmware](../benchmarks/esp32-optimisation-screens-2026-10-24.tar.gz)
retains the earlier and rejected images separately. Every archive member is
hash-verified; [archive hashes](../benchmarks/esp32-optimisation-2026-10-24-archives.json)
identify the two bundles.
The earlier [C3 results](c3-performance-2026-10-24.md) remain a separate historical
cohort with different firmware and observers.

## Source and implementation

Branch `codex/c3-profile-source`, inherited from the original C3 chat. Commit
`b77c3d2` merges main `15096bc` into C3 `babf38d`. Commit `4874f7b` extends the
profiles and temperature tooling; measured firmware is frozen at
`e3c3dd36355a36cd9619e630ac958f83f5233904`. Later changes are measurement tools,
documentation and evidence, not firmware rebuilds.

The candidate adds strict core `-O3`, annotated drawing kernels in instruction
RAM, partition-lifetime mapped storage reads, and 2 ms slice/epoch budgets.
Writes/erases still use the IDF APIs. Both standard and candidate use one actual
render worker, including S3, 160 MHz CPU, DIO 40 MHz flash, 128 KiB VM heap,
256-byte bytecode cache, 1024 pixel capacity, 512 KiB storage and 2 MiB app
partition. There is no fast-math, FMA contraction, PSRAM assumption or clock
increase. Explicit resource overrides remain available. An explicit profile for
the wrong chip fails configuration.

Both final groups include the single-worker collector repair, a 100 ms USB
connection-loss confirmation, full-frame USB enqueue and final-status-only
reply draining. They also share filtered `input_list(name)` and reset-cause
telemetry. Thus the paired comparison isolates the execution profile; it does
not separately estimate the benefit of these common USB/telemetry changes.
The complete frame is copied into the IDF ring before the staging buffer is
reused; final status drains before `app_reply_delivered`.

## Rig and protocol scope

| Board | Controller | Revision | Logical output pin |
| --- | --- | --- | --- |
| XIAO ESP32-C3 | `esp32-d4f98d0449c0` | 0.4 | D1 |
| XIAO ESP32-C6 | `esp32-10bda3999904` | 0.2 | D0 |
| XIAO ESP32-S3 | `esp32-a4cb8fe03008` | 0.2 | D0 |

ESP-IDF 5.5.3, Espressif GCC 14.2.0, macOS host. Boards run concurrently on
separate USB serial connections. Full-service images have Wi-Fi, HTTP,
WebSocket, MQTT and boot-sequence support; they run clean unprovisioned AP
configuration. No authenticated broker or loaded network cohort is implied.
Physical strip wiring, optical output and waveform quality are unverified.

The common lane uses 200 logical WS2812B pixels, brightness limit 32, USB STATUS
20 Hz and native temperature every two seconds on the same client. Eleven
animation and 68 numeric frozen packages were recovered and hash-verified from
the existing protocol evidence. The primary lane uses ten animation cases:
default stochastic Stars is excluded in favour of fixed 20-star controls;
Color, Rainbow, Sweep, map, scalar, draw, twelve sprites, mixed map/scalar and
Stars with/without a cached background remain represented.

Five alternating pairs use AB, BA, AB, BA, AB, reversing case order in even
pairs, followed by one return-baseline block. The predeclared local campaign
uses 12-second windows and two-second warmup (fixed Stars additionally waits for
25 frames). This is shorter than protocol v1's recommended 30-second windows,
seven-second warmup and three return-baseline windows. Approximately 1,200 STATUS
observations per case/firmware meet the comparator's p99 sample minimum, but
these runs do not establish a hard real-time bound or replace the longer recipe.
The first baseline windows overlapped a Pico cross-build; later primary windows
ran without compilation. Host scheduling noise is therefore an explicit limit.

The temperature guard requests only `board.temperature_c`, checks validity and
age ≤2 seconds, and clears outputs/stops the test at ≥80 °C or unavailable/stale
telemetry. This is an operator stop threshold, not a hardware rating or surface
temperature. Full-metadata polling from earlier screens is a different observer
and is not pooled with the final filtered cohort. Native heap/CPU snapshots in
endurance occur between timed windows.

## Paired animation results

Values below are median useful layer completions/second, **standard → AUTO**,
not merely physical output FPS. Full per-window ranges and mean render costs
are in the structured comparison. Cached Color and background layers render
zero additional frames after warmup in both groups.

| Workload/layer | C3 | C6 | S3 |
| --- | ---: | ---: | ---: |
| map | 27.95 → 71.40 | 90.59 → 119.34 | 31.28 → 108.81 |
| scalar | 3.83 → 10.90 | 12.48 → 17.15 | 3.08 → 7.57 |
| draw | 32.88 → 46.26 | 59.52 → 84.82 | 36.70 → 66.15 |
| sprites | 4.74 → 40.52 | 14.15 → 77.32 | 2.75 → 46.36 |
| control-rainbow | 36.13 → 93.74 | 99.20 → 138.51 | 44.52 → 116.45 |
| control-sweep | 17.48 → 73.17 | 43.87 → 131.92 | 8.66 → 85.39 |
| mixed/A/layer0 | 20.65 → 36.78 | 55.92 → 66.56 | 28.03 → 48.77 |
| mixed/A/layer1 | 2.50 → 5.40 | 5.24 → 8.74 | 1.75 → 4.41 |
| stable-stars | 2.41 → 20.13 | 6.83 → 49.98 | 1.33 → 25.80 |
| stars-background | 2.25 → 19.73 | 6.66 → 47.84 | 1.25 → 25.04 |

All dynamic layer rate/cost gates pass: no median rate loss above 5% and no
median render-cost increase above 5%. The fixed image workloads show the largest
gains. Ordinary map/scalar gains on C6 primarily reflect scheduling: their mean
execution costs improve only about 1.5%/2.1%, while useful rates rise 31.7%/37.4%.
The combined profile is measured; this is not an ablation of its four ingredients.

| Fixed 20-star rate range and render cost | C3 | C6 | S3 |
| --- | ---: | ---: | ---: |
| standard | 2.41–2.41 Hz; 102.43 ms | 6.82–6.83 Hz; 60.33 ms | 1.33–1.42 Hz; 197.59 ms |
| AUTO | 20.06–20.14 Hz; 22.10 ms | 49.93–50.02 Hz; 11.50 ms | 25.79–25.80 Hz; 16.40 ms |

The single return-baseline block exposes drift: C3 Rainbow rate/cost differs by
+7.8%/−7.2%, S3 Rainbow by −8.8%/+9.1%, and S3 fixed Stars rate by +6.3%
(with execution cost essentially unchanged). C6 rate drift is at most 1.6%.
Most other controls remain within 1%; the raw record retains every deviation.
Small effects should therefore not be inferred from the large profile gains.

## Responsiveness and open gates

| Candidate, worst case across the ten workloads | C3 | C6 | S3 |
| --- | ---: | ---: | ---: |
| USB p95 (ms) | 8.38 | 7.31 | 7.98 |
| USB p99 (ms) | 11.26 | 8.59 | 10.34 |
| Steady ingress gaps >10 ms, standard → candidate | 1 → 3 | 0 → 0 | 3 → 13 |

All sampled absolute USB p95/p99 gates pass. All three boards fail the relative
p95 regression rule in some workloads (allowed increase is max(10%, 1 ms)).
C3/S3 also fail zero steady ingress gaps. Setup/flash/near-boot intervals and
lifetime foreground-loop maxima remain in the raw data; they are separate from
the steady ingress counter deltas. A rare USB maximum above the p99 bound is
retained rather than discarded, even when its percentile gate passes.

A shorter 1 ms slice screen on C3/S3 reduced image throughput substantially and
did not eliminate ingress gaps, so it was rejected. USB batching and complete
frame enqueue reduced the observer burden; the final standard and candidate
both include them. The retained S3 `usb-frame` screening attempt disconnected
and rebooted at a last observed 66.1 °C, then reconnected around 61.1 °C. Its
cause is unresolved; neither thermal causation nor a clean pass is inferred.

## Correctness, lifecycle and endurance

Each final full-service AUTO image passed exact host-reference smoke checks,
32 stored-program replacement/delete/recreate cycles, four lazy-fault/recovery
modes with 40 invalid/oversized candidate rejections, 180 exact/fragmented USB
boundary requests through 528 bytes, and 96 deterministic scalar/bulk pixel
comparisons at 33, 200 and 1024 pixels. This is the frozen deterministic replay
matrix, not the protocol's entire cross-boundary numerical conformance recipe.

All boards admitted and made progress in two-map, two-image, four-scalar,
asymmetric, eight-layer, two-output and two-layer-per-A/B-slot checks at 200
pixels. Saved-recall fade retirement stopped outgoing work. Five heavy
replacements per board preserved observed old-animation progress; ten
cancellations preserved the live generation and VM heap. Acceptance times were
3.7–8.4 ms across all 15 replacements; completion was about 0.78–1.55 seconds.
These are candidate diagnostics, not matched preparation-speed comparisons.

C6/S3 admitted four scalar layers at 1024 pixels and then replaced the active
configuration with an empty one in 91.7/105.9 ms without pre-clearing. **C3
rejected that composition with a memory-allocation error**, so that required
capacity/replacement gate is not passed. C3 cleanup succeeded. A matched
standard-profile capacity attribution was not measured; this is not labelled
as either a proven new regression or an inherited pass.

The user explicitly stopped the planned full-hour test to avoid waiting.
Completed measured blocks total **240.064 seconds C3, 240.060 seconds C6 and
180.052 seconds S3**, with one boot per board, stable per-scene VM heap and
continued dynamic progress. These are short stability screens. SIGINT cleanup
restored empty configuration and removed temporary programs on all three.
The raw runner's `passed:false`/interruption is preserved with a separate
`endurance-disposition.json`; it is not converted to a one-hour pass. No overnight
job was scheduled. An earlier C6 attempt stopped before any measured block on
a harness brightness-range mistake; that failed attempt is retained separately.

All three USB-only AUTO images completed their separate ten-case screen and
cleanup. A newly stored constant-colour program survived an esptool reboot,
read back byte-for-byte and executed to the expected 25-pixel RGB snapshot on
all three boards. This persistence lane used USB-only firmware. Full-service
AUTO images were then clean-installed and flash-verified as described below.

## Resources and temperature

| Resource, standard → AUTO | C3 | C6 | S3 |
| --- | ---: | ---: | ---: |
| App image bytes | 1,097,552 → 1,136,992 | 1,139,104 → 1,178,544 | 1,000,256 → 1,033,792 |
| Linked shared RAM used (bytes) | 206,092 → 211,772 | 239,294 → 244,978 | 201,015 → 205,887 |
| Linked shared RAM unassigned (bytes) | 115,204 → 109,524 | 212,818 → 207,134 | 140,745 → 135,873 |
| Measured main-task peak in paired windows (bytes of 16,384) | 6,028 → 5,904 | 6,164 → 5,860 | 7,040 → 6,360 |

During the short stability blocks, live VM heap remains 14,588 bytes for mixed
and 17,032 bytes for Stars/background on C3/C6; S3 uses 14,380/16,808 bytes.
Native heap and lifetime minima are retained separately from VM accounting in
each block's `native_sensors_after`. These minima also include the preceding
correctness/lifecycle work on the same boot.

Linked memory figures are SDK region accounting, not runtime free heap. In
particular S3's dedicated 16 KiB IRAM window is fully assigned by the linker;
additional instruction bytes use shared DIRAM. The app images all fit the 2 MiB
partition. Main-task high-water readings do not prove every IDF task's stack
margin, and flat sampled VM heap does not prove allocation-free rendering.

| Final campaign native chip temperature | C3 | C6 | S3 |
| --- | ---: | ---: | ---: |
| Observed range (°C) | 50.1–59.1 | 57.6–68.6 | 56.1–66.1 |

These ranges include matched runs, correctness, short stability and postflight.
No thermal threshold was reached. The historical screening disconnect remains
separate evidence. Quantisation and native die-sensor error are not calibrated.


Host full-service CTest: 29/29; final AddressSanitizer/UndefinedBehaviorSanitizer
CTest: 29/29; focused input/monitor/comparison/runner tests: 45/45. Nine ESP
builds pass (standard/full, AUTO/full and AUTO/USB-only on each chip), as do
nine wrong-chip/invalid-profile rejection checks.

A fresh Pico W Wi-Fi/HTTP build passes its RAM and closed software-stack gates:
14,184 bytes remain after configured reservations; main/worker software margins
are recorded in the archived `pico-clean-stack-paths.json`. The maximum-service
Pico build still fails its RAM gate by 1,328 bytes, matching the inherited main
limitation. An intermediate reused build also failed on stale pre-merge
callgraph files; that report is retained. No physical Pico claim is made.

## Gate disposition

| Gate / measured scope | C3 | C6 | S3 |
| --- | --- | --- | --- |
| Deterministic replay; useful progress/render cost | pass | pass | pass |
| Cached background | pass | pass | pass |
| Absolute USB p95/p99 | pass | pass | pass |
| Relative USB p95 | fail | fail | fail |
| Zero steady ingress gaps >10 ms | fail | pass | fail |
| 200-pixel layer progress; preparation/cancellation subset | pass | pass | pass |
| 1024/four-scalar admission and empty replacement | fail | pass | pass |
| Linked margins | pass | pass | pass |
| All IDF task stacks | unmeasured | unmeasured | unmeasured |
| One-hour endurance | unmeasured: user deferred | unmeasured: user deferred | unmeasured: user deferred |
| USB boundary/storage subset; USB-only reboot execution | pass | pass | pass |
| Final image and clean state | pass | pass | pass |

Unmeasured scope includes authenticated loaded HTTP/WebSocket/MQTT and HA
lifecycle, full task-stack accounting, physical USB unplug during upload,
WS2812/APA102 waveform/optics, optical input-to-visible timing, matched detailed
freshness percentiles, near-full A+B+transition admission, and internal telemetry
cost against the ≤2% target. Capacity correctness at 1024 pixels is not a matched
1024-pixel throughput result. Saved historical scenes retain their old evidence;
they are not silently labelled as newly remeasured here.

## Final images and clean installation

Final full-service AUTO images (application SHA-256):

| Board | Bytes | SHA-256 |
| --- | ---: | --- |
| C3 | 1,136,992 | `d58b3cac4fb363a266e5173ce93878b2d1f71ca75b3fd6e58003fca755d18d10` |
| C6 | 1,178,544 | `59aca30946586f345e7fcbf780c57202d5fa3651a3638530a735c7b90ee56209` |
| S3 | 1,033,792 | `54042c31fcb1fb3a35d44361d89004374a03cd46a93439d8b1277413f6cffc74` |

Bootloader, partition table and app were independently verified with esptool
after a full erase. All boards report empty active/saved outputs, no stored
programs or presets, network revision zero, and no boot/animation error. The
native sensors remain available. These recorded installation identities precede
any subsequent independent noise-feature campaign.

These are disposable test boards. A clean install erases flash, flashes the
matching bootloader/partition/app files from `flasher_args.json`, then verifies
them with `esptool verify_flash`. Start from an empty active/saved configuration
and reprovision the network, outputs, programs and presets explicitly. No
migration or compatibility path for older experimental settings was added.
Do not flash an image onto a different chip; use separate build directories.

To reproduce the paired lane, use the archived package manifest and firmware
hashes with `tools/benchmark_animation_profile.py`, the parameters above, and
`tools/compare_optimisation.py`. The archived `run_acceptance.py`,
`run_validation.py` and `run_postflight.py` record the exact local orchestration;
update their root, toolchain and serial-port paths for another rig. The public
archive contains no full-flash backup or provisioned network credentials.
