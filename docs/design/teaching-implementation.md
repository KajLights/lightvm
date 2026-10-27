# Teaching implementation and acceptance

27 October 2026. Implementation of the [language and bytecode review](teaching-language-review-2026-10-27.md).
The software fixes and teaching material are implemented. Physical-device
performance qualification and observation with real beginners remain external
acceptance gates. No test controller was flashed or reconfigured.

Start with the [course guide](../teaching.md) and
[twelve lessons](../../examples/lessons/README.md). Learner text uses everyday
language; technical execution details stay in optional performance notes
and engineering references.

## Review findings addressed

| Finding | Implemented change | Main evidence |
| --- | --- | --- |
| F1: Python loops | Private range cursor; target assignment cannot change iteration. Empty loops preserve existing values; nonempty loops retain the last source value. Signed steps, nested targets, break/continue and i32 termination are checked. | Independent CPython trace tests and native scalar/map replays |
| F2: logical expressions | Lazy, single-evaluation, operand-valued logic for compatible scalars. State arguments are copied before a later argument can change them. Unsupported mixed result types give diagnostics. | Source-semantic, frontend and numeric tests; loader admission for every successful course package |
| F3: ordinary JavaScript loops | Bounded number loops and immutable count aliases reach the shared optimiser without int(0). Counter expressions retain f32 meaning. Mutable or unproved bounds stay ordered. | Numeric frontend tests, cross-language lesson output and map advice |
| F4: equivalent conditions | Exclusive same-index branch stores merge into one final store, preserving lazy evaluation. Skipped writes, feedback, state changes and RNG remain ordered where independence is not proved. | Branch/helper lessons, fault tests and existing map/numeric suites |
| F5: everyday decomposition | Procedures, bare return, same-type branch joins, managed-array iteration, chained comparisons, constant defaults/named arguments, module scalar/colour constants, tuple swaps, power and unary plus. | Fourteen independent source-semantic tests with both compiler modes, plus full integration suite |
| F6: useful feedback | Source/block maps beside the package CRC; VM failing PC/op/reason; actual bad position/length for indexed accesses. Local strip/ring preview, frame/statement stepping, bounded scalar watches and stop/reset. Device faults link only to matching bytecode. | Real C VM compiled to Wasm, source/block tests, saved-file browser test and device-state protocol test |
| F7: consistent teaching material | Twelve lessons × four versions × three languages; corrected API docs; exponential trail fade; positive wrapping helpers; locally packaged Python/editor assets. | Generated-source check, lesson replay, browser startup with remote requests blocked |

## Completed surface and runtime work

- [x] Blocks calculations and drawing procedures with up to three named inputs.
- [x] Range and array loops, bounded seeded random choices and ring wrapping.
- [x] Boolean/number/colour socket checks, saved variable/function name choices,
  setting bounds and clear clip/wrap drawing options.
- [x] Generic managed-number array fill/copy conveniences.
- [x] Beginner toolbox, advanced-tool toggle and lesson selector.
- [x] Preview settings/manual inputs survive local Save and Import.
- [x] Source locations survive optimisation, helper expansion and map-lane faults.
- [x] Native fault metadata stays small; complete source text remains in the editor.
- [x] Offline compiler, editor and lesson assets; reproducible Wasm build in CI.
- [x] Updated fixtures and work accounting for correct loop exit values.
- [x] Refreshed Pico callback accounting and corrected 12-byte prepared-instruction
  memory estimates. Removed stale fixed scratch-size advice.

No new bytecode opcode, call stack, dynamic guest allocator or compatibility
shim was added. Recompile existing source to obtain corrected loop semantics.
The container and persisted storage formats did not change, so this work needs
no storage migration or wipe. Runtime source feedback needs updated firmware;
older firmware cannot report the new fault fields.

## Executed acceptance

The [machine-readable evidence](../benchmarks/teaching-implementation-2026-10-27.json)
records source hashes, package sizes, plan sizes, count-by-count outcomes, replay
totals, host measurements and firmware build/stack results. The historical
review JSON is retained separately; it still describes the original bugs.

