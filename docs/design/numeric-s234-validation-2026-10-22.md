# S2–S4 numeric kernels and native math validation

22 October 2026. The implementation follows S1 commit `b5c8537` on
`codex/core-numeric-foundation`, based on validated Pico repair `a9c8611`.
It implements the [numeric contract](../numeric-types.md) and
[kernel encoding](../bytecode-emitter.md#prepared-pixel-kernels) with independent
fixtures motivated by the local Pattern, Colors, Candle, Flame and colour-helper
requirements. The repair archive and its firmware remain unchanged.

This record covers S2–S4 and the attached RP2040 checks. S5 full fixed authoring
and independent-emitter parity, and S6's complete multi-board release matrix,
remain separate work. No RP2350 or ESP32 physical performance result is claimed.

## Implementation

- Stable resident numeric-array gathers and exact fixed/bitwise expressions are
  eligible for maps. The loader checks constant resource identity, kind, flags,
  nonaliasing, permissions and generations independently of compiler recognition.
- Forward conditional maps have one common final store and definite assignment
  on every path. Their maximum body is 128 instructions. Dead-arm loads/math
  faults stay dormant; persistent destinations retain ordered writes before a
  fault. Final locals merge correctly across conditional writes, lanes and yields.
- Immutable plans are prepared incrementally, charged to admitted heap and freed
  on cancellation/destruction. Workers use bounded private scratch. Weighted
  chunks retain the repair's 128/64/32 pixel maxima and check time between pixels.
  Straight-line paths use the verifier's complete write set; only conditional
  paths track individual writes. Rendering performs no allocation.
- All required f32 functions share one evaluator between scalar and bulk paths.
  Exact bit operations implement integral rounding/min/max; integer square root
  is correctly rounded. Transcendentals use binary64 libm and explicit f32
  narrowing. Coarse exp/pow range guards avoid libm range-error writes to shared
  bare-metal `errno`. No guest mode, effect-specific native or compatibility
  interpreter was added.

The existing sine binding and FMIN/FMAX now use the published finite-f32 contract,
including zero signs. In particular, min/max reject a nonfinite operand instead
of masking it with the other operand. This is the current experimental contract;
there is no legacy switch. Ordinary arithmetic order and fixed semantics are
unchanged. New instructions 62..71 and broader maps require 1.3.0; scalar fixed
operations retain 1.2.0 and simple existing maps retain 1.0.0.

## Host and build checks

Host: arm64 macOS, Apple Clang 21, Python 3.12.13. Host services enabled HTTP,
WebSocket, MQTT and boot sequences, with Wi-Fi disabled. Network-listener tests
ran with the required local socket access.

| Check | Result |
| --- | --- |
| Native CTest | 19/19 pass |
| ASan/UBSan CTest | 19/19 pass; standalone arithmetic also instrumented |
| Full Python/compiler/receiver discovery | 344 tests run, two environment skips, no failures |
| Additional reviewed fixed/bitwise map vectors | Bulk suite 10/10 pass, normal and ASan/UBSan |
| Frontends with final native receiver | 152 pass, one audio-device environment skip |
| TypeScript and Vite production build | Pass |
| Pico W, Pico 2 W and minimal Pico | Strict flash, SRAM and whole-path stack gates pass |
| ESP32-S3, ESP-IDF 5.5.3 | Shared core and full network-service firmware build pass |

The Python skips are the existing host Wi-Fi/power-recovery classes. The full
discovery preceded the additional map-vector test, which was then run separately
with its entire ten-test suite in both configurations. No LeakSanitizer result is
claimed: ASan uses `detect_leaks=0`; explicit VM allocation accounting verifies
zero retained heap after teardown.

Tests compare raw fixed/f32 state, resident arrays, persistent images, output
bytes and faults with optimization on/off. They cover all resident numeric array
kinds, reviewed exact numeric vectors, slice-boundary counts, two lanes, forced
time yields, conditional last writes, empty/negative loops, lazy faults and
malformed forward CFGs including a valid target that skips a required definition.
Exact 64/128-instruction bodies pass; a 129-instruction body stays scalar.
Native lifecycle tests fail each allocation and cancel at each preparation return,
including partially decoded plans, and deny all allocations during invocation.

The only audited build-file fingerprint update is the reviewed, optional
`LIGHTVM_NUMERIC_PROBE` target. It links the same numeric core and compiler double
configuration without changing application callbacks or stack reservations.
No SDK fingerprint was regenerated. Whole-path totals include IRQ allowances:

| Build | Static SRAM | Remaining after reservations | Main / reservation | Worker / reservation |
| --- | ---: | ---: | ---: | ---: |
| Pico W, one or two workers | 132,736 B | 640 B | 12,000 / 16,384 B | 5,064 / 8,192 B |
| Pico 2 W | 136,076 B | 124,020 B | 12,168 / 16,384 B | 5,016 / 8,192 B |
| Minimal Pico | 37,336 B | 26,152 B | 11,404 / 16,384 B | 5,052 / 8,192 B |

Pico W retains the 64 KiB VM heap, 16 KiB operating reserve and 16/8 KiB stack
reservations. These are software path bounds, not an optical or universal physical
stack certificate. ESP32-S3 firmware is 992,624 bytes in its 2 MiB application
partition; physical ESP32 numeric conformance remains unmeasured.

The final Arm ELF reports a 48-byte plan header plus eight bytes per instruction
(1,072 bytes at the 128-instruction limit). Each of the two stack-local lane tasks
is 576 bytes, including its 512-byte value array; each executing lane also has
512 bytes of invariant scratch. GCC's complete local frames are 1,288 bytes for
`pbc_map_run`, 584 for `map_pixels` and 2,760 for admission-time CFG analysis.
The whole-path table above includes callers and math callees, rather than adding
these local frames in isolation.

## Native math accuracy

The frozen corpus SHA256 is
`1a014790e7794e10313aef06f87aff83fb7b0c3e364bfbb04579954ebb03a9b0`.
Its 16,209 vectors cover all f32 exponent bands, both signs, subnormals, domain
boundaries, range-reduction extremes, deterministic random pairs and the 43
reviewed identities/faults. Expected values use independent rational/Decimal
calculations at 90 and 160 digits, which must round identically. Trigonometry uses
independently calculated pi and series; no host libm supplies expected results.

Both the host and the isolated RP2040 probe pass every case and preserve an
`errno` sentinel, including on faults. Abs, rounding, sin/cos, sqrt, log/exp and
min/max match the rounded reference exactly in this corpus. Pow's maximum error
is one f32 ULP, absolute error `2^-149`. Zero signs and domain decisions pass
exactly. These measured results are within the frozen 0/4/8-ULP targets and are
not a universal proof for every input or another target's libm.

## Attached Pico campaign

Device: Pico W, RP2040 B2, serial `E6614103E7236D37`, 125 MHz, Pico SDK 2.3.1,
Arm GCC 13.2.rel1. The existing WS2812 output is GP16 with 200 physical pixels.
Larger logical counts test execution/admission; they are not optical validation.
Network configuration stays in place. Temporary numeric outputs cap brightness
at 32, and every harness restores configuration and removes its temporary programs.

Firmware identities:

- Repair baseline: `ace68ff719e5f098812b941c0e47123142b25e72303b9ac744aa6deb25e52c4f`.
- Final two-worker candidate: `f87a2e51ecd31f8cbaf2fa48667c32ae94faddb7b5c1f311e945abc5434bee6e`.
- Final one-worker candidate: `78821310022a7496ae9ee2054cd17e442cafc59b3fe1cdff39db53e452f2df6d`.
- Final math probe: `c31f2507a58aaf40bf97cc177ce870166909e59dfd2bc77827e8b67f47da1f2a`.

The first one-worker attempt used the build's default 512 KiB
storage layout and rejected uploads because the existing 1 MiB store was not
available. That attempt is excluded. The corrected build uses the same 1 MiB
layout as the baseline and two-worker candidate. No new firmware backup was made.

### Matched controls

Five alternating baseline/candidate pairs use five-second windows after a fixed
two-second workload warmup, with USB STATUS polling at 20 Hz. Rainbow uses the
unchanged saved preset. Other controls use packages frozen with the baseline
compiler; source/package hashes are recorded. The fixed-population sprite case
is the repeatable regression control for the sprite path. A preliminary unchanged
Stars screen is retained separately because its live random population varies.

| Control / layer | Baseline median render µs | Candidate median render µs | Change |
| --- | ---: | ---: | ---: |
| Simple map | 2,943.57 | 2,992.15 | +1.65% |
| Fixed 12 sprites | 10,148.74 | 9,505.44 | −6.34% |
| Mixed scene, map | 5,655.84 | 5,607.37 | −0.86% |
| Mixed scene, scalar | 101,615.08 | 98,216.03 | −3.35% |
| Saved Rainbow | 1,639.23 | 1,590.85 | −2.95% |

The initial candidate's simple-map cost rose about 5.1%. Avoiding unnecessary
per-instruction write tracking on straight-line paths reduced that overhead;
the table reports the final firmware. None of these controls has an unexplained
median CPU regression above 5%. Completion and displayed-contribution counters
remain separate from render cost and physical presentation rate.

### Numeric workloads

The machine-readable results record scalar/bulk frame hashes, plan bytes,
admission/steady heap, render cost, completed/displayed contributions, status RTTs
and service counters. Primary gather/fixed-branch timings use five alternating
scalar/bulk pairs per worker count. Additional math, mixed-scene and 500/1,024-pixel
screens are single pairs and do not establish statistical parity across the full
S6 matrix. The costly math-body screen specifically checks the cooperative bound
when a weighted chunk can only contain one pixel.

All eight fixtures pass 104 scalar/bulk replay pairs per worker configuration:
13 pixel counts from 1 through 1,024, with five explicit frame deltas per pair.
The host additionally compares raw values and persistent partial writes. Hardware
fault tests retain the last published frame, recover after reset, and reject ten
oversized candidates in each of four cases per configuration without changing
the active generation or steady heap. Forged feature floors are rejected.

Primary results at 200 pixels, five six-second windows per variant. Ranges are
the minimum and maximum window means; the JSON also records standard deviation.

| Workers | Workload | Scalar median µs (range) | Bulk median µs (range) | Reduction |
| ---: | --- | ---: | ---: | ---: |
| 1 | Repeat gather | 15,866.19 (15,862.62–15,867.81) | 3,183.53 (3,182.40–3,186.90) | 79.94% |
| 2 | Repeat gather | 15,867.02 (15,863.85–15,873.01) | 1,748.50 (1,747.96–1,750.53) | 88.98% |
| 1 | Fixed branch | 29,461.01 (29,447.67–29,467.10) | 6,288.35 (6,284.56–6,289.06) | 78.66% |
| 2 | Fixed branch | 29,461.87 (29,454.58–29,470.65) | 3,608.29 (3,607.00–3,612.45) | 87.75% |

The two-worker gather completes about 149 contributions/s versus 49 scalar;
the fixed branch completes 145/s versus 27. Distinct displayed contributions
track those rates in these scenes. These are separate counters from physical
output presentations; the already fast gather approaches the strip's output
ceiling. Render cost is the existing instrumented `render_total_us` per completed
contribution, not a sum of both cores' instruction cycles.

The repeat gather's plan is 144 bytes and adds 160 admitted heap bytes including
allocator overhead. The fixed branch's 280-byte plan adds 296 bytes. Steady heap
is respectively 10,314→10,474 and 10,300→10,596 bytes at 200 pixels. Across all
numeric timing windows heap stays constant; the cumulative peak after large
replays and candidate replacement is 40,128 bytes, within the unchanged 65,536-byte
limit. Controls peak at 20,783 bytes in the candidate versus 20,639 in the baseline.
Failed-candidate and cancellation accounting is also exercised directly by the
native allocation-failure tests.

Single-pair screens at 200 pixels show these scalar→bulk costs in µs:

| Workload | One worker | Two workers |
| --- | ---: | ---: |
| Gaussian math | 82,210.62→51,196.84 | 82,222.87→34,285.16 |
| Colour power | 89,435.15→52,580.01 | 89,416.07→29,657.20 |
| Fixed damping with independent shading | 13,225.56→2,689.35 | 13,230.39→1,557.29 |
| Fitted gather | 21,546.07→3,904.04 | 21,550.46→2,263.40 |

The two-worker 500/1,024-pixel gather, fixed-branch and Gaussian screens also
improve. For example, Gaussian at 1,024 pixels falls from 422,046 to 175,210 µs.
The 33-pixel costly-body screen, containing 30 log/exp pairs per pixel, falls from
261,692 to 144,551 µs on one worker and 261,693 to 81,132 µs on two. These extra
screens are useful stress evidence, not five-pair statistical acceptance studies.

### Service limits and investigation

Pooled USB STATUS round-trip measurements retain every sample:

| Regime | Windows | p95 ms | p99 ms | Maximum ms | Foreground / ingress gaps above 10 ms during polling |
| --- | ---: | ---: | ---: | ---: | ---: |
| Numeric, one worker | 31 | 5.44 | 6.81 | 22.00 | 3 / 0 |
| Numeric, two workers | 43 | 5.18 | 6.44 | 8.11 | 1 / 0 |
| Matched baseline controls | 20 | 6.00 | 6.97 | 31.44 | 9 / 5 |
| Matched candidate controls | 20 | 5.12 | 5.67 | 38.82 | 5 / 5 |
| Settled candidate Rainbow, brightness 0.8 | 5 | 4.79 | 5.36 | 6.64 | 1 / 0 |

Each near-boot Rainbow control window records one roughly 54 ms ingress gap,
on both baseline and candidate. The five settled windows have none. This isolates
an inherited startup/background condition from steady numeric rendering, but
does not identify its exact cause. Network configuration remained intact and
the final device reports station Wi-Fi and MQTT connected. Full network-load
acceptance and that startup behavior remain part of S6.

The one-worker 22.00 ms host RTT outlier occurs in scalar damping without a
foreground or ingress counter increment around that sample. The occasional
foreground-loop increments also occur on settled legacy Rainbow. Foreground and
ingress counters cover different service intervals; they must not be presented
as interchangeable, or as proof of a universal response bound.

The largest recorded numeric render job is 4,983 µs with one worker and 5,559 µs
with two; epoch maxima are 6,119 and 6,046 µs. The costly-body fixture exercises
the required one-pixel minimum: it is impossible to interrupt midway through
that pixel with this plan representation. Thus the 2 ms slice and 4 ms epoch are
cooperative targets, not hard bounds. The weighted plan limits the next chunk
to one pixel per lane, retains progress, and the measured ingress intervals stay
below 10 ms during these polling windows. Stronger per-pixel preemption remains
outside this implementation.

Full DESCRIPTION queries before/after windows add boundary gaps, reported
separately in the JSON. Cumulative foreground/ingress maxima reach about 7.6 s
during synchronous manual FRAME replay of the 1,024-pixel costly body. Uploads,
storage/admission and startup also contribute to cumulative counters. Those
maxima are retained in raw evidence and are not steady-scheduler latency claims.

Measured main-stack high water is 6,040 bytes in the one-worker numeric campaign
and 5,992 with two; the two-worker worker-stack high water is 2,252 bytes. These
observations complement the larger conservative whole-path bounds above.

## Evidence and final device state

- [Machine-readable summary](../benchmarks/numeric-s234-validation-2026-10-22.json):
  complete window statistics, exact build/source identities, memory and service
  accounting, mathematical error results and restoration checks.
- [Raw evidence archive](../benchmarks/numeric-s234-validation-2026-10-22-raw.tar.gz):
  hardware reports, individual RTT/heap/counter samples, frame hashes, frozen
  packages/sources, host reports, test/build logs and stack reports. Preliminary
  controls are explicitly separated from the final campaign.
- [SHA256 manifest](../benchmarks/numeric-s234-validation-2026-10-22-manifest.json):
  archive, summary and each archive member's digest and size.

The final two-worker candidate is running on the attached Pico. All eight
original program hashes, both saved presets and active/saved configuration match
the initial records. No temporary test programs remain. Rainbow is on at
brightness 0.8. No firmware backup or saved-network change was made.

S2–S4's implementation, exactness, lifecycle, build and attached-Pico checks are
complete. S5 frontend/emitter parity and the explicitly scoped S6 matrix remain
open; neither physical RP2350/ESP32 conformance nor a hard real-time guarantee is
implied by this record.
