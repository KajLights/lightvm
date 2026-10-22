# Pico performance and command-lifetime implementation plan

20 October 2026. Planning baseline: `59b7c52`.

This plan follows the [performance review](pico-performance-review.md), checked
against the current source and recorded measurements. The implementation tracker below records progress; acceptance criteria in the
plan remain targets until the corresponding evidence is recorded.

The objective is to increase useful animation progress while bounding control
service gaps and memory use. Output presentations, layer completions, visible
freshness and command completion are separate measures. There is no new global
60 FPS target.

## Implementation tracker — reviewed 22 October 2026

### Regression repair and repeat comparison — validated

Goal: fix the measured throughput/fairness, preparation/recall and freshness
regressions and the 1,024-pixel replacement failure, then repeat the matched
hardware gates and commit the validated changes. The preceding comparison
remains the baseline evidence; its failed gates are not waived.

- [x] Reduce configuration/parser scratch and add a large-live-scene replacement
   regression, including rejected replacement preserving the active scene.
- [x] Coalesce cheap preparation units within a bounded foreground quantum; test
   cancellation, ownership, busy status and total activation/HA latency.
- [x] Isolate scheduler admission and native execution costs. Recover balanced and
   asymmetric useful throughput and fair layer progress without losing command
   responsiveness; evaluate shared dispatch only with measured benefit.
- [x] Repeat matched archived-bytecode hardware runs, exact pixel/state replays,
   500/1,024-pixel replacements, freshness, authenticated HA and endurance.
   Preserve firmware/package identities, raw samples and unchanged gates.
- [x] Review the full repair diff, run native/Python and strict board/stack checks,
   update this tracker with measured decisions, restore the original saved scene
   and prepare the validated fixes for this commit.

Validated repair: compact JSON tokens; allocate only configured
digital-input descriptors and borrow them in acquisition; cache immutable
register values; use exact binary32 finite checks and budget measured Pico W
kernels in SRAM. Admission-based round robin, publication before new layer work,
and bounded early transition work recover the screened dynamic workloads.
Native contracts cover tight-memory replacement, descriptor rebinding and
updating a reusable layer without modifying its waiting composite. The
1,024-pixel/four-scalar reproduction repeatedly replaces configuration without
an explicit composition release: 58,568 bytes live before and after a rejected
configuration, with generations retained, continuing frames and no reset.

Screens 17–26 isolated preparation and control-service interactions. The
last four-part trace measured preparation, USB dispatch and Wi-Fi work that
could concatenate past 10 ms. The port now services real USB/digital-input
acquisition after both network and engine work. `fg10` retains the original
full-loop metric; a separate `io10` counter measures actual ingress intervals,
so this change is not hidden by replacing the old counter. Screen 27 completed
three heavy replacements in 1,585–1,593 ms with zero observed ingress intervals
over 10 ms. Screen 28 passed five trials with five preparation turns per render
turn and the complete dynamic workload screen. Preparation grants up to 4,096
instructions within the existing 2 ms interpreter slice; its outer target is
4 ms, with cancellation/revision checks between units. The full frozen-source
comparison now passes the measured repair gates. Failed screens and temporary
profiling patches are retained under
`build/performance-review/regression-fixes-2026-10-22/`.

The frozen repair UF2 is `ace68ff719e5f098812b941c0e47123142b25e72303b9ac744aa6deb25e52c4f`.
Its final rerun passes 16 native contracts, 311 Python tests (two skips), focused
ASan/UBSan checks and strict Pico W/Pico 2 W/minimal builds. The complete primary
hardware block passes: +110.1% fixed sprites, +94.3% timed sprites, +6.0% images,
about +30% four scalar layers, and +20.2–22.6% A/B layers. All 30 primary trials
have zero observed ingress-over-10-ms deltas. The [repair report](pico-performance-repair-2026-10-22.md)
tracks the passing secondary matrix, replay, HA and 61.55-minute endurance run.
Three return-baseline trials per primary case confirm the gains; baseline drift
is −2.98% to +0.92%. Firmware readback matches all 2,605 UF2 blocks and all 27
package extents remain unchanged. The original 13 saved objects/settings are
restored and verified, with the optimized firmware left running as requested.
The 460-file public bundle passes its privacy/hash audit. The full repair diff
was reviewed; all 242 frozen source/test/tool hashes match the validated build.

The repeated baseline block is complete against the exact original image; the
candidate's complete workload and endurance campaign passes. Twenty-seven
archived benchmark packages were installed once and verified by USB readback;
a private full-flash snapshot confirms the original code hash and records their
exact payload extents. Firmware-only candidate flashing retains these
packages. All original numeric gates remain in force.

The implementation continuation includes C3 and closed software stack-path
accounting. The first attached-board comparison reopened throughput, freshness,
preparation/recall and replacement-admission gates; the repair results above
track their closure without removing that evidence. A verified 2 MiB backup
preserves the exact pre-optimisation firmware and all 13 original saved objects.
Both comparison images use SDK 2.3.1, two workers and a 2,000 µs slice. The
historical 8.7% single-core deficit uses a different fixture/build and remains
context, not the current comparison denominator. Pico 2 W and electrical probes
still require their hardware.

| Item | Software disposition | Evidence / physical follow-up |
| --- | --- | --- |
| P0 | Complete on attached Pico W | Verified baseline backup, five-trial primary baseline, fixed/seeded/time-indexed hardware replays, 200/500/1024 pixels and eight layers; final candidate comparison and restoration complete |
| P1 | Implemented; overhead gate open | Real dirty/display, wait and epoch measurements recorded; 20 Hz observer overhead measured separately from still-unisolated internal telemetry cost |
| P2 | Complete with conservative scope | Stars geometry moved to creation; unused callback clocks omitted; helper delta history preserved; broader automatic setter hoisting declined as described below |
| C1 | Complete | Owned typed HA light/recall/power/key/indicator requests; shared management parser; scene/reset/configuration ownership now supplied by C3 |
| C2 | Software audit complete | Both linked graphs close with callbacks, library/assembly paths, interrupts and guarded recursion; strict build gate and source fingerprints; attached B2 ROM and stress watermarks pass their scoped checks; Pico 2 W physical follow-up remains |
| P3 | Complete with conservative proof | Verified straight-line unchanged-input reuse, live virtual clocks/ramps/timeouts and manual FRAME; broader branch/map/resource proof deliberately not inferred |
| P4 | Selected fast paths complete | Exact endpoint span copies, integer image mapping, channel/premultiplication fast paths and repeated-colour transform reuse; measured image/map/HSV kernels use budgeted Pico W SRAM; fused fill/transform reserved for a demonstrated residual hotspot |
| P5 | Repair workload and endurance gates pass | All required dynamic layers exceed fresh baseline; 54 primary/secondary trials have zero observed steady ingress-over-10-ms deltas; input readback p95 improves 61.30 → 51.18 ms |
| C3 | Repair atomicity, memory and latency gates pass | Heavy completion median 2,001.88 → 1,408.80 ms; acceptance p95 4.59 ms; ten cancellations preserve the old scene; three 1,024-pixel/four-scalar empty replacements succeed without explicit release |
| P6 | Chunk tuning validated; optional shared batch remains open | Image/asymmetric join waiting is still 21.2%/10.9% of epoch time; only two VMs are eligible in those fixtures. No replacement executor or isolated prototype benefit is claimed |
| P7 | Shared artwork retained; register-value cache measured | Immutable initial values use four charged bytes/register, at most 512 bytes/VM, eliminating repeated operand flash reads. Larger instruction-cache calibration rejected; other caches still require measured hotspots/admission evidence |
| P8 | Complete with conservative scope | Source-mapped map/reuse/resource advice, creation/frame Blocks controls and shared artwork, generation-aware rates/dirty delay, preparation progress/cancel/reconnect; speculative source rewrites and optional rate caps declined |
| V | Attached-board regression repair validated | Primary/secondary, controls, replay, source variants, 61.55-minute endurance and return baseline pass; original state restored with optimized firmware, firmware/package readback and public evidence audit pass. Broader P1/P6 and additional-hardware items remain explicit |

