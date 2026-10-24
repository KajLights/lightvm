# ESP32 optimisation campaign — 24 October 2026

Working branch: `codex/c3-profile-source`, inherited from c314.
Merge base for this campaign: `b77c3d2` (main `15096bc` merged into C3 work).

## Frozen design and gates

Compare `standard` and board-matching `c3`/`c6`/`s3` profiles at 160 MHz CPU,
DIO 40 MHz flash, 512 KiB store, 128 KiB VM heap, 1024 pixel capacity and one
actual render worker. Both include main's shared device services and native
board sensors, the single-worker collector repair, actual USB ingress timing,
and the 100 ms USB-disconnect confirmation.

Candidates add strict `-O3`, annotated hot kernels in IRAM, partition-lifetime
mapped storage, and 2 ms slice/epoch targets. No fast-math, clock increase,
PSRAM assumption or additional worker is involved. Explicit resource overrides
remain available. Fresh standard builds are the denominator; old C3 firmware
is a historical reference only.

Follow `docs/optimisation-protocol.md` without relaxing thresholds after seeing
results: five alternating baseline/candidate pairs, return baseline, exact
pixels and numeric replay, storage/USB lifecycle, memory/stack and one-hour
endurance. Report failures and unmeasured gates explicitly. Short screens select
candidates; they do not promote a baseline. Source/package hashes and raw runs
are retained in `build/esp32-campaign` and the final evidence archive.

The user authorised clean installs on all three boards. Initial identities:
C3 `esp32-d4f98d0449c0`, C6 `esp32-10bda3999904`, S3 `esp32-a4cb8fe03008`.
Initial chip temperatures: 52.1, 53.6 and 62.1 °C respectively. Read native
`board.temperature_c` every two seconds on the same serial connection during
work, including warmup. Stop and clear outputs for invalid/stale readings or
>=80 °C. This is a conservative campaign stop threshold, not a chip rating or
surface-temperature measurement. These observers are identical in both groups.

Use D1 on C3, D0 on C6/S3, 200 logical pixels for the common screen; physical
strip wiring/optical output has not been established. Full network services are
compiled, with clean unprovisioned AP configuration. USB-only cross-builds and
separate measurements are also required. Broker-authenticated/loaded network
and waveform/optical checks need their actual rig; do not claim them from USB.

## Progress

- [x] Merge main, resolve conflicts and remove duplicate CPU telemetry hook.
- [x] Full-service host: 29/29 CTests.
- [x] Guard/comparison tests: 3 thermal + 9 protocol tests.
- [x] Recover and hash-verify 11 animation / 68 numeric packages.
- [ ] Build and screen all three standard/candidate pairs.
- [ ] Investigate regressions, freeze selected images and repeated comparisons.
- [ ] Correctness, lifecycle, resources and endurance.
- [ ] Final image verification, report, raw evidence and clean device state.
