# Numeric and kernel workload specification

23 October 2026. **Host and RP2040 numeric checks pass; control-service and
other-board acceptance remain open.** Follow the
[numeric contract](../numeric-types.md),
[delivery plan](core-numeric-kernels-plan.md) and
[current Pico W validation](numeric-pico-validation-2026-10-23.md). The
[earlier measurements](numeric-s234-validation-2026-10-22.md) retain their
attached-RP2040 scope. The complete S6 physical matrix remains pending.

## Source grounding

Reference picolights commit `f6480b4845b2ba3af4fbf0ffb1f398d362dda772`, using paths
relative to that repository: `picolights/effects/animations/pattern.py`,
`colors.py`, `candle.py`, `flame.py`, `starlight.py`, `twinkle.py` and
`picolights/colors/_colors.py`. These path names remain meaningful in a managed
worktree where picolights is not an adjacent directory. The original plan links
the sibling checkout for ordinary repository layouts.

Pattern demonstrates fitted/repeating lookup; Colors demonstrates prepared
artwork. Candle demonstrates ordered damping and logarithmic perturbation;
Flame's Gaussian transform needs log, sqrt and trigonometry. Colour helpers use
powers, signed wrapping and piecewise selection. Starlight/Twinkle demonstrate
feedback, RNG and sparse writes that cannot silently become independent maps.
LightVM's existing motion, easing and Sweep establish that effect equations
belong in source. Create original small fixtures for these requirements; do not
copy the older object model, RNG schedule or attributed easing implementation.

## Frozen workload definitions

Use output counts `N = 200, 500, 1024`, plus correctness-only counts
`0, 1, 31, 32, 33, 63, 64, 65, 127, 128, 129`. Invalid zero-size resources test
admission failure where applicable; zero-trip loops use an admitted resource.
Array counts are separate from output counts. Timed comparisons use captured
timestamps and identical inputs, never uncontrolled host wall time.

| ID | Definition | Expected eligibility and comparison |
| --- | --- | --- |
| G1 repeat | Prepared RGB8 integer table, lengths 3, 17, 64; destination pixel `i` reads `(i + offset) mod length`, with offsets -19, 0, 23 | Stable numeric gather after S2; compare scalar/bulk pixels exactly |
| G2 fit | Same table; index `floor(i * length / N)` computed in i32 for positive N, then gather | Stable gather; record preparation separately from rendering |
| Q1 ramp | Q16.16 phase captured once; `p = (phase + fixed(i) / fixed(N)) mod fixed(1)`; piecewise triangular envelope, explicit RGB quantization | Scalar initially, bulk after S3; raw fixed results and pixels identical |
| Q2 damping | Per frame `v = (v + impulse) * damping`, `x = clamp(x + v * gain, lo, hi)` with explicit intermediate fixed operations; replay prepared impulses | Ordered persistent state; expose equivalent pure pixel shading separately |
| B1 piecewise | One final store; clipped colour bands and easing selected by nested forward conditionals | S3 acyclic branches; include dead branches with bad index/zero division |
| B2 long body | Pure sequential integer/fixed expression bodies of 33, 64 and 128 verified instructions including final store; prevent constant folding from removing varying work | S3 admission boundary; a 129-instruction body stays scalar or is rejected if explicitly encoded as a kernel |
| M1 Gaussian | `sqrt(-2 * log(u1)) * cos(2*pi*u2)` and sine counterpart from prepared binary32 input pairs | Pure native math after S4; RNG generation excluded from the kernel |
| M2 colour | Signed trigonometric components raised to positive powers, with piecewise channel selection as in the local colour requirements | S4 math plus S3 branches; define exponent and angle inputs explicitly |
| O1 ordered | Feedback reads followed by writes, stateful RNG, sparse skip-writes, source/destination alias and scatter destinations | Negative fixtures: retain scalar order; no accidental map or reuse promotion |