### Hardware comparison completed — 22 October 2026

The [full hardware report](pico-performance-hardware-2026-10-22.md),
[structured results](../benchmarks/pico-performance-hardware-2026-10-22.json),
[raw evidence](../benchmarks/pico-performance-hardware-2026-10-22-raw.tar.gz) and
[hash manifest](../benchmarks/pico-performance-hardware-2026-10-22-manifest.json)
record the comparison, unsuccessful attempts, limits and restoration. The
candidate **does not pass overall optimisation acceptance**.

The attached Pico W (RP2040 B2, 125 MHz, 200 WS2812 pixels on GP16) was compared
with its verified exact pre-optimisation image, associated with `59b7c52`.
Primary tests reuse identical archived bytecode, two workers, a 2 ms slice and
256-byte cache. The initial `1d9b7ea` candidate and the final candidate with the
output-ownership fix have separate identities; hashes/build options are in the
report. Flash extent addresses were not pinned across firmware blocks, so the
comparison does not causally separate scheduling, instrumentation and cache
layout effects.

- **Completed:** five 30-second trials per primary case after seven-second
  warm-up, 3,005 USB latency samples per case/image; fixed sprites, timed births,
  balanced images/scalars, asymmetric layers and A/B scenes.
- **Completed:** three 20-second secondary trials at 500/1,024 pixels and eight
  map layers; five trials of creation-time source variants and unchanged saved
  stars; boundary-only and diagnostic polling comparisons.
- **Completed:** 4,200 hardware manual frames across baseline and both candidate
  compiler sets. Exact pixels and observable clocks/counters/inputs/settings
  match; private guest arrays/internal RNG state are not exposed.
- **Completed:** five heavy-preparation trials, ten safe-boundary cancellations,
  100 dirty-input events, authenticated 62-command recall/power scenarios,
  1,000 alternating dimming controls, and 62.55 minutes on one candidate boot
  spanning USB, HTTP, MQTT, uploads/deletes, replacements and rendering.
- **Restored and verified:** the exact original full-flash image and all 13
  saved objects. Ten public program/preset hashes were checked again over USB;
  original configuration/light controls, connected MQTT and running rainbow
  were verified. Private backups and credentials remain outside published data.

| Finding | Result / disposition |
| --- | --- |
| Matched fixed twelve | 26.65 → 40.01 updates/s, **+50.1%**; controlled fixed gate passes. This is not an identical rerun of the historical single-core measurement |
| Timed births | 32.18 → 46.30/s, **+43.9%** |
| Balanced images | 46.83 → 34.14–34.17/s, **−27.1%**; throughput acceptance fails |
| Scalar / asymmetric / A-B | Required dynamic contributions lose roughly 11–21%; explicit lower latency does not automatically accept that tradeoff |
| Eight identical map layers | 28.56–29.41 → 18.63–24.87/s; no starvation, but fairness ratio worsens 1.030 → 1.335 |
| USB | Six primary cases meet p95 ≤15/p99 ≤25 ms. Two-map p95 rises 10.25 → 11.47 ms; busy-preparation p95 is 17.13 ms under the heavier defined observer load |
| Preparation | Median acceptance 2,020.61 → 9.42 ms; median completion **2,020.61 → 3,590.44 ms**. Ten cancellations preserve the old scene and complete within 20.28 ms |
| HA | Dimming p95 62.42 → 58.38 ms; recall/power scenario p95 **300.18 → 1,349.81 ms**. Recall latency remains unacceptable |
| Input freshness proxy | Composite-readback p95 61.99 → 78.29 ms. Candidate internal output-completion p95 43.93 ms is a different boundary and cannot substitute for the baseline comparison |
| Creation-time work | Fixed geometry **+8.9%**, geometry plus constant style **+31.0%**, independently measured over five trials |
| Reuse / saved stars | Saved-stars median 25.87 → 32.34/s; background goes from about 101 renders/s to zero further renders while valid |
| Observation | 20 Hz STATUS costs about 8–10% baseline / 7–9% candidate in descriptive boundary-only comparisons; detailed polls cost more. Internal telemetry ≤2% remains unisolated |
| Epoch / pair waiting | Image epoch reaches 10.53 ms. Pair joins exceed the 10% P6 investigation gate; cumulative foreground maxima include flash/setup |

**Hardware-discovered output ownership fix:** rapid baseline dimming exposed
`native transition output failed; retrying` at ten sampled checks. Synchronous
light refresh previously allowed its own physical output to resubmit from a
nested render checkpoint. Guarding that output while permitting other output
service fixes direct/composition/transition cases. The native regression fails
before the fix and passes after it. The final candidate has zero sampled retry
errors in both HA stress runs. All 16 native CTests, the Python suite (309 tests,
two skips), and strict Pico W/Pico 2 W/no-network Pico builds pass.

**Replacement-admission failure:** four scalar layers at 1,024 pixels occupy
58,200/65,536 heap bytes, with a 64,604-byte observed peak. Even validating an
empty replacement fails: the 5,136-byte parser and 2,512-byte decoded configuration
coexist before parser release. Explicitly clearing the composition reduces live
heap to 6,600 bytes and allows replacement without reset. The remaining 1,024-pixel
timing trials label this explicit release regime; atomic replacement did not pass.

**C2 evidence:** actual B2 ROM SHA256
`ef07d54bdd259c7858483606118a3c704976801823c38ac5a2fcdffacd2d83fe`.
80/83 exported entries close; relevant flash/float/memory/reset paths use at most
88/68/12/16 bytes, within existing allowances. Debug trampolines and vector
transfer remain explicit unresolved non-helper entries. The same-boot stress
watermarks are 5,740/16,384 main and 2,684/8,192 worker bytes. Linked bounds with
interrupts remain 12,152/5,300 bytes. Pico 2 W still requires its own physical ROM
and stress evidence; neither watermarks nor build success certify all hardware.

### Work reopened by the first comparison (historical)

1. **C3/resources:** reserve or reduce parser/decoded-configuration workspace so
   an admitted scene can safely be replaced or cleared; add the 1,024-pixel,
   four-scalar reproduction to admission contracts.
2. **C3:** profile and coalesce cheap preparation units under a shared foreground
   budget. Judge acceptance, total completion, old-scene progress and busy control
   latency together; preserve cancellation, atomicity and stack tests.
3. **P5/P6:** isolate native image/map execution, epoch admission and code/data
   layout effects, then prototype a bounded shared batch because pair waiting
   crossed the plan's gate. Repeat balanced/asymmetric and fixed-sprite cases.
4. **P1/P5:** recover the input-readback tail and two-map control regression;
   measure steady ingress gaps separately from flash and isolate internal
   telemetry overhead. Keep all original numeric gates.
5. **V:** repeat the accepted candidate matrix after those changes. Pico 2 W,
   APA102/electrical probes, multiple physical outputs, exhaustive admission and
   one/two-worker/quantum sweeps remain outside this attached-board campaign.

The 6 ms and 6 ms/512-byte cache calibrations did not recover image throughput;
no default change was adopted. Optional caches/fusion remain gated by measured
benefit and simultaneous-scene memory. The repair tracker above records the
subsequent workload, memory and control results; this earlier comparison itself
did not close those gates.

### Full-diff review — 22 October 2026

Reviewed the complete implementation against the planning baseline, including
candidate ownership/cancellation, persistence, cached rendering, compiler/frontend
semantics, client reconnect behaviour and the stack gate. Fixed these regressions
before committing:

