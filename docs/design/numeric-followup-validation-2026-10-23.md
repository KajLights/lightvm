# Numeric foundation follow-up validation

23 October 2026. The implementation corrections from the review of `129ebbf`
are complete in `codex/core-numeric-foundation`. Physical release acceptance
remains open. This record supplements the historical S1/S2–S4/S5 results; it
does not carry their performance claims onto the changed runtime.

The [machine-readable record](../benchmarks/numeric-followup-validation-2026-10-23.json)
contains source identities, package hashes, every matrix replay and build
summaries. The [frozen archive](../benchmarks/numeric-followup-validation-2026-10-23.tar.gz)
contains changed sources relative to `129ebbf`, workload sources/packages,
validation logs and the full Pico stack reports. Its digest is in the JSON.

## Implemented corrections

- Map index/bound aliases fail admission before plan allocation. The compiler
  maintains distinct controls through liveness allocation. Independent wire
  mutations and native activation tests cover empty/positive ranges, yields,
  both worker counts, cleanup and retention of the running generation.
- Finite-f32 min/max and sine require application release 1.3.0, including
  reused instructions/native binding 7, declaration min/max and typed callbacks.
  Declaration min/max shares the native signed-zero evaluator. Old-target,
  forged-floor, mixed-type declaration and overflowing-intermediate tests pass.
  Recompile affected experimental packages; no compatibility mode was added.
- Nine normalized sine/circular/exponential easing helpers are ordinary guest
  equations. Exact endpoints, clamping, adjacent f32 values, monotonic samples,
  independent equations and exact scalar/kernel results are checked. All four
  frontend paths agree. Fixed inputs require `float(...)`. Trusted library math
  imports are hygienic, and early returns no longer introduce unreachable jumps
  that prevent otherwise eligible kernels.
- The operation catalogue generates C/Python metadata. Static assertions pin
  wire IDs and math enum ordering. A native probe checks every signature and
  loader operand/result/floor rejection. Metadata comparisons include effects,
  eligibility, work weights and reused EQ/min/max/sine. The independent emitter
  still uses literal wire IDs and standard-library imports, with RGB channel
  boundaries and over-budget map rejection added.
