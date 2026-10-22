# Attached Pico W performance comparison — 22 October 2026

**Comparison complete; candidate not accepted.** Fixed sprites improve 50.1%
and timed births 43.9%, but balanced images regress 27.1%, recall/preparation
latency worsens, and a large live scene cannot admit configuration replacement.
The exact original firmware and all saved state have been restored and verified.

See the [planning tracker](pico-performance-plan.md),
[structured results](../benchmarks/pico-performance-hardware-2026-10-22.json),
[raw measurements and fixtures](../benchmarks/pico-performance-hardware-2026-10-22-raw.tar.gz)
and [SHA256 manifest](../benchmarks/pico-performance-hardware-2026-10-22-manifest.json).
Private flash/network backups are excluded from the published archive.

## Method and identity

The attached device is Pico W `E6614103E7236D37`, RP2040 B2/ROM version 3, running
at 125 MHz with 200 WS2812 pixels on GP16. Both images use SDK 2.3.1, GCC 13.2.1,
two render workers, a 2,000 µs cooperative slice, 256-byte instruction cache,
64 KiB VM heap, 16/8 KiB main/worker stacks and uncapped output. The candidate
adds a 4,000 µs render-epoch target. Wi-Fi and the original authenticated local
MQTT broker remain connected throughout timed tests. An independent subscriber
records command traffic; no production automation is deliberately changed.

The HA load uses MQTT 3.1.1 QoS 0 and waits for each applied-state
acknowledgement, then pauses 30 ms before the next ordinary dimming command.
Those payloads are 18 bytes; the measured command rates are 12.76/s baseline
and 13.39/s candidate. Each broker-observed sequence exactly matches the 1,062
scripted commands plus restoration, with no extra commands during these runs.

The exact attached baseline was saved and verified before any firmware change.
A private 2 MiB backup also preserves all 13 original stored objects and network
credentials. It is excluded from published evidence. Baseline code/padding
SHA256, excluding the journal and storage:
`7fcadd399912be0c60246bb3508282cd332029c3220642d2eb61426f89c7bc6d`.
The baseline's source association is `59b7c52`; the physical bytes are the
reproducibility identity, not an assumed clean rebuild.

The initial optimisation image is `1d9b7ea`, UF2 SHA256
`9d372f9bc1886416ca3236dc32a08191d6fb39e7bd4878cbf20f9a2daf7cd3d8`.
The final tested candidate adds the output-ownership correction, UF2 SHA256
`b07036cc64210154409eab8af7f33c680709d45a1b3ba61c9942eb2ad3c2002f`.
Code hashes distinguish the images; raw-bin and UF2 hashes use different formats.
Compiler packages are archived separately with source and bytecode hashes. The
primary comparison reuses the **same baseline-compiler bytecode** on both images.
Current-compiler and creation-time source changes are tested separately. Program
bytes are hash-identical in the primary comparison, but their flash extent
addresses were not pinned across firmware blocks. The results establish these
measured deployments; they do not isolate scheduling, instrumentation and
code/data cache effects from one another.

The main six workloads each receive five 30-second samples after a seven-second
warm-up, with case order reversed on alternate trials. Firmware order is initial
candidate screening, the full baseline, calibration, then the final candidate;
this is varied block order, not randomly alternating every individual firmware
trial. USB status is sampled at 20 Hz. Pooled p99 latency uses more than 1,000
samples per primary case. Bootstrap intervals resample complete trials and are
descriptive with only five repetitions; they do not include systematic observer
or boundary-timestamp uncertainty. Render/job times measure elapsed execution
intervals, including internal waits; they are not hardware-sampled core CPU
utilisation. Worker job occupancy and join time are reported separately.

A-only composition presentation counters live under `A.frames`. An early harness
read the unused root transition counter and recorded zero for these presentations.
Those fields are excluded from summaries, and the corrected harness retains
both boundary snapshots. Layer frame/render-time and command latency results were
unaffected. The saved-preset and A/B transition counters were correct.

## Repeated 200-pixel comparison

Five trials per case are complete for both images, with 3,005 USB latency samples
per case per image. Rates are **completed layer renders per second**. Current
firmware also records distinct contributions whose output transfer completed;
for fixed twelve sprites this is 39.98/s versus 40.01 completed renders/s.

| Workload / contribution | Baseline updates/s | Candidate updates/s | Change |
| --- | ---: | ---: | ---: |
| Fixed twelve sprites | 26.65 | 40.01 | +50.1% |
| Time-indexed births | 32.18 | 46.30 | +43.9% |
| Two image layers, each | 46.83 | 34.14–34.17 | −27.1% |
| Asymmetric fast map | 90.11 | 79.82 | −11.4% |
| Asymmetric slow scalar | 4.30 | 3.50 | −18.6% |
| Four scalar layers, each | 3.33 | 2.73–2.80 | −16.0% to −18.0% |
| A/B scalar layers, each | 3.40–3.50 | 2.76–2.80 | −17.7% to −20.9% |