- **Scene ordering:** slot and named-layer lifecycle commands now cancel older
  preparation. Rejected stale lifecycle requests no longer cancel valid work.
  Revision changes abort at the next preparation unit, before a long guest init
  can consume more work. Native contracts exercise each case.
- **Slot identity:** a single-program slot activation again exposes the `main`
  layer in VM state, preserving later layer addressing.
- **Cached STOP/resume:** a retained reusable direct contribution is presented
  again after STOP; a cleared named layer loses its guest stamp and rerenders on
  PLAY while retaining its readable black snapshot.
  Stopped B-slot fronts discard their contribution stamps. The direct/layer
  regression test failed with black output before the fix and now passes.
- **Client recovery:** expired operation history no longer prevents the editor
  from polling live state. Python FINISH waits use the bounded operation waiter,
  retain timeout identities and preserve optional terminal-error inspection.
- **Stack gate:** limits derive from linked main/worker reservations. Optional
  network targets are filtered using explicit build options, preserving strict
  failure for unknown or missing enabled callbacks. Tests cover reduced worker
  stacks and feature filtering; an additional no-network Pico build passes.

The review adds no hardware performance claim. Software acceptance and the
remaining physical gates below retain their original distinction.

### C3 result and limits

Direct activation and transient FINISH, compositions/presets, recalls, power,
program/layer reset/replacement, custom transitions and preset/configuration
validation/save/apply use controller-owned operations. Startup feeds the bounded
queue as capacity becomes available; six startup outputs are tested. Only one
candidate is allocated globally. Queue (four operations), terminal history
(eight records), parser scratch and candidates are charged to the VM heap.

JSON parsing, package verification, resource initialization, guest init/first
render, composition and transition snapshots return between fixed work units.
Parsers are freed before guest execution. Cancellation never finishes a discarded
guest loop. The old scene runs while the candidate is prepared; transfer checks
revisions and happens only after admission. A newer explicit light update wins;
one first-frame refresh prevents continuous controls from restarting preparation
forever. Later changes flow through normal live rendering.

Flash writes now have a resumable, exclusive writer with page/sector work units,
CRC/readback stages and an indivisible catalog publication point. A cancelled
unpublished write preserves the old root. Light-state debounce waits for an active
writer. Configuration save has separate durable output-state/configuration records;
a cancelled save can retain updated remembered states for already active outputs.
Native configuration apply retains its documented empty-configuration result if
driver construction fails; ordinary scene preparation preserves the old scene.

Acceptance, activation, first completed output and recall fade completion are
separate. Queries survive reconnect; cancellation/supersession/failure are terminal
outcomes. Clients release the command queue between polls; the editor shows
progress/cancel/check and HA reports applied state and terminal failures.
`max_step_us`, stage execution totals and foreground gaps expose native/flash
units that overrun the VM quantum. Hardware service and total preparation targets
remain unproven. See [the complete operation contract](../scene-preparation.md).

### Verification evidence

The [implementation evidence record](../benchmarks/pico-performance-implementation-2026-10-21.json)
identifies the source hashes, executable and all three ELF/UF2 images, profile settings,
RAM reports, full stack reports and test logs. Local logs are under
`build/performance-review/`. The original host executable remains at
`build/performance-review/baseline-lightvm`.

| Check | Result |
| --- | --- |
| Host CTest | 16/16 passed; threaded execution, output/pin lifetime, every preparation yield, each candidate allocation failure, flash write cancellation/faults and input provenance |
| Full Python integration | 302 tests, OK with one skip (host Wi-Fi setup excluded); CMake supplied so board-selection tests run; includes compiler advice, STOP/resume, client and stack-gate regressions |
| Editor Vitest | 150 passed, one skipped; native frontend replay enabled; expired operation history preserves live polling |
| Browser protocol | 20/20 passed, including operation progress/cancellation |
| Production editor | Build passes; existing chunk-size advisory remains |
| Sanitizers | Native platform contract passes ASan/UBSan with `detect_leaks=0`; no LeakSanitizer claim |
| Pico W, Pico 2 W and no-network Pico | Release builds pass flash, RAM and strict whole-path software gates; both MQTT-enabled W profiles pass HA frame checks |
| Exact replay | All fixed-population and 300-frame seeded animated variants match; 650-frame timed-birth/lifetime replay is covered by the full suite |

The current replay has animated dispatch totals **103,731 / 95,289 / 84,864**
(original source without optimisation / original source optimised / geometry at
creation), with aggregate pixel SHA256
`7b9e75d072e4222c88432dc763acae1312938506eebe62f801c8cc87dbd0097d`.
Twelve fixed sprites use **3,570 / 2,970 / 2,250** dispatches across ten frames
(original / geometry at creation / geometry and style at creation). These are
same-compiler interpreter-work comparisons, not Pico speedups.

Stack results include two worst interrupt paths plus 256 bytes per level:

| Profile | Main accounted / reservation | Worker accounted / reservation | Linker reservation margin |
| --- | --- | --- | --- |
| Pico W | 12,152 / 16,384 bytes | 5,300 / 8,192 bytes | 1,760 bytes |
| Pico 2 W | 12,288 / 16,384 bytes | 5,060 / 8,192 bytes | 120,228 bytes |
| Pico, networking disabled | 11,932 / 16,384 bytes | 5,300 / 8,192 bytes | 22,400 bytes |

HA `light_command` / `message` / `ha_tick` frames are **24 / 184 / 656 bytes** on
both W profiles. All three graphs have zero unresolved paths. Attached RP2040 B2 ROM and stress evidence now supports its scoped allowances.
RP2350 ROM and physical stress margins remain conditional.
See [stack-path accounting](pico-stack-accounting.md); these are not physical
stack certificates or measurements of actual free heap.

### Decisions on optional complexity

These are deliberate scope decisions, not claims that unimplemented mechanisms
were shipped. They close the software choices for this pass and preserve the
original measurement gates:

- **General sprite setter forwarding/hoisting and broader reuse/map recognition:**
  retain conservative execution and explanatory advice. The safe explicit creation
  API and current proof remove demonstrated work. Automatic rewrites of aliased
  style/geometry, lazy branches, fault ordering and mutable resources need a
  demonstrated residual hotspot before adding that proof surface.
- **Fused fill/transform:** keep exact endpoint spans, integer image drawing and
  identical-colour transform reuse. Background reuse removes steady-state solid
  rerenders; profile first renders and ramps on boards before introducing a fused
  instruction or changing rounding/fault ordering.
- **Shared batch / adaptive chunk scheduler:** keep bounded pairs and the new
  fairness/deadline ordering. Calibrate existing chunks/quanta on boards; open a
  batch implementation only if the recorded ≥10% wait-cost or end-to-end gain gate
  is met. Host elapsed time cannot settle that decision.
- **Retained scratch-free sprites, image residency, prepared maps, larger caches,
  static composites/dirty spans and RAM hot helpers:** retain existing immutable
  shared artwork and package-backed reads. No new cache has earned its simultaneous
  old/candidate/transition RAM cost with board evidence, especially on Pico W.
- **Per-layer rate caps:** do not add a control that changes time-sensitive birth
  behaviour before measuring the current scheduler. Automatic unchanged-input
  reuse and truthful rates are shipped; a new user rate policy is not required
  to remove redundant work.

### Acceptance work reopened by hardware measurements

1. Identify/flash both boards and ROM revisions. Verify the external ROM stack
   allowances and record both core watermarks under the HA/control/flash stress
   and one-hour endurance matrix. Software source/ELF gates already pass.
2. Establish matched AB/BA baseline/candidate measurements with fixed firmware,
   clocks, output sizes, network regimes and observer rates. Close the fixed
   sprite deficit, dynamic useful-progress, freshness, command latency and
   instrumentation-overhead gates; retain raw samples and uncertainty.
3. Measure heavy-scene acceptance, stage costs, total completion, first output,
   fade and maximum foreground/flash gaps. Check the 10 ms service target and the
   proposed ≥20% preparation improvement, separately from correctness.
