# Numeric and kernel workload specification

22 October 2026. **S0 specification; benchmark implementations and board results
are pending.** Follow the [numeric contract](../numeric-types.md) and
[delivery plan](core-numeric-kernels-plan.md). The standalone host suite measures
correctness only.

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