Trial-bootstrap change intervals remain clear of zero: fixed sprites
+48.6% to +50.1%, timed births +43.1% to +44.5%, and the first image layer
−27.5% to −24.8%. These are matched comparisons with the attached corrected
multicore baseline. They do not treat the historical single-core 29.26/s
measurement as an identical fixture or build.

| Workload | Baseline USB p95 / p99 | Candidate USB p95 / p99 |
| --- | ---: | ---: |
| Fixed twelve | 8.72 / 9.79 ms | 8.55 / 9.21 ms |
| Timed births | 9.97 / 10.52 ms | 9.09 / 9.43 ms |
| Images ×2 | 19.69 / 20.92 ms | 11.16 / 13.87 ms |
| Asymmetric | 9.99 / 10.42 ms | 10.80 / 11.40 ms |
| Scalars ×4 | 11.79 / 13.76 ms | 8.23 / 10.00 ms |
| A/B scalars | 11.37 / 11.82 ms | 8.19 / 9.36 ms |

All six steady rendering cases meet the candidate USB p95 ≤15 ms and p99 ≤25 ms
targets, without exceeding the allowed p95 regression against their baseline.
The throughput losses nevertheless fail the independent 5% dynamic-layer gate.
The maximum recorded render epoch reaches 10.53 ms in the image case, slightly
exceeding the 10 ms service target before counting other foreground work.

The primary comparison, source and observer runs are complete.
Other completed evidence:

- Unchanged saved stars on the final candidate: median **25.87 → 32.34** layer
  updates/s (+25.0%) over five trials. The initial candidate measured 33.15/s. The reusable background makes no further renders,
  versus about 101/s before. These populations are stochastic; deterministic
  fixtures carry the throughput gate.
- Both archived and current compiler packages reproduce the baseline hashes for
  every fixed population (0/1/6/12/20), each creation-time variant, 300 seeded
  animated frames and 650 timed-birth frames. Brightness changes occur at animated
  frames 100 and 200. Observable clocks, counters, inputs and settings match;
  private guest arrays and the internal RNG state are not exposed by this API.
- The original one-frame ownership defect is reproduced by the baseline HA
  stress and by a failing native regression. The correction excludes only the
  affected physical output while a synchronous light refresh services a nested
  checkpoint; direct, composition and transition cases are covered.

## Larger outputs and layer fairness

Secondary cases use three 20-second samples after seven seconds of warm-up.
The 1,024-pixel candidate rows use explicit scene release before configuration
replacement, following the admission failure described below. This changes the
setup regime, not the measured steady rendering window.

| Pixels | Workload / representative layer | Baseline updates/s | Candidate updates/s | Change |
| ---: | --- | ---: | ---: | ---: |
| 500 | Fixed twelve | 27.43 | 36.52 | +33.1% |
| 500 | Images ×2, layer 0 | 23.02 | 19.92 | -13.5% |
| 500 | Scalars ×4, layer 0 | 1.40 | 1.15 | -17.8% |
| 1024 | Fixed twelve | 29.97 | 31.32 | +4.5% |
| 1024 | Images ×2, layer 0 | 12.64 | 11.19 | -11.5% |
| 1024 | Scalars ×4, layer 0 | 0.65 | 0.55 | -15.4% |

At 200 pixels, eight identical map layers span **28.56–29.41/s** on the
baseline and **18.63–24.87/s** on the candidate. Every layer progresses,
but the fastest/slowest median ratio grows from 1.030 to 1.335.
USB p95 improves from 22.02 to 12.90 ms. The two-map
case falls from about 108.60 to 73.54 updates/s per layer. This is a scheduling/fairness
regression with no observed starvation, not proof that the fairness target is met.
The two-map USB p95 also rises from 10.25 to 11.47 ms (+11.9%), exceeding the
relative 10% target despite remaining below the absolute 15 ms limit.

## Preparation, control and freshness