For Q1 use raw phases 0, 1, 32768, 65535 and 65536. Define the triangular
envelope as `2*p` for `p < 0.5`, otherwise `2*(1-p)`; assign red using the
explicit fixed-to-i32 truncation of `255*envelope`. Intermediates remain in range.
Use raw damping 58982, gain 22938, initial `x=52429`, `v=0`, bounds 36045..65536
and a repeating signed-raw impulse table `[0, 1311, -1311, 655, -655]` for Q2.
The reference evaluates the written operation order, including each rounding.
Compare f32 variants separately; cross-type visual similarity is not exactness.

For M1, use all pairs from `u1 = {1/65536, 0.125, 0.5, 1-1/65536}` and
`u2 = {0, 0.125, 0.5, 0.875}` with `pi` encoded as binary32 `0x40490fdb`.
For M2 use exponent inputs `{0.5, 1, 2, 4}` and phases `{0, 0.125, 0.25, 0.5,
0.75, 0.875}` multiplied by the same explicitly rounded `2*pi`. Use the S4
high-precision oracle to establish numeric envelopes; compare scalar and bulk
bit-for-bit within one build. Domain-fault fixtures are separate from timed runs.

Existing Color, Rainbow, Stars and Sweep are unchanged controls. Freeze their
source and compiled bytes at the integration baseline. Do not redesign Stars
while claiming performance against its unchanged behaviour. The separate
procedural Stars/noise proposal, event delivery and new compiler frontend are
outside this foundation milestone.

## Measurement and acceptance

Compare optimized and forced-scalar emission of the same typed program and
inputs. Use one/two workers and a mixed scene, recording source/package/firmware
hashes, compiler flags, board/clock, resource counts and random seeds. Include
array construction, admission and prepared-plan memory separately; no rendering
allocation is allowed. Hardware performance begins after the Pico repair is
committed and its baseline is preserved.

Use at least five alternating baseline/candidate windows with a fixed warmup.
Publish raw samples and variability. Apply the plan's unchanged gates: at least
15% lower median render CPU cost for two newly eligible RP2040 workloads, no
unexplained regression above 5% on controls, no heap growth or leaked candidate
storage, and passing existing stack/service budgets. Report fixed arithmetic by
operation and target; it has no assumed universal speed advantage over f32.

Measure contributions displayed, render CPU, p95/p99 command latency, longest
chunk/epoch and ingress gaps separately from wire-limited FPS. A local host loop
cannot close any board gate. RP2350 and ESP32 claims require their own evidence.

## Integration handoff

The first executable fixtures live in [tests/fixtures/numeric](../../tests/fixtures/numeric):
repeating/fitted three-colour gathers, a fixed triangular branch ramp, prepared
Gaussian input pairs, trigonometric colour powers and ordered fixed damping with
independent shading. These are original small workload expressions. Their timed
versions capture bounded elapsed-time phase/offset to prevent static frame reuse.
The damping fixture alternates an impulse for repeatable frame-by-frame comparison;
it is not the full five-impulse Q2 study. The 23 October matrix below supplies
the full parameter grids and five-impulse study; timing claims retain their
measured case and board scope.

`tools/benchmark_numeric.py --prepare` freezes source/package hashes and both
optimized and forced-scalar packages. Its replay mode compares five explicit
frame deltas; its timing mode alternates scalar/bulk order and records raw
render, presentation, status-latency, heap and service counters. Configuration is
restored and temporary programs are removed. `tools/check_numeric_runtime_hardware.py`
checks late faults, retained frames, recovery and repeated rejected admission.
Neither tool changes saved configuration or makes firmware backups.

The independent host tests additionally exercise every resident numeric array
kind, exact 64/128-instruction map bodies and the 129-instruction scalar boundary,
forced yields and conditional last writes across two lanes. Those raw-state
checks complement the application-level hardware snapshots.

