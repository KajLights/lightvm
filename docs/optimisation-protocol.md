# Optimisation and regression protocol

Protocol v1, 24 October 2026. Use this for a compiler, VM, scheduler, storage,
driver or board-profile change. It defines the common workloads, historical
references, run order and acceptance record for every supported LightVM target.
**Useful per-layer updates, render cost, responsiveness and correctness must all
be checked. Output FPS alone cannot establish an optimisation.**

## Start here

1. Record the target, build and rig using the checklist below; freeze baseline
   and candidate firmware and one shared package set.
2. Run host correctness checks and cross-build every affected platform.
3. Run the common animation screen on each physical board. Run five alternating
   baseline/candidate pairs for acceptance, followed by a return-baseline check.
4. Complete numeric, lifecycle, loaded-network, memory and endurance checks
   appropriate to the change. Record unavailable hardware as **unmeasured**.
5. Compare the raw runs, investigate failed gates, verify the final image and
   restore the intended device state. Keep failed attempts and raw evidence.

The commands below implement the common animation and numeric lanes today.
They do not flash firmware. Broader acceptance includes the explicitly listed
operator checks; a successful benchmark command is not a full-platform pass.

## Sources and evidence status

Reviewed chats (titles retained verbatim):

- [Add C3 optimized default profile](thread://01a16d11-7c4c-72d0-a036-e8c02780a1c8?hostId=local)
- [Plan Pico performance fixes](thread://01a15b36-5383-7bb1-96bc-fc4fce24d283?hostId=local)
- [Refine SDK](thread://01a1676c-9333-7fc3-afda-55e25604644d?hostId=local)
- [Review Pico core utilization issue](thread://01a15a37-d464-7743-81c1-705e95b563e9?hostId=local)
- [Check Pico core utilization](thread://01a15994-0fe8-7c70-9f29-9888250708a3?hostId=local)
- [Add Pico 2W board configuration](thread://01a15950-7e4e-77f2-9505-58cf35bac4b1?hostId=local)

The linked reports and raw packages take precedence over rounded chat summaries.
The C3 [report](design/c3-performance-2026-10-24.md),
[structured results](benchmarks/c3-performance-2026-10-24.json) and
[raw archive](benchmarks/c3-performance-2026-10-24.tar.gz) have been preserved here
from that chat. The remaining profile, serial scheduler and USB changes were
subsequently integrated into the current shared-service layout on 24 October.
`LIGHTVM_ESP32_PROFILE=AUTO` now selects the C3 profile on C3 and `standard` on
S3/C6. The recorded hardware timings still identify their original frozen images;
the integration build is not a fresh hardware performance measurement.

Keep four different evidence classes distinct:

| Class | Use |
| --- | --- |
| Historical result | Sanity reference with its original workload, firmware and observer; not an automatic current threshold |
| Short screen | Select experiments and detect large losses; does not establish p99 or a small gain |
| Matched acceptance | Same device, packages and conditions; repeated frozen baseline/candidate builds |
| Final-image confirmation | Recheck the exact delivered image after the last change; earlier candidate results remain separate |

## Supported target matrix

The build matrix is [CI](../.github/workflows/build.yml). The common tools accept
the board IDs below. Pins are example defaults; select the actual wired/test pin
explicitly. Larger logical counts are not evidence of that many physical LEDs.

| Board ID | Executor / profile to record | Example pin | Physical timing evidence available |
| --- | --- | --- | --- |
| `simulator` | Native host, 1/2 workers, OS/CPU/compiler recorded | `GP16` | Host correctness/tool validation; no MCU timing baseline |
| `test_175` | Host 175-pixel / 16 KiB program profile, 1 worker | `GP16` | Capacity/conformance lane; use ≤175 pixels, not the 200/500/1024 matrix |
| `pico` | RP2040, USB-only; verify actual worker/slice values | `GP16` | Builds checked; do not substitute Pico W timings |
| `pico_w` | RP2040, 125 MHz in recorded tests, 2 workers, 2000 µs slice | `GP16` | 20–23 October hardware campaigns |
| `pico2_w` | RP2350 Arm, 150 MHz in recorded tests, 2 workers, 500 µs slice, DCP on | `GP16` | 20 October physical MCU, unconnected output pins; newer numeric/profile acceptance unmeasured |
| `xiao_esp32c3` | 1 actual worker; measured C3 profile: 160 MHz, 2000 µs slice/epoch | `D1` | 24 October campaign, unprovisioned network |
| `xiao_esp32s3` | Current ESP executor reports 1 worker; record actual clock/build | `D0` | Build evidence; matched LightVM performance campaign unmeasured |
| `xiao_esp32c6` | Current ESP executor reports 1 worker; record actual clock/build | `D0` | Build evidence; matched performance campaign unmeasured |

Do not infer executor parallelism from the chip's core count. Use DESCRIPTION's
actual `render_workers`. S3's physical test was interrupted in the C3 chat; its
ROM response and successful cross-build are not performance or sustained-operation
evidence. Kaj-fi audio firmware is a separate workload, not this LightVM baseline.

Pico builds use SDK 2.3.1 / Arm GNU 13.2.Rel1. ESP builds use ESP-IDF 5.5.3 and
its selected toolchain (C3 measurements used GCC 14.2.0). Use the
[README build commands](../README.md#firmware-builds), fresh build directories
and the full CI feature matrix for integration. At minimum measure USB-only
and the deployed service profile separately. Full network builds enable Wi-Fi,
HTTP, WebSocket, MQTT and boot support; the original Pico has no Wi-Fi lane.

On Pico/Pico W/Pico 2 W and native host, compare 1/2 workers when changing
scheduling or kernels. On Pico 2 W compare DCP on/off when changing arithmetic.
On ESP keep the actual single-worker executor unless changing it is the explicit
experiment. Slice/cache/IRAM/clock changes are separate variants, not hidden
differences. Keep refresh uncapped (`LIGHTVM_MAX_REFRESH_HZ=0`) for throughput.
Select worker variants with `-DLIGHTVM_RENDER_WORKERS=1` or `2`, and Pico 2
arithmetic variants with `-DLIGHTVM_PICO2_DCP=OFF` or `ON`. Build `test_175` in a
separate host directory with
`-DLIGHTVM_BOARD_CONFIG="$PWD/tests/boards/host_175.h" -DLIGHTVM_RENDER_WORKERS=1`.

## Historical baseline register

### Common 200-pixel animation controls

Each cell below is **completed layer updates/s / mean render cost in ms**,
using the median of the recorded windows. The Pico values are from 23 October
except saved Stars (22 October repair). C3 before/tuned values use five pairs;
the tuned column predates its USB disconnect repair. The final C3 image has five
saved-Stars windows but only one confirmation window for each other control.
Do not pool those stages. All exact values, ranges and image hashes are in the
[C3 structured record](benchmarks/c3-performance-2026-10-24.json).

| Case / layer | Pico W | C3 preserved standard | C3 tuning candidate | C3 final image |
| --- | ---: | ---: | ---: | ---: |
| `map` | 151.27 / 2.938 | 24.18 / 11.485 | 72.16 / 5.699 | 72.03 / 5.730 |
| `scalar` | 9.62 / 90.748 | 4.31 / 56.121 | 11.26 / 42.380 | 11.26 / 42.723 |
| `draw` | 95.94 / 7.974 | 35.46 / 12.041 | 46.76 / 9.655 | 46.67 / 9.606 |
| `sprites` | 73.31 / 9.874 | 5.31 / 48.784 | 42.39 / 9.743 | 41.08 / 9.818 |
| `control-rainbow` | 147.69 / 1.664 | 38.79 / 6.097 | 91.61 / 3.541 | 91.15 / 3.607 |
| `control-sweep` | 144.32 / 3.961 | 18.24 / 14.138 | 71.98 / 5.277 | 71.58 / 5.238 |
| `stable-stars` | 41.47 / 18.985 | 2.66 / 97.251 | 21.88 / 20.371 | 20.56 / 20.892 |
| `mixed`, map | 112.61 / 5.654 | 19.22 / 11.435 | 38.09 / 5.227 | 36.82 / 5.288 |
| `mixed`, scalar | 6.95 / 97.798 | 2.65 / 58.247 | 5.64 / 45.508 | 5.64 / 45.650 |
| `saved-stars`, foreground | 40.18 / 14.72 | 4.46 / 53.695 | 36.14 / 11.125 | 33.91 / 11.838 |
| `saved-rainbow` | Unmeasured here | 47.48 / 5.883 | 103.89 / 3.424 | 103.66 / 3.418 |

The `sprites` control and the older `fixed12` campaign are different packages.
Saved Rainbow and `control-rainbow` are also different. Color/background reuse
has zero steady repeat renders; its render cost is **not applicable**, not 0 ms.

Saved Stars has a random population: C3 final range 32.91–39.41 updates/s versus
tuning 34.51–40.44 and historical Pico 39.34–49.26. Use it as a user-scene check,
not the sole regression gate. `stable-stars` uses capacity 20, duration 1000 s,
probability 10000/s, fixed colours, ≥2 s and ≥25 completed warmup frames. Its
population is stable, though positions are not a seeded cross-device replay.

Cross-board conditions differed: Pico had Wi-Fi/MQTT connected and PIO/DMA;
C3 had no provisioned network and RMT. Saved Stars used brightness limit 255
on Pico and 32 on C3. These numbers cannot establish a CPU-only ranking.
The preserved C3 standard binary includes instrumentation but the old two-job
collector; today's `standard` option after integration includes the scheduler
repair, so rebuilding that option will not recreate the historical denominator.

### Earlier timing anchors and regression traps

| Recorded experiment | Measured result | Interpretation / source |
| --- | --- | --- |
| Pico W saved Stars, 20 October | Original 22.91 updates/s; regressed 5.39; corrected 26.48 | Regressed output still presented at 103.20/s. [Report](multicore-rp2040-validation.md#follow-up-stars-rendering-regression) |
| Pico W fixed twelve, 22 October repair | 26.65 → 56.00 updates/s | Different controlled campaign from `sprites`; [repair](design/pico-performance-repair-2026-10-22.md) |
| Pico W timed sprites / two image layers, repair | 65.77 / 49.63 updates/s per dynamic layer | Balanced and asymmetric controls are both required |
| Heavy preparation, repair | 2001.88 → 1408.80 ms median completion; acceptance p95 4.59 ms | Completion and acceptance are different boundaries |
| Dirty input readback, repair | p95 61.30 → 51.18 ms | Internal completion p95 29.15 ms is a different metric |
| HA dimming / recall+power, repair | p95 53.07 / 236.80 ms; maxima 110.13 / 297.79 ms | 1000 dimming + 62 recall/power commands |
| 1024-pixel four-scalar empty replacement, repair | 98.95–104.14 ms; 58,568 live VM bytes | Must succeed without manually releasing the old scene |
| Pico W endurance, repair | 3693.11 s (61.55 min), one boot | Separate from C3's two 120 s soaks |
| Pico W Rainbow, 20 October | 200/500/1024 output FPS: 149.50 / 62.61 / 30.96; 200 render 1.938 ms | Earlier bytecode/build; [RP2040 report](multicore-rp2040-validation.md) |
| Pico 2 W Rainbow, 20 October | 200/500/1024 output FPS: 155.24 / 64.45 / 31.71; render 0.574 / 1.086 / 1.939 ms | Physical MCU, no connected strips; [RP2350 report](multicore-validation.md) |
| Pico 2 W transformed image | One/two workers: 2.665 / 1.899 ms; two workers without DCP 2.497 ms | DCP's measured ~24% reduction is workload-specific |
| Pico 2 W four scalar layers | One/two workers: 5.20 / 9.08 updates/s each | Earlier scene, not a current numeric-suite baseline |

At 200 pixels, the 23 October Pico W numeric campaign measured these median
render costs in µs. Repeat at 500 and 1024; the full tables are in the
[numeric report](design/numeric-pico-validation-2026-10-23.md).

| Frozen workload | Scalar, 2 workers | Bulk, 2 workers |
| --- | ---: | ---: |
| `G1-l64-o-19` | 14874.1 | 1729.8 |
| `G2-l17` | 16825.2 | 2021.6 |
| `Q1-p32768` | 23105.5 | 3111.9 |
| `B1-lazy-piecewise` | 26289.5 | 3815.6 |
| `M1-all-pairs` | 82291.4 | 31716.1 |
| `M2-all-pairs` | 72785.6 | 30344.8 |

### Known open gates

- Pico W brightness changes at 500 logical WS2812 pixels exposed synchronous
  output refresh. At 10 Hz MQTT brightness + 10 Hz each HTTP/WebSocket STATUS,
  with USB STATUS at 20 Hz, the final 30 s window recorded 279 actual ingress
  gaps >10 ms and USB p95/p99 25.8/29.9 ms. The baseline also failed. Faster
  numeric execution does not close this gate.
- C3 tuning's 50 windows had one ingress gap >10 ms; final confirmation's 14
  windows had zero, but its mixed 120 s soak had one. A strict ≤10 ms service
  guarantee remains unproven. Short final confirmations cannot replace five
  matched acceptance pairs for the final binary.
- Pico W at 63,474 live bytes plus A/B/transition could reject even the cleanup
  configuration parser. Clearing B recovered without reboot. Record this
  near-capacity admission limit separately from the repaired 1024/four-scalar case.
- Basic internal telemetry overhead ≤2% remains unisolated. STATUS observer
  overhead was material (up to 15% in the repair's sparse image check).
- Modern Pico 2 W, plain Pico, S3 and C6 acceptance, APA102/electrical/optical
  checks and multiple connected outputs remain unmeasured where reports say so.

These are historical open findings, not assertions that every newer build has
the same fault. Close them only with new, identified evidence of the same test.

## Freeze the experiment

Save a public `context.json` alongside each campaign, with these fields:

| Category | Required record |
| --- | --- |
| Identity | Date, protocol version, stable rig ID, board/chip revision, physical strip count and wiring; host CPU/OS for host runs |
| Software | Git SHA and dirty patch, compiler SHA, SDK/compiler versions, binary SHA256, ELF/map, CMake cache/sdkconfig, firmware verification log |
| Resources | Actual CPU/flash clock, workers, slice/epoch, work budget, cache, VM heap, stack reservations, storage layout, driver/DMA mode, refresh cap |
| Workload | Package hashes, settings/input bindings, initial state/seed policy, physical/logical count, brightness/order, A/B/layer layout |
| Environment | Network provisioned state, broker and traffic recipe, clients, observer rates, USB host/port, warmup/window/trials and case order |
| Outcome | Raw run files, failure/retry logs, correctness result, each gate's status, final firmware and restoration checks |

Keep private full-flash backups and credentials outside public evidence. Back up
the existing configuration/programs and intended playback before running: tools
temporarily configure outputs. They restore active configuration and remove their
temporary programs, but do not recreate arbitrary unsaved scenes, RNG position,
or every original light-control value. Explicitly restore and verify those at end.
If an experimental storage layout is incompatible, use the documented
[clean installation](architecture.md#clean-installation-after-experimental-changes)
and reprovision; do not add a migration to make the benchmark work.

## Executable common sequence

Run from the repository root. The commands use Bash arrays. Freeze packages once:

```bash
PY="$PWD/build/venv/bin/python"
RUN="$PWD/build/optimisation-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$RUN"
"$PY" tools/prepare_optimisation.py --output "$RUN/packages"
"$PY" tools/prepare_optimisation.py --suite numeric --output "$RUN/numeric"
```

Preparation verifies the archived bundle, member manifest and every bytecode hash.
It extracts data files only into a new directory. Keep these bytes unchanged
across board/firmware trials. A compiler change needs an additional, separately
labelled lane with newly compiled artifacts and exact semantic replay; it must
not replace the frozen firmware comparison. When obsolete bytecode is no longer
admitted by design, create and document a new baseline cohort rather than add a
compatibility path or compare unlike packages.

Build and check a host reference using the current sources:

```bash
cmake -S . -B "$RUN/host" -DLIGHTVM_PLATFORM=host -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_HTTP=ON -DLIGHTVM_WEBSOCKET=ON -DLIGHTVM_MQTT=ON -DLIGHTVM_BOOT_SEQUENCE=ON
cmake --build "$RUN/host" --parallel
ctest --test-dir "$RUN/host" --output-on-failure
LIGHTVM_RECEIVER="$RUN/host/lightvm" "$PY" -m unittest discover -s tests -v
"$PY" tools/replay_performance.py --process "$RUN/host/lightvm" --output "$RUN/replay.json"
```

For compiler/frontend changes also run `npm test`, `npm run build`,
`node --test tests/test_editor.mjs` with `LIGHTVM_RECEIVER` set, and the
[numeric matrix](design/numeric-workloads.md). Use ASan/UBSan for native changes,
ThreadSanitizer for concurrency changes. Sanitizer timings are not baselines.

Select one board from the matrix, its actual port/pin and a stable rig label:

```bash
BOARD=pico_w
PIN=GP16
RIG=pico-w-01
TRANSPORT=(--port /dev/cu.REPLACE_WITH_DEVICE)
NETWORK='station connected; MQTT connected idle; no intentional commands'
INGRESS=(--ingress-instrumented)
PIXELS=200
# Host alternative:
# BOARD=simulator; PIN=GP16; RIG=local-host
# TRANSPORT=(--process "$RUN/host/lightvm"); NETWORK='host; no network load'; INGRESS=()
# ESP: BOARD=xiao_esp32c3, PIN=D1; S3/C6 use D0 unless wired otherwise.
# The animation runner automatically uses no-reset USB opens on ESP.
# Only set INGRESS after confirming firmware instruments actual USB acquisition.

run_block() {
  local label="$1" firmware="$2" trials="$3" warmup="$4" seconds="$5"
  local order=()
  case "$label" in *-2|*-4) order=(--reverse-cases);; esac
  "$PY" tools/benchmark_animation_profile.py "${TRANSPORT[@]}" \
    --board "$BOARD" --pin "$PIN" --rig "$RIG" \
    --packages "$RUN/packages" --firmware "$firmware" --label "$label" \
    --network-regime "$NETWORK" "${INGRESS[@]}" --pixels "$PIXELS" \
    --samples "$trials" --warmup "$warmup" --seconds "$seconds" --poll-hz 20 "${order[@]}" \
    --result "$RUN/$label.json"
}
# Set this to the image already flashed AND verified (host: the executable).
IMAGE=/absolute/path/to/verified/lightvm.uf2
run_block screen "$IMAGE" 1 1 3
```

The default cases are map, scalar, draw, twelve sprites, Color, Rainbow, default
Stars, Sweep, mixed map/scalar, stable Stars and stable Stars/background. The
runner verifies board identity and package readback, starts fresh compositions,
checks for reboots/generation changes, checkpoints raw samples and checks cleanup.
Use `--case` repeatedly on the direct command for a subset; the comparator's
scope is only those selected cases. `test_175` uses `PIXELS=175` (or smaller if
admission requires); never silently reduce counts on the other boards.

Acceptance order, with verified firmware-only flashes between A/B as needed:

| Pair | Run first | Run second |
| --- | --- | --- |
| 1 | baseline-1 | candidate-1 |
| 2 | candidate-2 | baseline-2 |
| 3 | baseline-3 | candidate-3 |
| 4 | candidate-4 | baseline-4 |
| 5 | baseline-5 | candidate-5 |

For each entry call `run_block LABEL IMAGE 1 7 30`. The function reverses case
order on even pairs (multi-trial blocks also reverse internally). Start with
five 30 s windows; extend slow cases to
obtain useful completions and at least 1000 STATUS observations per case/firmware
for p99. Keep the same extended duration in both groups. Run the original
baseline again for three windows after the candidate to expose drift.
Do not combine different firmware hashes inside either comparison group.
The default 11-case block takes at least 6 min 47 s (11 × 37 s), plus setup;
five pairs take at least 68 minutes plus flashing. A screen is about one minute.
Schedule saved-scene, numeric and one-hour endurance lanes separately.

```bash
"$PY" tools/compare_optimisation.py \
  --baseline "$RUN"/baseline-[1-5].json \
  --candidate "$RUN"/candidate-[1-5].json --output "$RUN/comparison.json"
```

Exit 0 means the **recorded steady-state subset** passes; 1 means a failed gate;
2 means insufficient/incompatible evidence. Missing cases/trials, different
packages/rig/observer settings, duplicate runs and failed cleanup cannot pass.
Unknown ingress instrumentation and short latency screens stay incomplete.
Review the per-window range as well as medians. Re-run borderline or variable
results with longer windows; report uncertainty before claiming a small gain.

Saved-scene confirmation is a separate diagnostic block, using frozen originals:

```bash
"$PY" tools/benchmark_animation_profile.py "${TRANSPORT[@]}" \
  --board "$BOARD" --pin "$PIN" --rig "$RIG" --packages "$RUN/packages" \
  --firmware "$IMAGE" --label saved --network-regime "$NETWORK" "${INGRESS[@]}" \
  --case saved-stars --case saved-rainbow --samples 5 --warmup 7 --seconds 30 \
  --result "$RUN/saved.json"
```

Repeat admitted primary controls at 500 and 1024 pixels, recording rejected
configurations. Take a separate boundary-only `--poll-hz 0` block and a separate
detailed freshness block; do not mix either with 20 Hz throughput results.
Repeat the screen and affected acceptance cases on the **final binary after
every post-measurement change**, including transport-only fixes.

### Numeric correctness and throughput, all targets

Set `USB_FLAGS=(--no-reset)` for ESP and `USB_FLAGS=()` for Pico/host. Check the
board identity first with the common runner. Numeric timing alternates scalar
and bulk modes within each firmware; it is not a baseline/candidate firmware
comparison. Its `passed` field means the harness completed, not all service gates.

```bash
USB_FLAGS=()
case "$BOARD" in xiao_esp32*) USB_FLAGS=(--no-reset);; esac
"$PY" tools/smoke_controller.py "${TRANSPORT[@]}" --board "$BOARD" \
  --reference "$RUN/host/lightvm"
"$PY" tools/benchmark_numeric.py "${TRANSPORT[@]}" "${USB_FLAGS[@]}" --pin "$PIN" \
  --packages "$RUN/numeric" --firmware "$IMAGE" --label correctness \
  --network-regime "$NETWORK" --correctness \
  --case G1-l64-o-19 --case G2-l17 --case Q1-p32768 --case B1-lazy-piecewise \
  --case M1-all-pairs --case M2-all-pairs \
  --pixels 1 31 32 33 63 64 65 127 128 129 200 500 1024 \
  --result "$RUN/numeric-correctness.json"
"$PY" tools/benchmark_numeric.py "${TRANSPORT[@]}" "${USB_FLAGS[@]}" --pin "$PIN" \
  --packages "$RUN/numeric" --firmware "$IMAGE" --label throughput \
  --network-regime "$NETWORK" \
  --case G1-l64-o-19 --case G2-l17 --case Q1-p32768 --case B1-lazy-piecewise \
  --case M1-all-pairs --case M2-all-pairs \
  --pixels 200 500 1024 --samples 5 --warmup 7 --seconds 30 \
  --result "$RUN/numeric-throughput.json"
```

Adjust the counts to the explicitly bounded `test_175` profile. The six cases
are the common timing subset, not the complete conformance matrix: before
accepting numeric/compiler changes run all deterministic matrix cases (omit the
six `--case` arguments from the correctness command; it excludes default unseeded
control Rainbow/Stars), lazy-fault/retention tests, independent
emitter and native-math corpus described in the numeric specification. Do not
compare unseeded random frames between two independently reset device programs.
Keep ordered RNG/state/scatter negative controls in the full suite.
The smoke tool uses the board's example pin from the target matrix, so run that
line only on a rig using that pin. For other wiring, use the numeric runner's
explicit `--pin` and perform equivalent reference-frame checks on the chosen pin.

## Extended acceptance script

Perform these steps on each relevant board/profile. USB-only Pico omits network
steps explicitly; missing connected-output or probe hardware is unmeasured.

| Step | Procedure and evidence |
| --- | --- |
| Exact replay | Same packages/inputs/deltas on reference and candidate; fixed sprite populations 0/1/6/12/20, time-indexed sprites, transforms, scalar/bulk chunks and ordered negative cases. Capture pixels and exposed state; source changes get a separate lane. |
| Fairness/freshness | Two maps, two images, four scalar layers, asymmetric map/scalar, eight layers where admitted, two outputs and two layers per A/B slot. Record each layer's completions and distinct displayed contributions, p95/p99 sample age, dirty-to-completion, join wait and longest job/epoch. End a recall fade and verify hidden outgoing work stops. |
| Preparation | Five heavy replacements while old playback continues; timestamp request acceptance and activation separately. Cancel ten candidates; reject invalid/oversized ones; assert old generation, heap and frame survive. At 1024/four-scalars require empty configuration replacement without pre-clearing. Test near-full A+B+transition admission separately. |
| Loaded control | USB STATUS 20 Hz, HTTP/WebSocket STATUS 10 Hz each, MQTT brightness 10 Hz for ≥30 s at 500 logical pixels. Repeat MQTT-only, HTTP/WebSocket-only and cached Color if a failure needs attribution. Also exercise colour and live-input updates. Record exact load payloads and rates. |
| HA lifecycle | Authenticated broker: 40 preset switches, ten OFF/ON cycles, controls (the 62-command recipe), then 1000 brightness commands. Include active/completed/interrupted fades and manual A/B takeover. Measure applied-state publication, not send completion. |
| USB/storage | Fragmented and exact 64-byte packet boundaries through 528 bytes; disconnect/reconnect during upload/preparation. Replace stored programs during rendering, retain pinned old generation, verify readback/reboot execution and delete/recreate. Required for C3 mapped-storage/USB changes. |
| Resource checks | Fresh boot for lifetime peaks; steady and old+candidate+A+B+transition heap; cancellations and rejected admission; native/driver reservations, linked RAM/IRAM/DRAM/flash and both stacks. Run Pico strict RAM/stack gates and measured watermarks, retaining ≥2 KiB main and ≥1 KiB worker margin; ESP reports actual task-stack and heap headroom. |
| Endurance | ≥60 minutes on one boot with mixed scenes, periodic switches and load; retain boot identity, errors, heap series and counters. Two-minute soaks are screens. |
| Output hardware | Connected WS2812 and APA102 and multiple physical outputs where supported. Probe waveform, latch, gaps and buffer ownership separately from logical timing. No optical claim from USB snapshots. |
| Restore | Verify intended firmware bytes, saved/active config, program/preset hashes, power, brightness, colour and selected preset; verify advancing frames/network recovery and removal of temporary programs. Record any restore failure. |

Existing detailed harnesses: `benchmark_pico_campaign.py`,
`benchmark_preparation_hardware.py`, `benchmark_dirty_hardware.py`,
`benchmark_ha_hardware.py`, `benchmark_usb_hardware.py`, `benchmark_multicore.py`,
`check_render_math.py`, and [PIO output probe](../tests/pico_output/README.md).
**These older specialised harnesses are not uniformly board-aware**: several
hard-code GP16/GP17 or reset-on-open. Use them on their documented Pico rigs;
before using one on ESP, parameterise its pin/transport and verify identity, or
perform the same step using the board-aware client/API. Do not blindly run a
Pico command on an ESP and mark that board covered. The frozen numeric archive
also retains the service-load and peak-memory recipes; their local paths and
credentials must be supplied for the actual rig.

The imported `check_storage_hardware.py` is board-aware and checks repeated flash
replacement/readback, pinned generations, deletion/recreation and stable heap.
`check_numeric_runtime_hardware.py` now accepts `--pin` and `--no-reset` for
scalar/bulk lazy-fault, recovery and rejected-admission checks on ESP as well as
Pico. Both accept `--process` for host validation.

## Acceptance and reporting

These are engineering gates, not numbers inferred from historical tables.
They carry forward the [Pico plan](design/pico-performance-plan.md#validation-and-acceptance-gates)
and [numeric requirements](design/numeric-workloads.md). Freeze them before a run.

| Gate | Acceptance |
| --- | --- |
| Correctness | Exact deterministic pixels/state under the defined contract; no faults, resets, corruption, ownership errors or leaked resources |
| Useful progress | No required deterministic dynamic layer loses >5% median updates/s; no >5% median render-cost increase. Report rate and cost independently. |
| Claimed improvement | Repeated matched gain beyond variability in intended workloads; for newly eligible bulk kernels, ≥15% on at least two required families. A wire-limited FPS plateau does not disprove a render-cost gain. |
| Cached work | Unchanged Color/background does no repeated rendering; input changes invalidate it promptly. A stalled dynamic layer is never reclassified as cached. |
| USB | p95 ≤15 ms, p99 ≤25 ms; p95 regression ≤max(10% of baseline, 1 ms); ≥1000 observations per firmware/case for p99 |
| Ingress/service | Zero observed steady actual ingress gaps >10 ms; report full foreground-loop gaps separately. Setup, flash and near-boot intervals are separate measured phases, not discarded failures. |
| Freshness | Mixed-layer and input-to-display p95 within matched baseline variability; no starvation or stale generation marked fresh |
| Preparation/HA | Acceptance p95 ≤100 ms; ordinary HA p95 ≤500 ms/max ≤750 ms. No >5% matched completion regression; ≥20% heavy-case improvement when that is the claimed optimisation. |
| Resources/endurance | Positive declared RAM/flash margins, full stack accounting plus measured margin, stable post-warmup heap, cleanup and one-hour stability |
| Observer cost | Matched polling conditions; measure observer overhead separately; internal telemetry target ≤2% remains a separate gate |

The comparison tool automates the steady animation rate/cost, cached-work,
USB/ingress and sampled-heap checks only. It reports per-window medians/ranges;
five trials do not establish a hard real-time bound. Flat sampled heap does not
prove allocation-free rendering. Review workload settings and the public context
record even when the tool accepts their identity fields.

Report **pass / fail / unmeasured / not applicable** per board and gate. An
inherited failure remains a failure, with a separate statement of whether the
candidate worsens it. Explicitly document any accepted tradeoff; do not edit
thresholds after seeing results or replace the baseline with a failed candidate.
Promote a new per-board baseline only with its hashes, raw runs, final-image
confirmation and disposition of every gate. Do not overwrite older evidence.

Minimum final report: source/binary/package identities; board/profile and network
regime; per-layer rates/render costs with variability; latency sample counts and
tails; memory/stack/admission; correctness; endurance; unresolved gates; final
installed image and restoration outcome. Link the machine-readable comparison
and raw archive. A build-only or host-only pass never closes a physical-board row.

## Validation of these tools

On 24 October this protocol's host check passed 25 native suites and 11 focused
measurement/comparison tests. Both case orders completed (44 common animation
windows), saved Stars/Rainbow completed two more windows, and 96 scalar/bulk
comparisons passed at 33/200/1024 pixels. Active configuration and temporary
program cleanup were verified by the common runner. The comparison correctly
reports the short host screens as incomplete; these are tool checks, not a new
performance baseline. All 352 C3 evidence member hashes and local document links
were checked. No physical board was flashed or benchmarked for this protocol.