| Check | Result |
| --- | --- |
| Published course | 144 sources, 14 counts, both compiler modes; 3,780 native executions, no unexpected failures |
| Browser C VM | 3,078 runs and 34,338 frames; exact matched-language/mode output, 36 expected deliberate runtime faults, zero retained heap after close |
| Time and boundaries | Counts 1, 2, 12, 16, 24, 25, 31, 32, 33, 50, 63, 64, 65, 100; deltas 0/.016/.033/.25/1/.7; clock 1,000,000.25 seconds; progress 0/1; full search arrays |
| Semantics and stepping | 20 teaching tests, including 14 independent source-semantic tests, frame atomicity, source faults, watches and repeatable restart |
| Native contract tests | 28 USB-profile groups and 34 network-profile groups passed |
| Python integration | 402 tests, 7 configuration/tool skips, all remaining tests passed; MQTT 20, keypad 12, boot-sequence 5 and Pico-build 24 tests passed separately with their required profiles/tools |
| Editor | 180 tests passed, one existing native-audio test skipped; TypeScript and production build passed |
| Browser workflow | Remote requests blocked; compiler startup, ring, stepping, fault location, lesson loading, settings/input save/import, Blocks and narrow screen checked |
| Firmware builds | 100-pixel Pico USB and Pico W with Wi-Fi/HTTP/WebSocket/MQTT/boot recovery built; both stack software gates passed |

The course mistakes are also checked: wrong-type and missing-definition programs
must fail compilation; endpoint/endless-loop mistakes must fault; swapped bands,
frame-dependent movement and missing fading must produce visibly different
results. A program that merely compiles is not counted as successful teaching
acceptance.

The first broad test attempt encountered the sandbox's local-socket restriction.
A combined all-features simulator also activated recovery listeners during tests
designed for the separate USB profile. The passing integration run uses a USB
receiver for core tests and an explicit ephemeral-port receiver for network and
recovery tests; MQTT/keypad use their own service profile. An incorrect expected
source line in the new protocol test was corrected and that test passed again.
The skipped boot/MQTT/keypad/CMake cases were subsequently exercised in the
separate runs listed above. The browser had one cold-start timeout while the
larger suites were running; the subsequent offline run passed, including the
non-ASCII source-column check.

## What the performance measurements say

The same host executable ran three alternating 100-frame windows per variant at
each count. Each replay starts from a reset clock. Values below are median window
means in microseconds of engine render CPU per frame, not physical FPS.

| Workload | 25 pixels: ordered / bulk | 50 pixels: ordered / bulk | 100 pixels: ordered / bulk |
| --- | ---: | ---: | ---: |
| Rainbow | 21.10 / 13.95 | 42.05 / 15.53 | 85.59 / 24.41 |
| Nonlinear colour calculation | 7.10 / 11.73 | 12.39 / 14.45 | 25.61 / 40.92 |
| Sprite example | 52.54 / 41.22 | 54.64 / 41.30 | 59.13 / 44.88 |

Bulk plans added 108–178 bytes of measured host heap in these comparisons.
Heap stayed constant during rendering. The generic native rainbow fill used
less CPU than the authored loop in these measurements, but the authored loop is
still the useful way to teach repetition. Timing is host- and scheduler-specific;
these short samples establish neither jitter tails nor controller thresholds.

The nonlinear example is a concrete counterexample to equating fewer dispatches
with faster execution. Consequently no universal tiny-strip cutoff, approximate
arithmetic, guarded-store opcode or special effect opcode was introduced.
Those were review investigations contingent on evidence, not unconditional
language fixes. Conditions and helpers now reach the existing generic map path;
ordered array updates and skipped writes keep their meaning. Source advice
explains these choices without requiring a learner to restructure working code.

Pico USB accounting bounds are 11,516 bytes main / 5,108 worker, leaving
4,868 / 3,084 bytes. Network-enabled Pico W bounds are 12,152 / 5,120, leaving
4,232 / 3,072 bytes in the existing 16/8 KiB stacks. These are software accounting
bounds, not observed physical peaks. Target heap admission remains authoritative;
the browser's 4 MiB pool is deliberately larger than a controller's pool.

## External acceptance gates

- [ ] Run the [hardware campaign](../teaching-hardware.md) on a controller that is
  available for temporary test playback, using the changed firmware. The attached
  Pico W was running an audio installation and was only queried for identity.
  Device testing is deferred because another job is using the controller.
  RP2350/ESP32 performance and physical stack/command-latency checks remain open.
- [ ] Run the [beginner observation sessions](../teaching-pilot.md), record actual
  confusion and independent changes, then revise the course where needed.

The proposed 60 Hz classroom target is not certified by these software results.
The local preview covers animations and manual live values; A/B transitions,
automatic input drivers and physical sources are tested on a controller. Browser
shape/count/time-step controls are session preferences, not saved device settings.