- Maps account separately for first and steady pixels. Every admitted first
  path fits 1,024 work units; later cached reads cost one. A compiler advisory
  explains ordered execution of larger bodies. Attempts, faults and discarded
  speculative suffixes count toward invocation reservations. Exact 1,024/1,025
  boundaries, costly mutually exclusive arms and near-exhaustion cases pass.
  Dispatches remain a separate counter. The watchdog does not promise identical
  cutoffs across optimizations or worker/yield schedules; its complete contract
  is in [the emitter reference](../bytecode-emitter.md#prepared-pixel-kernels).

## Software checks

| Check | Result |
| --- | --- |
| ASan/UBSan host CTest, HTTP/WebSocket/MQTT/boot enabled | 25/25 suites pass, including the new matrix gate |
| Exhaustive native metadata/signature checks after the final test expansion | All three selected suites pass |
| Broad Python SDK run with local network test servers | 359 tests, three skipped, no failures; later added work-boundary/matrix cases pass through CTest |
| Complete editor unit run | 157 passed, 11 skipped; subsequent native-enabled frontend run resolves the ten runtime skips |
| Native-enabled Python/JS/Blocks/learning view plus compiler packaging | 55/55 tests pass |
| TypeScript and production editor build | Pass; existing large-bundle advisory remains |
| Full native parameter matrix | 34 cases × 13 counts × five modes = 2,210 passing replays |
| Catalogue generation and native fixture regeneration checks | Pass |

The matrix includes lengths 3/17/64, signed offsets, every specified Q1 phase,
five-impulse Q2 raw histories, B1/B2 boundaries, all Gaussian/colour input pairs,
ordered negative controls and unchanged Color/Rainbow/Stars/Sweep sources. It
compares raw state/arrays and pixels within a build, with independent expected
pixels for G1/G2/Q1 and raw Q2 histories. RNG seed and frame times are explicit.
Native invocation tests deny heap allocation; rejection/lifecycle tests verify
cleanup and active-scene retention. This is not a measured simultaneous-scene
peak-memory campaign on hardware.

The initial broad run encountered sandbox restrictions on local listeners and
the system Python 3.9 interpreter. It was rerun with Python 3.12 and permission
for the local test servers. An old 1.1 target assertion for examples was updated
to 1.3 to reflect the semantic floor. ESP-IDF also needed local process inspection
for its build dependency checker. The successful reruns are the results above.

## Cross-build and memory disposition

| Target / configuration | Software result | Physical disposition |
| --- | --- | --- |
| Pico W / RP2040, two workers, Wi-Fi/HTTP/WebSocket/MQTT/boot | Firmware, RAM and strict stack gates pass | No current-candidate board run; retain deployment/performance gate |
| Pico 2 W / RP2350, same enabled services | Firmware, RAM and strict stack gates pass | Arithmetic, timing and network-load gates remain open |
| XIAO ESP32-S3, same enabled services, current serial executor | ESP-IDF 5.5.3 firmware and partition-size checks pass | Arithmetic, timing and network-load gates remain open |

No USB test board was present: only the Bluetooth and debug-console serial
surfaces were visible. No firmware or saved device configuration was changed.
These builds are not physical conformance results.

Pico W uses 132,784 static SRAM bytes and has **592 bytes remaining** after the
existing heap, stack, driver and operating-headroom reservations. No reservation
was enlarged. The aligned increase of 48 bytes is the longer SRAM `map_pixels`
routine (46 code bytes before alignment). Its strict accounted stack bounds,
including interrupt allowances, are 12,000/16,384 bytes on the main core and
5,088/8,192 on the worker. Both required margins pass.

Pico 2 W uses 136,076 static bytes and retains 124,020 bytes after reservations.
Its stack bounds are 12,168/16,384 main and 5,056/8,192 worker. The existing
audited SDK/project fingerprint manifest verifies unchanged; it was not
regenerated to suppress a failure. The linked callgraph and disassembly were
reanalyzed. Plan headers still occupy 48 bytes on these 32-bit targets: the
extra first-cost field uses previous padding. Lane work counters increase stack
scratch within the checked bounds.

The ESP32-S3 application image is 993,232 bytes, within its 2 MiB application
partition. No ESP32 multicore claim or physical numeric result is made.

## Remaining physical acceptance

R1–R4 are closed. R5's implementation and software accounting are closed, while
its target calibration/service gate and R6's physical campaign remain open:

1. Run the current candidate's numeric corpus on RP2040, RP2350 and ESP32-S3.
   Do not use a host or cross-build result to claim another board's arithmetic.
2. Measure at least five alternating scalar/optimized windows at 200/500/1,024
   pixels where admitted, one/two workers where supported, and mixed scenes.
   Include tiny-loop break-even and per-operation fixed/f32 comparisons.
3. Retain the costly log/exp body as an ordered control after its explicit
   work-limit exclusion. Calibrate the first/steady model against cold/warm,
   invariant-heavy and branch-heavy paths. The required 10 ms service gate and
   15% gains on two newly eligible RP2040 workloads are unchanged; controls still
   need no unexplained regression above 5%.
4. Repeat under controlled HTTP/WebSocket/MQTT ingress and capture command
   percentiles, longest job/epoch, ingress gaps, and old/candidate/transition
   peak memory. Attribute startup/activation/manual-frame intervals separately
   from steady playback. The historical 54 ms startup gap is not cleared here.

The frozen packages are accepted by `tools/benchmark_numeric.py`; use explicit
case selections for mixed scenes and preserve its state-restoration protocol.
For example, `G1-l64-o-19` and `Q1-p32768` exercise distinct newly eligible
paths. Board-appropriate wiring and firmware identities must be recorded for
the physical campaign. Avoid additional scratch/lookup/math optimizations until
profiling identifies a bottleneck under the same correctness contract.