4. Use those measurements for the conditional chunk/quantum, batch, fusion and
   cache decisions above. Do not infer a new implementation is needed from host
   timings. If a board gate fails, record the measured cause and reopen the
   relevant item rather than silently weakening acceptance.
5. Exercise real DMA/PIO, USB, CYW43/MQTT, flash lockout, APA102 and WS2812 output
   probes, plus the maximum admitted output/layer combination. Software output
   completion is not an electrical assertion.

Package and persistent formats remain unchanged. The asynchronous command
lifecycle is a current experimental API change: update clients together with the
firmware. Recompile/reupload sources for compiler improvements. No storage
migration or compatibility layer was introduced.

The design sections below retain the full acceptance criteria and proposed
experiments; the tracker and decisions above state the implemented scope.

## Review assessment

The diagnosis is well supported. The recorded fixes are already present:

| Finding | Current implementation | Remaining work |
| --- | --- | --- |
| Output backpressure blocked independent work | `collect_composition()` and `composition_tick_impl()` allow an unfinished layer to progress while a prepared composition waits; `transition_tick()` permits child progress | Preserve these ownership rules with mixed-workload and delayed-output regression tests |
| Completed recall rendered its invisible source | `collect_render_jobs()` sets `destination_only` for a completed, recall-owned transition | Test interrupted recalls and manual takeover; do not generalise this policy to arbitrary stateful invisible layers |
| Native chunks assumed unavailable parallelism | Drawing/maps consult `render_executor_can_split()` and use smaller serial chunks | Measure per-operation cost and whole-epoch delay |
| Excessive slice overhead on Pico W | Its default is now 2000 µs; Pico 2 W keeps 500 µs | Compare matched profiles before further tuning |
| HA parsing stayed live through activation | `light_command()` is a separate non-inlined helper | Remove internal JSON round trips, bound complete paths, and unwind preparation work between stages |

Sources: [application scheduling](../../src/app.c),
[composition](../../src/composition.c), [transitions](../../src/transition.c),
[executor](../../src/render_executor.c), [drawing](../../src/portable_draw.c),
[maps](../../src/portable_map.c), and [HA](../../src/home_assistant.c).

The original review's host experiments were rerun during this planning pass:

- Seeded animated stars, 300 manual frames: **97,410 → 86,985 dispatches**;
  all frame bytes and the recorded aggregate SHA256 matched.
- Fixed twelve-star source, ten manual frames: **381 → 321 → 249 dispatches
  per frame**; all frame CRCs matched the recorded `552113798`.
- These runs used the existing host executable and the scripts under
  `build/performance-review/`. They reconfirm the compiler-work reductions, not
  hardware speedups. Host elapsed times differed, as expected.

The plan tightens five points in the review:

1. **Age is not always staleness.** An unchanged cached background is valid even
   if its original render is old. Track dependency validity and input-change
   latency alongside animation age; do not schedule a cached layer just because
   its timestamp is old.
2. **A seed alone does not make autonomous stars equivalent.** Births use
   `probability * runtime.delta` and one random trial per render. Changed frame
   timing changes the population. Use fixed-step replay for equivalence and a
   time-indexed event fixture for comparable autonomous dynamic workloads.
3. **The fixed benchmark must remain identifiable.** Preserve its original
   source and bytecode identity when adding optimised variants. Do not erase the
   8.7% deficit by silently changing the workload or caching the whole benchmark.
4. **Purity needs more than effect flags.** Render functions have broad rights;
   generated colour styles also write private resources. A blanket ban on all
   resource writes would exclude the very `color.py` case being targeted.
5. **Bounded execution requires actual ingress service.** The Pico checkpoint
   hook polls network work, while USB ingress and MQTT polling also occur in the
   main loop. Merely calling `runtime_checkpoint()` more often does not establish
   a USB/MQTT command-service bound.

Historical hardware numbers remain useful anchors: fixed sprites measured
29.26 completions/s originally and 26.72 after correction; corrected animated
stars measured 26.48/s. They are not newly controlled baselines. The corrected
Pico 2 W firmware still needs hardware testing, and uncontrolled network traffic
limits interpretation of the earlier image comparisons.

## Contracts to establish before optimisation

- Core 0 owns admission, input capture, commits, scene mutation, storage and
  destruction. A VM has at most one executing job. Worker completion publishes
  its writes before core 0 reads them.
- Backpressure follows a specific buffer. A composite pins committed layer
  contributions while reading them; a transition pins the composition fronts
  it samples; DMA pins the submitted presentation buffer. No commit, buffer
  swap, invalidation or free may violate those readers. Independent working
  buffers remain eligible when safe.
- Inputs, time and delta are captured once per invocation and remain stable
  across slices. A started stateful render is resumed, not restarted or silently
  discarded. Manual `FRAME` always performs the requested step.
- Completed automatic recalls may suspend the outgoing scene under the existing
  policy. Manual A/B operation resumes the existing instances. General visibility
  does not grant permission to skip state, RNG, expiry or callback effects.
- Every presentation carries the provenance of the contributions it sampled.
  Completing a newer layer frame does not retroactively make an older in-flight
  composition fresh.
- Preparation must finish validation and reserve peak resources before replacing
  live ownership. Failure or cancellation leaves the old scene usable.
- New metrics, queues, caches and preparation storage have explicit bounds and
  accounting. No per-frame allocation is introduced.

The PoC may change APIs, bytecode and stored formats. Update compiler, firmware,
clients and tests together. Recompile/reprovision incompatible experiments;
do not retain old parsers, migration paths or compatibility shims solely for them.
Conservative execution of a valid stateful program is part of the current
semantics, not a legacy compatibility path.

## Delivery order and dependencies

Each row is a reviewable change or a small series. These are dependency tracks,
not a requirement to implement multiple changes concurrently.

| ID | Deliverable | Depends on | Completion gate |
| --- | --- | --- | --- |
| P0 | Reproducible fixtures and ownership regressions | Current correction | Historical workloads identifiable; mixed-scene failure covered |
| P1 | Freshness, wait, epoch and resource measurements | P0 | Correct provenance; bounded instrumentation overhead |
| P2 | Proven sprite setup changes and callback simplification | P0 | Exact replay; dispatch reduction; matched Pico result |
| C1 | Typed commands and parser lifetime separation | P0 | Equivalent transport behaviour; no internal JSON reparse for converted operations |
| C2 | Command-path stack accounting and core-0 watermark | P1, C1 | Both cores have documented bounds and measured margin |
| P3 | Verified dependency-based contribution reuse | P1, P2 | Background sleeps, invalidation wakes it, stateful programs remain correct |
| P4 | Exact fill/transform and image fast paths | P0, P1 | Reference pixels and faults; lower native cost |
| P5 | Coordinator deadline and fair scheduling | P1, P3 | Bounded service gaps under many layers and outputs |
| C3 | Bounded scene preparation with operation lifecycle | C1, C2, P5 | Atomic activation, cancellation, memory and latency gates |
| P6 | Cost-aware chunks; optional shared job batch | P1, P4, P5 | Lower wait/freshness cost without control regressions |
| P7 | Retained artwork and selective caches | P1, P3, P4, C3 | Demonstrated gain within admitted memory |
| P8 | Compiler explanations and editor controls | P1, P2, P3, C3; P7 for retained sprites | Source-mapped advice, truthful progress/rates |
| V | Final matched-board validation and reprovision guide | Required work above | Published raw results and all mandatory gates pass |

C3 depends on C2's path inventory, tooling and watermark instrumentation. If
meeting the final stack margins requires C3's unwinding changes, complete that
remediation with C3 and rerun C2's acceptance gate before deployment; do not block
the necessary refactor on a pre-refactor bound that it is intended to fix.

