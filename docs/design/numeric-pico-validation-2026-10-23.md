# Pico W numeric validation and dispatch tuning

23 October 2026. The reviewed implementation was committed as `2459b5b` before
this board campaign. The subsequent tuning preserves the numeric and work-budget
contracts and adds no SRAM. **Numeric correctness and the recorded throughput
checks pass; full service/release acceptance remains open.** Concurrent brightness
controls miss the 10 ms ingress target on both preserved and final firmware.

The [structured summary](../benchmarks/numeric-pico-validation-2026-10-23.json)
contains gates, firmware identities, per-window variability and memory/service
results. The [frozen evidence](../benchmarks/numeric-pico-validation-2026-10-23.tar.gz)
contains raw samples, failed trials, workloads/packages, harnesses, build logs,
stack reports, and the final source changes relative to `2459b5b`. The JSON pins
the archive hash. Raw harness `passed` fields mean that measurement completed;
acceptance decisions below include the service failure and diagnostic controls.

## Setup and review

Pico W RP2040 `E6614103E7236D37`, 125 MHz; SDK 2.3.1; Arm GNU 13.2.Rel1
(13.2.1 20231009), Release `-g -O3 -DNDEBUG`. Wi-Fi, HTTP, WebSocket, MQTT and boot
support remain enabled. Storage stays 1 MiB, VM heap 64 KiB, two HTTP clients,
1,024-pixel capacity and the existing slice/epoch/work limits. Both one- and
two-worker firmware use the same sources. The existing 200-pixel GRB WS2812 strip
is on GP16. Test brightness limit is 32; 500/1,024 counts measure logical work and
wire transfers, not additional physical LEDs.

The board's program image was backed up before flashing. Its digest exactly
matches the preserved S2–S5 firmware; it is the matched regression baseline,
not an inferred source build. Firmware flashing included readback verification.
No network credentials enter the archive. Temporary programs/configurations are
removed; the original configuration, programs and Rainbow/light controls are
verified after the final two-worker image is restored.

The review covered admission, feature floors, liveness, work charging, helpers,
metadata and the independent emitter. All 25 ASan/UBSan host suites passed before
commit. The new dispatch path then passed the same 25 suites, an explicit native
sine budget boundary (63/64/65/66 units), 2,210 full-matrix host replays and 16
additional operation-chain scalar/bulk pairs. Generated tables check cleanly.
Pico W, Pico 2 W strict RAM/stack gates and the ESP32-S3 firmware build pass.

## Measured tuning

The initial reviewed candidate added avoidable overhead to ordinary/native
instruction dispatch. Intermediate screens and paired runs are retained,
including the abandoned sprite regressions (roughly 17%, then 7–8%). The selected
change uses a generated 256-byte flash cost table, eliminates a redundant
watchdog comparison and decodes each native call record once before entering its
call path. Native arguments no longer trigger four unrelated register reads.
Native costs still come from the catalogue; the full charge precedes execution.
No numeric precision, work weight, chunk selection or resource reserve changed.

Five alternating AB/BA firmware pairs use identical frozen packages, one-second
warmup, three-second windows, 200 logical pixels and 20 Hz USB STATUS polling.
Values below are medians of per-window render cost; raw minimum/maximum and every
sample remain in the JSON. Render CPU cost and displayed contributions are
separate from wire-limited presentation FPS.

| Control | Baseline µs | Final µs | Change |
| --- | ---: | ---: | ---: |
| map | 2,951.0 | 2,937.6 | -0.5% |
| scalar | 91,168.1 | 90,748.1 | -0.5% |
| draw | 7,615.1 | 7,974.0 | 4.7% |
| sprites | 9,548.9 | 9,874.2 | 3.4% |
| control-color | — | — | —% |
| control-rainbow | 1,623.1 | 1,663.8 | 2.5% |
| control-stars | 3,839.2 | 4,371.9 | 13.9% |
| control-sweep | 4,090.7 | 3,961.3 | -3.2% |
| mixed | 5,607.6 | 5,653.9 | 0.8% |
| mixed / scalar layer | 98,557.2 | 97,797.7 | -0.8% |

All repeatable controls remain within the specified 5% median regression limit;
draw is close to that threshold and retains its raw variability. Color reuses
its frame, so no timed-render percentage applies. Default Stars varies with its
random population: its default-window result is diagnostic and cannot establish
an unchanged-Stars regression gate. The additional population-controlled run
uses the exact same source and package with its existing settings (capacity 20,
duration 1,000 seconds, birth probability 10,000/s, fixed colour), two-second
warmup and at least 25 completed warmup frames; its outcome is recorded separately.