| Measurement | Baseline | Final candidate | Decision |
| --- | ---: | ---: | --- |
| Heavy activation acceptance, median | 2,020.61 ms | 9.42 ms | Acceptance target passes |
| Heavy activation completion, median | 2,020.61 ms | 3,590.44 ms | 77.7% slower; improvement target fails |
| Busy USB status, p95 | Blocked inside activation | 17.13 ms | Still above 15 ms target |
| Concurrent HTTP status, maximum | About 2,051 ms | 106.10 ms | Old scene remains active while preparing |
| Ten cancellations, maximum completion | No async API | 20.27 ms | Old generation preserved |
| HA recall/power scenario, p95 | 300.18 ms | 1,349.81 ms | Latency target fails |
| 1,000 alternating dimming controls, p95 | 62.42 ms | 58.38 ms | Ordinary control target passes |
| Dimming maximum | 231.77 ms | 219.27 ms | Ordinary control target passes |
| Sampled output retry observations during stress | 10 | 0 | Ownership fix validated |
| 100 input events: composite readback p95 | 61.99 ms | 78.29 ms | Freshness comparison regresses |

Preparation acceptance and applied-state HA acknowledgement are different
boundaries. The busy test alternates back-to-back USB STATUS and operation
queries while an HTTP observer waits 50 ms between completed requests. This is
a heavier polling regime than the 20 Hz steady-rendering matrix. The old scene keeps rendering during candidate construction. Heavy
initialisation uses roughly 0.94 seconds of measured preparation execution,
while wall completion is roughly 3.59 seconds: service/render interleaving is a
substantial cost. This is an observed tradeoff, not a claim that asynchronous
construction met its completion target. Coalescing cheap preparation units
within a shared ingress budget is a follow-up experiment, not a shipped change.

Candidate dirty-to-driver-completed-output p95 is 43.93 ms. That internal stamp
cannot be compared directly with the baseline's host-to-composite-readback
measurement. Neither is an optical measurement. A cached-valid background's
old capture timestamp also does not make unchanged content stale.

The first post-flash MQTT attempt timed out before the readiness precondition
was enforced; the connected rerun is the timed comparison. The initial
cancellation harness wrongly required immediate terminal cleanup in the cancel
response. Corrected trials wait for the documented safe-boundary termination.
Both unsuccessful attempts are retained separately and are not counted as passes.

## Observer cost and displayed contribution age

Boundary-only runs (one descriptive 30-second trial per case) give fixed/image/
scalar rates of 29.03 / 50.96 / 3.70 per second on the baseline and
43.62 / 36.89 / 3.00 on the candidate. Compared with the repeated 20 Hz STATUS
runs, the observer reduces throughput by about **8–10% baseline** and **7–9%
candidate**. These single-trial overhead estimates are descriptive. Removing
polling does not remove the image/scalar regression.

Adding 20 Hz layer-state queries to STATUS polling reduces candidate fixed
sprites to 33.67/s and four scalar layers to about 2.20/s. Each diagnostic case
returns 601 status and 601 layer snapshots; no requested polling slots are
observably lost in these runs. This external observer cost does not settle the
separate ≤2% internal basic-telemetry gate, which remains unmeasured.

Under that diagnostic load, sampled displayed-contribution age p95 is 65.01 ms
for fixed sprites, 33.42 ms for the asymmetric map, 673.64 ms for its scalar
layer, and 886–889 ms for the four scalar layers. These are poll-weighted current
contribution ages, not independent per-frame event latencies. The cached-valid
background is excluded from freshness conclusions: its unchanged capture age
grows while dependency validity remains true. The baseline cannot supply the
same provenance fields without modifying its exact binary.

## Creation-time sprite work

This comparison holds the final firmware and archived compiler fixed. Each
source variant receives five 30-second trials. Fixed geometry and style are
constant in this fixture; the live-brightness style in real stars must remain
in its frame callback.

| Twelve-sprite source | Median updates/s | Change from original source |
| --- | ---: | ---: |
| Original | 40.01 | +0.0% |
| Geometry at creation | 43.57 | +8.9% |
| Geometry and constant style at creation | 52.40 | +31.0% |

The current compiler also reduces ten-frame fixed-twelve dispatch counts to
3,570 / 2,970 / 2,250 for original / geometry / geometry-and-style source. Their
measured manual-render execution times are 136.69 / 121.96 / 102.66 ms. These
manual timings are one deterministic replay, not the repeated autonomous-rate
comparison above. The seeded animated current source takes 3,000.91 ms for
300 manual frames versus 3,421.64 ms for its original source on the same current
compiler; both retain identical pixels and observable state.

## Calibration and deferred decisions

A 6 ms epoch and a 6 ms/512-byte instruction-cache build were tested as separate
calibrations. Neither recovered image throughput. The larger cache lifted the
four-scalar case to about 3.00 updates/s, still below the baseline 3.33/s, with
status p95 around 12.15 ms and observed epochs above 10 ms. Keep the default
4 ms/256-byte profile; one screening trial is not sufficient evidence to adopt
an extra per-instance memory cost.