Start with P0–P2 and C1. Do not wait for a scheduler rewrite to remove proven
redundant work. Complete P3–P5 and C2–C3 before deciding whether the more complex
parts of P6–P7 earn their cost. P8 can expose each capability as it becomes real.

## P0 — Make regressions reproducible

Extend [benchmark_multicore.py](../../tools/benchmark_multicore.py) and add checked-in
fixtures/replay tooling. Promote the two useful local experiment scripts out of
the ignored build directory, parameterising the executable and output paths.

Keep three independent sprite comparisons:

1. The original fixed twelve-sprite workload, plus fixed populations of 0, 1, 6,
   12 and 20 where admitted. Record forced-render/manual cost even if later
   optimisations can prove a whole scene reusable.
2. The same source structure with geometry, then constant style, moved to spawn.
   Attribute source and runtime gains separately.
3. Seeded real stars at 16 ms manual steps, including the existing 300-frame seed-7
   case and brightness changes at frames 100 and 200. Add a longer replay covering
   repeated expiry/reuse, capacity saturation and colour changes.

For autonomous comparisons, add a dynamic fixture whose births, positions and
lifetimes are generated ahead of time from a seed and indexed by animation time.
Catch up all scheduled events when time advances; do not consume a different RNG
sequence for each rendering rate. Record population and sprite-render count as
well as layer completions. Keep the real stars workload as a separate realistic,
timing-dependent case.

Add mixed static-background/slow-sprite and asymmetric scalar/native workloads.
Retain pure maps, transformed images, balanced layers, two outputs and both A/B
slots. Create a fresh runtime for every case, not just an identical configuration.

Record source and bytecode hashes for **every** program, settings, bindings,
seed/event schedule, pixel count, output driver, compiler/firmware revision,
toolchain, clock, workers, slice/cache/profile limits, network regime, warm-up,
measurement duration, raw samples and errors. Keep layer distributions separate;
the current aggregate `manual_layer_render_us_median` can hide the expensive layer.
Archive the original compatible firmware/compiler/artifacts for historical
comparisons. If the new bytecode format changes, compare those archived builds
with newly compiled equivalent fixtures; do not add old-format support to the
new firmware merely to run a benchmark.

Add deterministic tests with a fake/delayed output driver and controlled clock:
unfinished stars progress during output wait; committed buffers stay pinned
during composition; completed recall suspends A; interrupted recall/manual
takeover remains correct; faults and output retries release the right ownership.
Extend [executor tests](../../tests/test_render_executor.c),
[composition contracts](../../tests/composition_contract.inc),
[transition contracts](../../tests/transition_contract.inc) and the Python
composition/recall tests as appropriate.

**Done:** fixtures reproduce the recorded dispatch counts and hashes; tests
would fail if broad backpressure or invisible outgoing work were reintroduced;
hardware scripts never confuse output presentations with layer progress.

## P1 — Measure the path to the displayed frame

Instrument [animation commits](../../src/animation.c), [VM scheduling](../../src/vm.c),
compositions, transitions, the executor and the [Pico main loop](../../platforms/pico/main.c).
Expose a bounded diagnostic snapshot through the existing management interface.

For each contribution, record an identity scoped to the instance generation,
invocation start/input-capture time, completion time and captured dependency
revision. Carry these identities through both composite buffers, both transition
buffers and the submitted output. Advance displayed provenance only on successful
driver completion. Cancellation/failure must not publish success. Also cover
direct-output programs, missing layers and transparent/zero-weight contributions.

Measure separately:

| Metric | Meaning |
| --- | --- |
| Layer completions and VM dispatches | New render work, including completed but not yet displayed work |
| Distinct contributions presented | Which completed work actually reached a successful presentation |
| Displayed sample age | Driver-completion time minus the sampled invocation's input/time capture |
| Completion-to-display delay | Delay after rendering while composition/output catches up |
| Dirty-to-display delay | First outstanding dependency change to a presentation reflecting it |
| Execution, runnable wait and buffer wait | Separate wall-time intervals; waits labelled by owning buffer |
| Pair wait/idle estimate | Difference between paired jobs and coordinator join wait, with dispatch overhead separated |
| Epoch and service gap | Whole coordinator work and intervals between actual USB/network/MQTT service opportunities |
| Preparation stage times | Validation, admission, init, first frame, activation and cleanup costs |

For retained valid backgrounds, mark dependency state as current; do not treat
their increasing sample age as a missed update. For continuously changing inputs,
retain the first unserved dirty time and the generation captured by each render;
a newer publication must not reset waiting time or be marked served by an older
frame. Sample age periodically as well as at presentations so an output stall
cannot disappear from the distribution.

Driver completion is the software proxy for display. Electrical/optical latency
requires the separate output probe; do not claim the timestamp measures photons.
During fades, retain provenance for both contributing scenes and the transition
progress snapshot.

Keep basic totals/maxima in production. Use bounded diagnostic sampling or small
histograms for distributions; compute quantiles on the host. Workers write only
job-local counters, which core 0 merges after joining. Avoid per-pixel timers,
unbounded logs and expensive stack scans in ordinary status polling. Mark sample
drops and quantile resolution. Measure instrumentation on/off under identical load.
Allocate provenance only for admitted scenes and charge it to their memory budget;
do not statically reserve maximum-size history arrays for every possible output.
Include these bytes in peak old-plus-candidate preparation accounting.

**Done:** the mixed scene explains whether delay came from execution, unfair
turns, composition, pinning or DMA; p50/p95/p99 freshness and latency are available;
basic metrics cost at most 2% median throughput in matched runs. Diagnostic mode
may cost more but must report its measured overhead and allocated bytes.

## P2 — Remove demonstrated sprite work first

Move the fixed transform in [stars.py](../../examples/animations/stars.py) into
`star_init` after position assignment. Keep fading alpha, brightness and expiry
in `star_render`. Preserve the baseline benchmark and add the optimised variant
instead of replacing its source in place.

Then change [sprite_compiler.py](../../lightvm/sprite_compiler.py) in small steps:

- Compute callback time only when reachable code uses it, including inlined helpers.
- Eliminate `batch_delta` only when no callback path can observe its history.
  It writes previous-callback time, so ordinary unused-result elimination is
  insufficient. Test conditional reads, skipped callbacks and reused slots.
- Remove redundant default style writes and forward known style values across
  generated setter/getter sequences when aliasing and control flow permit it.
- Hoist general immutable sprite setup only after proving operands are fixed
  for the sprite lifetime and moving evaluation cannot change faults or effects.
  Prefer the explicit source change before adding such a compiler pass.

Use optimised/unoptimised replay as the oracle. Compare pixels plus observable
state, RNG progression, sprite order, lifetime, slot reuse and clocks. Internal
dead storage may legitimately differ; tests should distinguish it from state
that a later callback can read. Include early return/remove, overlapping sprites,
fractional positions, clipping, wrap and group opacity.

**Done:** reproduce at least the demonstrated geometry-only and constant-style
dispatch reductions for their exact fixtures; validate any further reductions;
report Pico layer/native/dispatch timings without claiming that dispatch reduction
and FPS improvement are the same percentage.

## C1 — Decode once into typed internal commands

Extract command validation and execution from [app.c](../../src/app.c) into a
small internal command API, with proposed `src/command.h` and `src/command.c`.
Use a tagged payload with explicit field-presence bits, owned bounded names,
target identity/generation, expected revisions, source identity and request ID.
Document payload limits and charge any variable payload to a bounded admission
budget. Do not put a union containing every maximum-size document on the stack.

Transport adapters parse once, validate their transport-specific rules, and
return a typed request. The application performs common semantic/revision checks.
Release parser frames before execution; accepted long operations own their data
and never retain pointers into a parser, receive buffer, callback stack or session.