The five population-controlled Stars pairs measured 20,265.5 → 18,984.7 µs (-6.32%). Baseline window range: 20,221.3–20,296.7 µs; final: 18,965.1–19,000.1 µs. The unchanged source passes the 5% regression gate with this stable population. The default stochastic windows remain visible above.

## Physical correctness and throughput

- 16,209 isolated native-math corpus vectors pass, including domain rejection,
  signed zero and the errno sentinel. Maximum oracle error is zero ULP for the
  sampled operations except pow, which reaches one ULP. Arithmetic sources are
  unchanged by dispatch tuning; the probe has its own recorded firmware hash.
- 416 deterministic scalar/bulk frame pairs pass: 32 cases × 13 admitted counts
  (1/31/32/33/63/64/65/127/128/129/200/500/1,024), five explicit frame deltas per
  mode. Full parameter grids and ordered negative controls are included. This is
  pixel equivalence; host replays additionally compare raw state and damping
  histories. Default unseeded Rainbow/Stars stay out of device exact replay.
- On each worker image, independent literal-wire emission matches 20 exact frames
  and rejects 17 malformed/floor-forged packages while retaining generation, heap and frame.
  Lazy late faults, recovery and ten rejected candidates per case pass for
  scalar/bulk execution at 33 and 129 pixels.

Six workload families use five alternating scalar/bulk pairs at each count,
one-second warmup and three-second sampling windows for each worker count.
The mixed-scene observation is a separate single window, not five-pair acceptance.
The full input grids remain frozen inside their workloads. Values are median
render microseconds; the structured record retains ranges, completions, displayed
contributions, heap samples and command percentiles.

| Workload | Pixels | Scalar / 1 worker | Bulk / 1 worker | Scalar / 2 workers | Bulk / 2 workers | 2-worker reduction |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| G1-l64-o-19 | 200 | 14,871.2 | 3,148.2 | 14,874.1 | 1,729.8 | 88.4% |
| G2-l17 | 200 | 16,816.4 | 3,649.1 | 16,825.2 | 2,021.6 | 88.0% |
| Q1-p32768 | 200 | 23,095.0 | 5,489.5 | 23,105.5 | 3,111.9 | 86.5% |
| B1-lazy-piecewise | 200 | 26,282.7 | 6,674.6 | 26,289.5 | 3,815.6 | 85.5% |
| M1-all-pairs | 200 | 82,197.1 | 48,345.9 | 82,291.4 | 31,716.1 | 61.5% |
| M2-all-pairs | 200 | 72,656.9 | 40,137.2 | 72,785.6 | 30,344.8 | 58.3% |
| G1-l64-o-19 | 500 | 37,001.1 | 7,631.4 | 37,002.1 | 4,127.7 | 88.8% |
| G2-l17 | 500 | 41,781.7 | 8,764.5 | 41,778.3 | 4,784.8 | 88.5% |
| Q1-p32768 | 500 | 57,410.1 | 13,180.1 | 57,443.7 | 7,155.9 | 87.5% |
| B1-lazy-piecewise | 500 | 65,450.2 | 16,244.3 | 65,443.5 | 9,051.7 | 86.2% |
| M1-all-pairs | 500 | 205,481.9 | 121,748.1 | 205,854.1 | 79,072.4 | 61.6% |
| M2-all-pairs | 500 | 182,360.4 | 100,647.7 | 182,540.2 | 75,587.0 | 58.6% |
| G1-l64-o-19 | 1024 | 75,599.7 | 15,385.7 | 75,600.9 | 8,291.7 | 89.0% |
| G2-l17 | 1024 | 85,379.9 | 17,778.8 | 85,389.5 | 9,645.4 | 88.7% |
| Q1-p32768 | 1024 | 117,414.5 | 26,907.7 | 117,414.9 | 14,574.9 | 87.6% |
| B1-lazy-piecewise | 1024 | 133,725.1 | 32,993.0 | 133,713.1 | 18,010.0 | 86.5% |
| M1-all-pairs | 1024 | 420,758.2 | 249,945.2 | 421,488.3 | 161,527.9 | 61.7% |
| M2-all-pairs | 1024 | 373,467.2 | 205,127.5 | 373,587.7 | 154,167.7 | 58.7% |