Image-pair join waiting exceeds 10% of render-epoch time, so the plan's P6 gate
for investigating a shared job batch is now open. A new executor is not yet
implemented. The fixed ownership correction also changes executable layout;
the final image's image-drawing cost differs materially from the earlier
candidate. The cause is not established. Preserve exact binaries and measure
layout/cache effects before attributing all cost to scheduling.

## Resources, endurance and limits

The actual B2 ROM's direct helper paths fit the existing stack allowances; see
[stack accounting](pico-stack-accounting.md). Unknown debug/vector-transfer
entries remain explicit. The ownership fix passes 16 native CTests, the Python suite
(309 tests reported, two skips; final audit/measurement checks also pass), and strict Pico W/Pico 2 W/no-network Pico builds.
The candidate ran **3,753 seconds (62.55 minutes) on one boot** through the full
campaign. The final dedicated HA soak was 120.76 seconds; this is a mixed-workload
continuous-operation test, not an hour-long fixed HA load. There were no observed
resets or sampled output-retry errors on the corrected candidate. The final
62-command scenario still has p95 1,349.39 ms / maximum 1,397.32 ms, confirming
the recall-latency regression.

| Stack measure | Main | Worker |
| --- | ---: | ---: |
| Reservation | 16,384 bytes | 8,192 bytes |
| Observed high-water use | 5,740 bytes | 2,684 bytes |
| Unused at observed peak | 10,644 bytes | 5,508 bytes |
| Linked bound including IRQ allowance | 12,152 bytes | 5,300 bytes |

During the 1,000 dimming controls, live VM heap remains flat at **28,608 bytes**
(candidate) versus 22,496 (baseline). The campaign-wide VM peak is 64,604/65,536
bytes. The Pico W static reservation margin remains positive at 1,760 bytes,
separate from its 16 KiB native operating reserve. These observations support
scoped stack/plateau checks; the replacement-admission failure below prevents a
blanket resource-acceptance claim. The Python skips were the CMake discovery
probe (bundled CMake was used for actual board builds) and Wi-Fi setup excluded
from this host profile.

A live four-scalar scene at 1,024 pixels occupies 58,200/65,536 heap bytes;
peak use reaches 64,604. The next configuration validation fails to allocate its
decoded hardware workspace, even when the requested configuration has no
outputs. The Pico ABI sizes are 5,136 bytes for `Json` and 2,512 bytes for
`HardwareConfig`; both coexist during decode, already exceeding the 7,336 bytes
left by the live scene before other operation costs. The latter includes a
fixed array of four 624-byte digital-input configurations even with no inputs.
The old scene continues running and USB responds. Explicitly clearing
that composition frees the live heap to 6,600 bytes and permits replacement
without rebooting. The remaining larger-strip timing runs use this **explicit
release** regime; their successful admission does not hide the failed atomic
replacement. The baseline completed its equivalent replacement sequence.

Only one physical output is attached. Logical 500/1,024-pixel streams exercise
software/DMA timing on GP16; they do not establish optical performance of a full
strip that length. No APA102, multiple physical outputs, Pico 2 W, electrical
probe, full one/two-worker factorial or slice sweep is claimed. Eight layers are
tested in one composition and four total in A/B; this is not an exhaustive
maximum-admission search. VM heap peaks are measured; the native operating
reserve is a static budget, not a measured native-heap watermark. Twenty-Hz
observer overhead is measured separately from the still-unisolated internal
telemetry overhead. Cumulative foreground-gap maxima include flash/setup and
cannot establish a steady-state ingress bound.

## Reproduction and restoration

Use `benchmark_pico_campaign.py --prepare --compiler <archived-source-root>` once
per compiler and reuse its package directory unchanged across firmware trials.
The campaign, saved-preset, preparation, dirty-input and HA runners save raw
measurements and restore their temporary output configuration. They only drive
GP16. Authenticated HA tests read credentials from a private file and never copy
credentials into results. `summarize_pico_campaign.py` computes median rates,
pooled latencies and trial-resampling intervals.

The verified original 2 MiB image was restored with `picotool load -v -x` and
passed flash verification. All 13 saved objects are restored; the eight public
programs and two presets also pass their original SHA256 checks over USB. Active
and saved configurations match, MQTT reconnects, and rainbow advances 766 frames
during the final check. Original power-on, brightness 1.0, white colour and
`color_control=false` are restored. The test programs are absent.

The [plan's reopened work](pico-performance-plan.md#work-reopened-by-measured-failures)
prioritises configuration workspace/admission, preparation cadence, scheduling
and freshness, then a repeated acceptance comparison. The original firmware
remains on the device because this candidate has failed gates.