Convert light updates, power and preset recall first, followed by program/scene
activation and reset. Include [HA key commands](../../src/ha_keys.c), which also
format internal JSON. Inventory the remaining management operations and convert
them in bounded groups; the end state has one internal executor rather than
permanent competing JSON and typed execution implementations. External JSON may
remain a wire format, parsed at its boundary.

Preserve deliberate rules: retained MQTT commands do not replay on reconnect;
HA brightness zero means OFF; exposed-preset restrictions remain; stale revisions,
invalid RGB, unknown fields, invalid targets and queue saturation return clear
errors. Test equivalent commands through USB, HTTP/WebSocket and HA. A command
accepted for later work is not reported as successfully applied.

Specify command ordering at coordinator boundaries. Do not silently discard
commands merely because another activation is pending. Short commands execute
at the next safe boundary; long commands use the bounded C3 lifecycle.

**Done:** converted operations have one parsing pass, identical intended state
changes and errors across adapters, no borrowed lifetime beyond decoding, and
no activation beneath a large transport parser frame.

## C2 — Bound complete stack paths

Extend [check_pico_ha_stack.py](../../tools/check_pico_ha_stack.py) beyond its current
three-frame regression gate. Keep that gate until the replacement coverage is
stronger. Generate stack-usage and call-edge evidence from the actual optimised
Pico builds, accounting for inlining/tail calls and the chosen toolchain.

Enumerate roots for USB decode, HTTP/WebSocket management, MQTT light/key
callbacks, startup/restore, storage operations, preparation steps and worker
rendering. Include manifest parsing, allocation, drawing/maps, transition state
copies, error formatting and all render/service work reachable from checkpoints.
Annotate indirect callback targets, SDK/library/assembly paths and permitted
interrupt nesting. Unknown/dynamic stack use on a required path must fail the
gate or have an explicit conservative bound; it is not zero.

Add a core-0 watermark using the real linker boundaries, alongside the existing
core-1 measurement. Fill only unused stack space safely at early startup, or use
an appropriate startup hook; never overwrite live main frames or a guard region.
Report maximum observed use and remaining reservation for each core. Watermarks
support testing but do not prove all paths are safe.

Proposed engineering gate: audited foreground bounds plus interrupt allowance
leave at least 2 KiB of the 16 KiB main stack and 1 KiB of the 8 KiB worker stack
unused. Record the allowance derivation and revisit it if the SDK changes.
Stress measurements must also stay within those margins. Increase reservations
only with a passing whole-profile RAM budget and evidence that remaining frames
are necessary; first eliminate overlapping lifetimes.

**Done:** both boards pass path accounting and stress watermark gates; synthetic
or archived pre-fix reports fail for the right reason; report coverage explicitly
where external code needs a conservative annotation.

## P3 — Reuse verified unchanged contributions

Derive scheduling eligibility from verified render operations at admission in
[portable.c](../../src/portable.c); mirror the analysis in
[portable.py](../../lightvm/portable.py) for explanations. Compiler-provided hints
must never independently authorise skipping execution.

Start with a deliberately narrow supported graph: output computed from constants,
settings/init-only values, immutable resources and captured live inputs. Follow
branches, operands and native/map calls conservatively. Reject reuse for reachable
time/delta/RNG dependencies, persistent state mutation, sprite lifecycle work,
mutable resource dependencies, feedback and unknown effects.

Allow fully reconstructed private temporary styles only when every consumed
field is written on all relevant paths before use and cannot expose prior-frame
state. Alternatively lower the narrow fill/transform pattern to an operation
with explicit immutable operands. This is necessary to optimise the real
[color.py](../../examples/animations/color.py), not just an artificial constant fill.
Init-only state reads can be stable within an instance generation; settings/reset
changes invalidate that generation. Keep the first proof implementation small.

Add explicit current/dirty/in-progress states and captured dependency revisions:

1. Render once after preparation/reset. Retain the existing committed buffer.
2. Before starting an autonomous render, evaluate only the proven dependencies.
   Reuse the contribution if they are unchanged; do not increment render counters
   or dirty downstream composition just for reuse.
3. Wake on changed effective input, binding/source fallback or timeout, output
   control used by this layer, setting/reset, resource/target generation or
   explicit invalidation. Compare captured values as well as binding revisions;
   a ramp changes without a new command.
4. If a dependency changes during a render, finish the captured invocation and
   keep the newer revision dirty. Never update its captured values mid-frame.
5. Preserve clock progress while asleep. Advance the virtual program clock and
   its wall anchor together when reusing a contribution; neither render counts
   nor provenance advances. This keeps newly bound ramps anchored to the current
   virtual time as well as allowing existing ramps to progress. Pause/resume and
   manual stepping retain their defined clock behaviour.

Reuse the evaluation logic in [inputs.c](../../src/inputs.c), with explicit tests
for source timeout/fallback, held values, publisher replacement, ramp endpoints,
repeat/ping-pong ramps and reconnect. Initially polling a bounded number of input
dependencies is acceptable; skipping a full render does not require an elaborate
timer queue. A fully static output must still process commands and input expiry.

Layer opacity/blend/ordering and transition progress can dirty the composite
without rerendering a reusable layer. Master dimming can require a new physical
presentation without changing scene pixels; an input bound to brightness may
also invalidate the particular layer. Keep these dependency domains separate.

Manual `FRAME` bypasses autonomous reuse, with unchanged frame/clock semantics.
Validate malformed/misleading scheduling metadata and forged resource effects.

**Done:** after initial rendering, an unchanged `color.py` background makes zero
additional render invocations over a 30-second run; changed inputs reach the
display promptly; ramps continue; stateful/RNG animations remain autonomous;
no extra pixel buffer is needed. A low background completion rate is success.

## P4 — Specialise exact native operations

Implement and measure separately so the benefit of each path is attributable:

1. **Uniform fill then transform.** Recognise a uniform overwritten span with
   an unobservable intermediate result and stable style. Compute the result using
   the existing premultiplication, unpremultiplication, rounding and transform
   sequence once, then fill the packed bytes. Preserve argument/fault order;
   do not fuse across reads, aliasing or partial writes that make it observable.
2. **Integer translation at unit scale.** In the drawing kernel, recognise the
   effective mapping including origin, not merely an integer `translate` field.
   Avoid interpolation/division only where samples and bounds are exactly equal.
   Keep the general path for fractional mapping, unsupported wrap/scale and alias
   cases. Identity-style opaque copies can be a still narrower subcase.
3. **Completed transition endpoints.** Replace per-pixel endpoint work with
   bounded span copies from the selected composition. Keep the current output
   ownership; a zero-copy presenter handoff is a separate proposal if measured
   copy cost later justifies it.

Use [pixel-array tests](../../tests/test_pixelarray.py),
[draw contracts](../../tests/portable_contract.inc), pixel-map, composite and
transition tests. Cover all supported channel formats, alpha 0/1/254/255, low
brightness, hue/saturation/tint, negative/fractional origins, scale boundaries,
clipping, wrap seams, overlaps and resource generations. Compare with a retained
test oracle/general kernel; unsupported fast-path cases use the current general
operation, not an approximation. Run software-math/DCP comparisons on Pico 2 W.

**Done:** exact output and fault behaviour, no extra per-frame allocation, and a
repeatable native-time gain on the targeted hardware cases. Since reuse removes
steady-state background rendering, measure fill improvements during first render,
input ramps and other changing fills too.

## P5 — Bound the coordinator epoch and protect fairness

The current two-job arrays in [vm.c](../../src/vm.c) flush while collecting jobs,
and [app.c](../../src/app.c) visits all eligible outputs/layers before ending the
deferred-service epoch. Change this before making the executor more complex.

Give the coordinator an absolute service deadline spanning VM slices, composition,
transition evaluation, conversion/packing and preparation. Retain cursors across
epochs. Check the deadline at bounded operation boundaries, stop admitting more
work when the remaining allowance is insufficient, join outstanding jobs, commit
them, release the execution epoch and service actual transport ingress. Do not
dispatch a second full traversal through composition/transition helpers after
the collection loop has exhausted the budget.