All 180 two-worker scalar/bulk timing windows have flat sampled heap and zero
inner-window ingress-over-10-ms increments. One first-trial, one-worker G1 bulk
window has one ingress violation and a 56.6 ms maximum USB STATUS latency; its
render-job/epoch high waters did not increase (2,822/4,217 µs). No instrumentation
locates that single delay, so it remains a service-attribution exception. All raw
windows are retained.

The required 15% improvement on at least two newly eligible workloads is exceeded
in the measured cases. This does not assert that every short loop wins or that
fixed arithmetic is universally faster than f32.

## Small loops and operation costs

These are one-pair screens (0.5-second warmup, one-second windows), not repeated
acceptance estimates. Negative reductions mean bulk was slower in that screen.

| Pixels | G1 reduction / 1 worker | G1 / 2 workers | Q1 / 1 worker | Q1 / 2 workers |
| --- | ---: | ---: | ---: | ---: |
| 1 | -10.3% | -16.7% | -4.1% | -4.3% |
| 3 | 24.2% | 14.5% | 30.1% | 27.1% |
| 17 | 65.4% | 69.6% | 66.6% | 73.5% |
| 32 | 72.9% | 78.5% | 71.1% | 80.5% |
| 33 | 73.0% | 78.8% | 71.3% | 81.0% |
| 64 | 76.0% | 84.2% | 74.8% | 83.8% |

Operation-chain screens use 200 pixels and eight written arithmetic operations
per pixel. A captured clock dependency prevents frame reuse while adding zero
to the values. The base row omits the eight operations. These are whole guest
loop costs, including conversion, loop and RGB quantization overhead; they are
not isolated instruction cycle counts. Each representation keeps its own
rounding contract. Scalar/bulk frames agree within each representation.

| Chain | Q16 scalar / 1 worker | Q16 bulk / 1 worker | f32 scalar / 1 worker | f32 bulk / 1 worker | Q16 bulk / 2 workers | f32 bulk / 2 workers |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| base | 16,284.5 | 3,815.4 | 17,275.0 | 3,617.3 | 2,102.5 | 2,002.6 |
| add | 35,181.4 | 7,552.9 | 35,596.1 | 7,272.9 | 4,145.5 | 3,957.6 |
| mul | 35,800.3 | 8,394.3 | 34,964.1 | 7,104.0 | 4,577.4 | 3,863.2 |
| div | 35,879.5 | 9,139.3 | 35,600.0 | 8,684.7 | 5,156.8 | 4,861.1 |
| mod | 35,966.7 | 9,150.7 | 38,173.0 | 11,565.9 | 5,169.1 | 6,297.4 |
| sqrt | 54,244.4 | 28,790.6 | 56,249.7 | 29,283.2 | 15,923.7 | 16,098.8 |

The first/steady screens retain seven cached exp calls, one cached exp call,
mutually exclusive seven-exp arms, and the original thirty log/exp-pair body.
The last body is explicitly excluded from maps by the first-pixel work cap and
runs in ordered form. The screen cannot prove a universal timing bound for all
admitted bodies; the 1,024-unit path cap and whole-work reservation tests remain
separate safety evidence.

| Workload | Scalar / 1 worker | Bulk emission / 1 worker | Scalar / 2 workers | Bulk emission / 2 workers |
| --- | ---: | ---: | ---: | ---: |
| cold-invariants | 96,681.4 | 18,275.6 | 96,626.9 | 10,620.3 |
| warm-invariant | 23,102.6 | 1,791.3 | 23,113.0 | 1,226.8 |
| branch-cost | 114,040.7 | 83,114.2 | 111,978.4 | 48,160.2 |
| math-body | 1,040,080.0 | 1,048,757.0 | 1,050,014.0 | 1,039,382.0 |

## Service gate: still failing

Load runs retain the configured Wi-Fi/MQTT network and use USB STATUS at 20 Hz,
HTTP and WebSocket STATUS at 10 Hz each, and authenticated MQTT brightness
commands at 10 Hz. All transports complete without reported errors. The first
combined run lasts 30 seconds; the baseline and isolated runs last 20 seconds.
Counters below use first/last STATUS samples *inside* each window, excluding
boundary DESCRIPTION/transition queries. Those boundary costs remain in raw data.

