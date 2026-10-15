# Component runtime experiment contract

These are isolated, opt-in architecture prototypes, not production firmware.
Each backend lives in its own directory (`wamr`, `native`, `typed`) and provides
`run.py --output PATH`. A successful run writes a JSON object and exits zero.
Failures must exit nonzero. Missing dependencies are reported explicitly, never
replaced with a different runtime or counted as a pass.

## Shared numerical workload

Use unsigned 32-bit arithmetic, wrapping after each operation. For each frame,
initialize `x = seed ^ ((frame * 0x9e3779b9) & 0xffffffff)`. For pixel `i`:

1. `x ^= x << 13`
2. `x ^= x >> 17`
3. `x ^= x << 5`
4. `x += i + 0x6d2b79f5`
5. Write `(x & 255, (x >> 8) & 255, (x >> 16) & 255)` to RGB8 native storage.

The common comparison samples are 300 pixels, seed 305419896 (0x12345678),
frames 0, 1 and 2. Report their FNV-1a 32-bit hashes as eight lowercase hex digits
in `workload.frame_hashes`. FNV starts at 2166136261; for each RGB byte XOR it
into the hash then multiply by 16777619, wrapping to 32 bits. The coordinator
checks these against an independent Python implementation.

## Required demonstrations

- Two independent effect instances, with resumable bounded computation, not
  restarting or merely aborting after each scheduling quantum.
- Native access to the same RGB backing storage used by effect computation;
  native fill/blend or another bulk transform, with no mandatory per-pixel host
  call. Report copies and retained allocations, and test bounds/stale handles.
- An asynchronous output consumer retaining storage while the scheduler and
  another effect continue. On host this is explicitly a simulated peripheral,
  not a DMA or physical LED test. Storage cannot be reclaimed or overwritten
  until completion, including after replacement.
- A bounded communications-service workload progresses alongside rendering and
  preparation. Use actual local socket I/O if practical; label a synthetic
  event queue as synthetic if that is used. Do not claim Wi-Fi/MQTT coverage.
- Prepare and activate a replacement while the old effect remains usable; retain
  the old buffer until output completion. Reject a failed preparation without
  losing the working instance. Separate load, initialization and activation costs.
- Out-of-bounds access, runaway work, allocation/admission failure, cancellation,
  malformed artifact and cleanup tests. Report unsupported guarantees honestly.

## JSON envelope

Required fields: `schema_version` (1), `backend` (`wamr-aot`, `direct-native`, or
`typed-interpreter`), `status` (`passed`, `failed`, or `unavailable`), `environment`
(object), `workload` (object with `pixels`, `seed`, and `frame_hashes`), `checks`
(object of named booleans), `metrics` (object), and `limitations` (array of strings).
The common check names are `independent_instances`, `resumable_execution`,
`native_buffer_alias`, `native_bulk_operation`, `async_output_retention`,
`service_progress`, `replacement`, `failed_replacement_preserves_old`,
`bounds_rejection`, `stale_view_rejection`, `runaway_bounded`, `admission_failure`,
`malformed_artifact_rejected`, and `cleanup`. Additional checks are welcome.
False or missing common checks must not be reported as an overall pass. These
checks establish the implemented host scenario only; they do not prove MCU
timing, adversarial native-code isolation, or arbitrary-load preemption.
Record native build/compiler information and artifact hashes. Clearly distinguish
tracked component allocations from process/SDK/native-stack memory. Record timing
units. Measured maxima are not proven interruption or deadline bounds.

Each directory should include reproduction instructions and backend-specific tests.
Keep generated binaries, downloaded dependencies and transient results under
`build/component-runtime/`. Do not change firmware defaults or existing work.
The experiment is not required to implement the four source-language frontends;
it establishes an execution target against which those frontends can be evaluated.