Start with a 4 ms work target and a proposed 10 ms maximum foreground service
gap on the standard Pico W workloads. These are design targets to calibrate in
P1, not guarantees from a 2 ms VM slice. Charge any unavoidable final kernel,
commit/copy or driver work to measured overrun. Long native operations must become
resumable or use sufficiently small chunks to meet the resulting bound. Record
flash erase/program gaps separately and bound them by their real hardware cost.

Prioritise eligible unfinished renders with old displayed dynamic contributions
or outstanding dirty inputs. Apply rotation/ageing across outputs and layers so
a costly render cannot starve another output, composition, input service or
preparation. Waiting on a pin/DMA is not runnable work. Cached-valid layers have
no accumulated freshness debt. Use bounded arrays and a simple scan over admitted
layers rather than an unbounded priority queue.

Do not restart renders on continuous input updates, abandon stateful frames, or
make commands mutate objects before all jobs join. Preserve safe mutation points
when an unfinished composite still holds its own longer-lived pins.

**Done:** maximum admitted layer/output combinations and adversarial asymmetric
loads have bounded service gaps and no starvation. Reproduce manual pixel/state
results across worker counts and scheduler settings. Test failure/retry, removal,
flash coordination and reset at every relevant yield boundary.

## C3 — Make preparation a bounded operation

Introduce a coordinator-owned operation record, outside transport callback frames:

`accepted → load/decode → verify → admit → initialise → first render → compose
→ ready → activate → complete`

Every nonterminal stage can enter bounded cleanup and finish `failed`, `cancelled`
or `superseded`. Error state retains the operation ID and reason. Progress reports
the stage and concrete units completed; do not invent percentage estimates for
unknown work.

Refactor `prepare_instance()`, `prepare_composition_data()`,
`prepare_preset_recall()` and transition preparation in `app.c`, plus
`pbc_open()`/verification, resource allocation, `animation_prepare()` and initial
composition. Use bounded byte/instruction/pixel counts and a time deadline.
Incrementally parse/verify large documents or demonstrate that their strict size
bound meets the stage deadline. Allocation/zeroing, snapshots and final cleanup
also count; moving a seven-second function into one state-machine step is not
asynchronous preparation.

Return to the top-level coordinator between steps instead of recursively servicing
rendering under parser/activation frames. Give preparation a guaranteed share of
each cycle so continued animation cannot postpone admission indefinitely.

Use this initial bounded policy:

- One candidate preparation globally, with fair selection among pending outputs.
  At most one compact pending request per output; no queue of fully allocated scenes.
- A newer scene request for the same output supersedes the older uncommitted
  candidate at a safe boundary. Report the old operation as superseded. A full
  cross-output pending set returns `busy`; it never grows silently.
- OFF cancels an uncommitted ON/recall and applies the defined power transition to
  the active scene. Ordinary brightness/colour changes apply to the active scene.
  Before activation, recapture current applicable controls and refresh the
  candidate's first frame if needed. Explicit controls attached to the winning
  scene request follow documented command ordering; a newer control update wins.
- Manual A/B takeover, output deletion/reconfiguration and reset cancel conflicting
  pending work. Validate target generations again immediately before activation.
- Once accepted, an operation is owned by the controller and can be queried after
  a transport reconnect. Do not retain a session reply callback. Retain only a
  bounded set of terminal summaries; boot ID plus operation ID distinguishes resets.

Snapshot/pin the exact preset and program generations being prepared. Account for
old scene, candidate, transition baselines, front/back/presentation buffers, parser
scratch, operation records and optional caches. Release transient parsing storage
before VM execution. Fail admission before live ownership changes. Do not evict
the old scene to make a failing candidate fit. Keep mutation and persistence
ordered so a failed/cancelled candidate cannot become the restored boot selection.

Provide operation query/cancel and completion state to the client/editor. Separate
accepted, scene activated, first output completed and fade completed. HA publishes
applied state at the appropriate transition point and reports failures; an early
acceptance acknowledgement must not masquerade as a successful preset change.

Convert runtime program replacement, preset/composition activation, custom
transition setup and reset to the same staged primitives. Adapt startup/restore
and configuration validation to those primitives as well; boot has no old scene
but still needs bounded work, admission and error handling. Keep deterministic
manual steps as explicit operations with their existing state semantics.

**Done:** the recorded heavy replacement keeps USB/MQTT responsive, old animation
progresses, activation is atomic, and phase-by-phase cancellation/fault injection
returns memory/pins to baseline. Repeated HA switches during preparation choose
the final requested scene without leaks or stale success publications. Measure
total preparation duration separately from acceptance latency; target at least
20% improvement against a fresh matched heavy-replacement baseline, then identify
the dominant remaining stage if that target is missed.

## P6 — Tune chunks, then decide whether to replace pairs

Use P1 timings to assign conservative chunk budgets by operation class, board and
whether the worker is available. Fill, colour transform, general image and map
pixels do not have the same cost. Start with measured fixed tables and bounded
minimum/maximum sizes. Add adaptive estimates only if fixed tables are insufficient;
never rely solely on a recent average as a worst-case bound.

Compare 500/1000/2000 µs quanta with matched one/two-worker builds. Change only one
factor per experiment and preserve the P5 service deadline. Optimise for displayed
dynamic progress and command latency, not worker offload count.

Only replace fixed pairs if residual pair waiting accounts for at least 10% of
measured render-epoch wall time on a required workload, or a measured prototype
shows a worthwhile end-to-end improvement. Use a small fixed-capacity batch of
distinct eligible VMs claimed by either core; prepare stable invocation snapshots
on core 0, execute each job once, then join and commit before mutation. Port the
same contract to the host executor. Avoid worker-side recursive dispatch and
unbounded queues. Batch size itself must fit the epoch deadline.

**Done:** recover the original fixed-workload deficit on matched measurements
without losing map/image gains or violating latency. If pairs are no longer a
material cost after earlier work, retain the simple executor and record why the
shared-batch option is closed without implementation.

## P7 — Retained artwork and memory-backed acceleration

Evaluate these independently after simpler work has been measured:

| Candidate | Implementation and proof obligations | Decision gate |
| --- | --- | --- |
| Retained sprite artwork | Explicit shared immutable artwork plus per-sprite transform/style/lifetime; pin resource generation; skip scratch clear/draw only where equivalent | Dynamic sprite native/VM cost remains significant; exact group-opacity/overlap/fault comparisons pass |
| Small immutable-image residency | Decode once at admission into a charged, bounded cache keyed by package generation, resource identity and format | Package reads are a measured hotspot; measured gain exceeds memory/admission cost |
| Prepared map execution plans | Verify/decode body and invariant dependency metadata once; refresh captured frame values separately | Chunk decode is material; state, faults, final register values and map ordering match |
| Instruction-cache sizing | Count misses/bytes read; compare measured hot-loop sizes per board | Benefit survives many simultaneous VMs and the full RAM budget |
| Static lower-layer composites/changed spans | Cache only a valid layer prefix; invalidate on style/order/generation changes; include both old and new moving-sprite footprints | Composition is still material after reuse; new buffers/metadata earn their cost |
| Pico W hot helpers in RAM | Place only measured hot functions, check map/XIP/flash lockout behaviour | Net hardware gain with passing RAM and flash gates |

Introduce retained artwork as a clear API alongside the general scratch-canvas
sprite API. The latter remains necessary for multi-primitive drawing and group
opacity. An automatic rewrite needs stronger proof than a visually similar
single-image example. Keep existing stateful lifetime callbacks unless explicitly
replaced by an equivalent contract.