| Run | Foreground gaps >10 ms | Actual ingress gaps >10 ms | USB p95 / p99 ms |
| --- | ---: | ---: | ---: |
| baseline-two | 212 | 186 | 23.0 / 29.3 |
| final-two-color-network | 190 | 190 | 18.7 / 22.0 |
| final-two-httpws | 25 | 0 | 5.8 / 6.5 |
| final-two-mqtt | 188 | 189 | 17.5 / 25.0 |
| final-two | 314 | 279 | 25.8 / 29.9 |

The idle-rendering windows in the service-load harness have no observed
inner-window service violation; the separate one-worker timing exception above
is not cleared by them. The
combined-load failure predates the numeric changes: the preserved firmware
exhibits it too. MQTT-only changes reproduce the ingress gap; HTTP/WebSocket-only
polling keeps actual ingress below 10 ms in its measured window but still exceeds
the full-loop foreground target. End-to-end HTTP/WebSocket/MQTT p95/p99 values,
including Wi-Fi/broker latency, are in the structured record; they are distinct
from device ingress counters and do not locate a stall by themselves.

Repeating combined traffic with cached Color in place of the numeric layers
still produces 190 actual-ingress and 190 foreground violations in 20 seconds.
That isolates the failure from the heavy numeric workload.

The relevant control path is `refresh_light_frame()` in `src/app.c`: a brightness
change drains/cancels the active transfer and calls synchronous `pixels_show()`.
At 500 logical WS2812 pixels a transfer takes approximately 15 ms, independently
of numeric workload cost. Convert this path to an owned asynchronous refresh,
retaining paused-frame behavior, A/B transitions, generation and buffer ownership,
then repeat the load test. Additional network/foreground phases still need
attribution. Reducing kernel work weights would not repair this control path.

The inherited near-boot approximately 54 ms gap remains separately open.
Uploads/preparation, large DESCRIPTION responses and synchronous manual FRAME
can increase lifetime maxima; none is silently included as a steady-render pass.
The failed empty-scene isolation harness hit a missing-frame-counter assumption;
that failed attempt is retained and is not a service result.

## Memory and stack

A fresh selected-firmware boot was used for the peak-memory sequence. Cancellation
and rejection retain the exact live generation/current heap. Twenty steady
A/B/transition samples are flat; host native invocation checks additionally deny
allocation during render. Flat device samples alone cannot prove that no transient
allocation occurred.

| Point | Current VM heap | Peak VM heap |
| --- | ---: | ---: |
| old A live | 27,212 | 28,180 |
| old A after ten cancelled candidates | 27,212 | 38,672 |
| new A after atomic replacement | 27,732 | 39,608 |
| A plus B live | 53,104 | 60,216 |
| A plus B plus program transition | 63,474 | 64,442 |
| transition after ten rejected candidates | 63,474 | 64,604 |

Ten in-progress candidate cancellations and ten oversized candidate rejections
pass. At 63,474 live bytes the scene fits, but a replacement configuration parser
does not. Initial test cleanup therefore failed; the next harnesses also failed
admission until space was released. All failures are retained. Clearing B recovers
to 45,942 bytes and permits configuration restoration, without reboot or a larger
heap. This is a measured admission/headroom limit, not a leak. Future tests must
clear scenes through the lightweight control path before allocating a parser at
near-full occupancy.

Static Pico W SRAM remains 132,784 bytes, leaving **592 bytes** after the existing
65,536-byte VM heap, 24,576-byte native stacks, 30,464-byte driver budget and
16,384-byte operating headroom. The reviewed implementation had already added
48 static bytes relative to the preserved firmware; tuning adds none. The strict
whole-path bounds including interrupt allowance are 12,000/16,384 main and
5,064/8,192 worker, retaining both required margins. Hardware high-water samples
remain observations, not proof that every interrupt/callback path was exercised.

Observed campaign high waters: render_max_job_us=6,113, render_epoch_max_us=7,302, main_stack_peak_bytes=5,980, worker_stack_peak_bytes=2,496. These are lifetime maxima, not per-window maxima.


Pico 2 W also passes the unchanged strict accounting/fingerprint gate (136,076
static bytes; 124,020 remaining after reservations). ESP32-S3 builds and fits its
2 MiB application partition. Neither has a physical conformance/performance
result in this campaign.

## Disposition

The final source and recorded Pico W measurements support this dispatch tuning
and the numeric throughput gains. R1–R4 remain closed. R5's accounting and measured
numeric paths are supported; R5/R6 overall service acceptance is open. Remaining
work is asynchronous brightness refresh and foreground/startup attribution,
repeated service checks after that repair, and physical RP2350/ESP32 validation.
The larger source-language compiler remains outside this core change.