The numeric foundation is rebased onto validated Pico repair `a9c8611` before
loader/interpreter, map executor, scheduler, preparation or board build changes.
Preserve its constant-value cache, float checks, SRAM placement, revised chunk
choices and ownership rules.
Do not mechanically restore the planning baseline's 128/32 chunk assumption:
the committed repair uses 64-pixel unsplit transient chunks.

Link the shared numeric module only after types, encodings and feature floors
are assigned. Measure the module's compiler helper calls and code placement;
putting additional functions in SRAM consumes the board's static reservation
margin. Plans and scratch consume admitted heap or audited stacks. Update
whole-path stack contracts from the resulting call graph, not by copying an old
hash manifest. The first merged hardware run must establish a fresh integrated
baseline; standalone test results do not carry forward a performance claim.

## 23 October follow-up matrix

`tools/numeric_matrix.py --prepare --packages DIRECTORY` now freezes 34 original
cases, both emission modes and their source/package hashes. It covers G1 lengths
3/17/64 with all signed offsets; G2 at all lengths; every Q1 raw phase; all five
Q2 impulses with recorded raw position/velocity histories; lazy B1; exact B2
33/64/128/129-instruction boundaries; all M1/M2 parameter pairs; ordered state,
RNG, sparse, scatter and source-mutation controls; and unchanged example sources
for Color/Rainbow/Stars/Sweep. G1/G2 construct their prepared tables with an init
loop, avoiding a register for every literal index/colour in a 64-entry initializer.

`--driver PATH --packages DIRECTORY --result FILE` replays the full count grid
through the native test port with one/two workers and forced yields. All 2,210
replays pass. G1/G2 and Q1 also check independent arithmetic pixel expectations;
Q2 checks every raw history value. The native driver fixes default RNG seed to
24301 and advances explicit 0.25-second frame inputs from 1.25 seconds. This is
host correctness evidence, not a physical timing result. A shorter 510-replay
version is part of CTest.

The package manifest works with `tools/benchmark_numeric.py`; select explicit
cases for mixed-scene timing. Its preparation mode retains over-budget bodies
as ordered controls and records the compiler reason. In particular, the prior
30 log/exp-pair fixture exceeds the new first-pixel cap and is no longer a map.
Do not omit it or compare it as a successful bulk specialization.

See the [current Pico W validation](numeric-pico-validation-2026-10-23.md).
The current Pico W campaign records five alternating windows at 200/500/1,024
pixels, small-loop and fixed/f32 operation screens, mixed scenes, network load
and simultaneous old/candidate/transition peaks. Its service gate fails and
remains open; RP2350 and ESP32 still require physical validation.

## Current physical disposition

The [23 October Pico W campaign](numeric-pico-validation-2026-10-23.md) adds
416 scalar/bulk frame comparisons (32 deterministic cases × 13 admitted counts),
independent wire/floor rejections, native-math vectors and fault/retention checks.
Unchanged Rainbow and Stars remain timing controls; their default unseeded RNG
prevents a frame-for-frame device comparison through the current public reset
API. Host replays include explicit seeds and still cover both examples. A
separate unchanged Stars comparison uses its existing settings to hold a stable
20-sprite population and passes the repeated regression gate.

Repeated render timing, small-loop screens, fixed/f32 operation chains and
first/steady-cost controls are reported separately. Counts above the attached
200-pixel strip are logical execution/admission tests. Screened operation chains
include common loop/quantization overhead and are not isolated cycle counts.

Concurrent brightness controls expose an existing synchronous output-refresh
path that misses the 10 ms service target. The original and tuned firmware both
fail this gate; HTTP/WebSocket polling alone has no observed ingress violation
in its isolated window but still has foreground violations. Peak-memory tests
also require clearing a scene before parsing a replacement at nearly full heap.
These findings and RP2350/ESP32 physical results retain their explicit open
statuses. Numeric throughput gains do not close unrelated control-service gates.