Caches are immutable during jobs and admitted/released at safe coordinator
boundaries. No lookup-triggered allocation or eviction occurs in worker code.
Optional caching can leave artwork package-backed when budget or workload makes
residency unhelpful. That is a current storage strategy, not an old-format shim.

The full Pico W profile's recorded **2148-byte reservation margin** is not actual
free heap. VM-charged caches consume the existing VM ceiling; global/native
metadata also affects the linker/native budget. Evaluate peak simultaneous
preparation, not just one active scene. Preserve the 16 KiB operating reserve
unless a new measured budget explicitly replaces it.

**Done:** retain only changes with a measured benefit above run-to-run noise and
passing memory/correctness gates. Record rejected/deferred experiments. Dirty
spans must not claim reduced WS2812 wire time; the complete chain is transmitted.

## P8 — Expose efficient structure and truthful feedback

Add advisory compilation reports separate from errors in
[compiler.py](../../lightvm/compiler.py), propagate them through
[compiler-worker.ts](../../web/src/compiler-worker.ts), types/store/compiler and
the development UI. Advice must not make `Compilation.success` false or disable
upload. Report native-map selection/rejection, dependency/reuse eligibility,
creation versus frame work and resource cost at the selected target settings.

Map diagnostics through the existing JavaScript/Blocks location maps, including
UTF-8/UTF-16 conversions and multiple reports at one source location. Distinguish
measured runtime figures, compiler estimates and unknown memory costs.

Show per-layer update rate, dirty/display latency and cached-valid state separately
from output presentations. Label preparation stages, activation and failure;
provide cancellation. Do not advertise a compile-time FPS prediction.

Python already exposes sprite init/render callbacks. Add corresponding JavaScript
and Blocks authoring support as an explicit feature: shared artwork, position,
opacity, lifetime, “when created” and “each frame”, lowering through the common
compiler. Shared compilation alone does not provide source API parity. Test both
frontends against equivalent Python pixel/state replays.

Consider an optional per-layer maximum update rate only after P5 is stable.
It defaults to automatic scheduling, delays new autonomous invocations without
interrupting unfinished work, and preserves full elapsed delta. Explain that it
can change timing-sensitive animation such as stars births. Manual steps remain
explicit. This control is not the fix for redundant background work.

Broaden native-map recognition in separate changes, beginning with safe ranges
and conditionals. Preserve lazy branch evaluation, floating-point order and fault
order; prove branch safety instead of evaluating both sides speculatively.

**Done:** advice points to the actual source construct; cached layers and pending
operations are intelligible in the editor; frontend replays and UI/compiler tests
pass. Creation/frame controls appear only for capabilities actually implemented.

## Validation and acceptance gates

The following numeric limits are proposed engineering targets, not results or
claims derived from the historical data. Establish the controlled baseline in
P0/P1, then freeze the gates before comparing candidates. If a target cannot be
met, report the measured reason and make a deliberate design decision rather
than silently relaxing it.

| Area | Gate |
| --- | --- |
| Correctness | Exact reference pixels and observable state/RNG for fixed-step tests; no lifetime races, leaks, resets or corrupted storage |
| Fixed sprites | Match or exceed the original firmware's rate in the controlled twelve-sprite case; historical reference is 29.26/s. Report forced-render cost and optimised-source gains separately |
| Dynamic useful progress | Improve the controlled mixed dynamic workload; no required dynamic layer loses more than 5% throughput without an explicitly recorded latency tradeoff |
| Freshness | Mixed-scene p95 displayed sample age and dirty-to-display delay improve or remain within baseline variability; no starvation or stale generation reported as fresh |
| USB status | Mixed 200-pixel scene p95 ≤15 ms and p99 ≤25 ms; also no p95 regression beyond the larger of 10% or 1 ms versus the matched corrected baseline |
| Coordinator service | Standard rendering/preparation foreground service gap ≤10 ms; separately report flash-operation stalls and exact hardware bounds |
| HA | Under a defined local broker load, applied-state acknowledgement p95 ≤500 ms and maximum ≤750 ms for ordinary light operations; preparation acceptance and actual activation measured separately |
| Preparation | Acceptance p95 ≤100 ms; status remains within the control gates while busy; ≥20% heavy-case completion improvement target; atomic activation and bounded cancellation cleanup |
| Resources | Positive full-profile RAM reservation margin on both boards; actual VM/native peaks charged; C2 stack margins; no unbounded allocation growth |
| Instrumentation | Basic telemetry ≤2% median throughput overhead; diagnostic costs and sample loss reported |

No optimisation can pass by merely lowering the count of “needed” benchmark
frames. Once a fixed fixture becomes reusable, report saved work and use its
forced-render variant plus the independent dynamic fixture for throughput gates.

Use this practical matrix rather than an uncontrolled full Cartesian product:

- **Every relevant change, host:** deterministic manual replays, unoptimised/general
  reference paths, one/two workers, lifecycle and allocator/fault tests. Run CTest
  and the affected Python suites; run the full suites at integration milestones.
- **Performance candidates, both boards:** matched one/two-worker Release builds;
  standard and changed slice/cache settings; map, image, scalar, fixed/dynamic
  sprites and mixed scenes at 200 pixels. Repeat relevant output-bound cases at
  500/1024 pixels within admission limits. Test the maximum admitted layer/output
  combination for fairness and service, recording any rejected configuration.
- **Timing runs:** seven-second warm-up, at least five 30-second samples per key
  comparison in varied AB/BA order. Extend runs to obtain at least 1000 samples
  for reported p99 command latency. Keep raw samples, spread and confidence
  intervals; do not claim a small improvement whose interval includes no gain.
- **Network regimes:** connected idle broker with a fixed telemetry poll rate;
  then scripted load including brightness/colour updates and live input traffic,
  with exact rates, payload sizes and broker conditions in the record. Measure
  the observer's overhead. Do not merge idle and loaded results.
- **Lifecycle/control:** direct activation, active/completed/interrupted recall,
  manual A/B takeover, pause/manual step, rapid replacement, cancellation, target
  removal, source timeout and transport disconnect. Run authenticated HA commands,
  not just USB equivalents; include the previous 62-command scenario and a
  longer 1000-command alternating-scene/control stress test.
- **Integration endurance:** at least one hour per board with multiple outputs,
  controlled network traffic, replacements, flash writes and resets; verify
  memory plateaus, boot ID, errors, final selected scene and reboot restoration.
  Include APA102 in output lifecycle checks. Electrical assertions require the
  [output probe](../../tests/pico_output/README.md), not software counters alone.

Use the commands in [repository verification](../../README.md#verification-and-measurements):
CTest, Python `unittest` with `LIGHTVM_RECEIVER` set, protocol tests, `npm test`
and `npm run build` for editor changes. Extend the Pico RAM, stack and flash
overlap gates and regenerate compiler fixtures when package semantics change.

A change is ready when its targeted correctness tests pass, both board builds
pass admission/resource gates, and any claimed performance gain has matched
hardware evidence. A successful Pico 2 W build alone is not hardware acceptance.

## Completion and reprovisioning

Publish the final raw benchmark records, exact build commands/hashes and a short
comparison explaining useful throughput, control latency, preparation latency
and memory. Update the multicore, API, input, sprite and editor guides to describe
the final contract, including any rejected optional optimisation routes.

If bytecode/API versions change, update the release contract and recompile all
test/example programs. If stored formats change, document an explicit catalog
wipe, current-format program/preset upload, hardware/network reprovision and
startup selection. Do not restore an incompatible experimental binary catalog.

The required outcome is complete when the mixed-scene and HA regressions have
permanent coverage, reusable backgrounds stop consuming render cycles, proven
sprite work is removed, coordinator and command lifetimes are bounded, the fixed
sprite throughput gate is met with matched evidence, and
both boards pass the acceptance matrix. Optional complexity is complete when
its experiment has a recorded adopt/defer/reject decision, not when every possible
cache or scheduler mechanism has been added.
