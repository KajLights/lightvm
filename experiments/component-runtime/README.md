# Component runtime architecture experiments

Three opt-in implementations exercise the component-runtime request without
changing production firmware or the existing WASM ABI:

The follow-up [same-board ESP32-C3 comparison](device/REPORT.md) runs all three
with real 200-pixel RMT output, USB traffic and live replacement. Its
[device harness](device/README.md) uses matched scheduling and memory accounting;
the host experiments below retain their original, narrower scope.

| Directory | Execution experiment |
| --- | --- |
| [`wamr/`](wamr/) | A real WAMR ahead-of-time artifact and AOT-only host runtime |
| [`native/`](native/) | Separately compiled, dynamically loaded native components |
| [`typed/`](typed/) | Serialized bytecode executed by a small typed interpreter |

The [shared contract](CONTRACT.md) specifies numerical semantics, fourteen common
checks, reporting and scope. Each backend owns its implementation and additional
fault tests. The coordinator independently verifies identical RGB frame hashes;
successful exit status or self-reported success alone cannot pass the comparison.

## Run

From the repository root, after the dependency setup below:

```sh
python3 experiments/component-runtime/test_coordinator.py
python3 experiments/component-runtime/run.py
```

Read each backend's README for its compiler/dependency setup. WAMR additionally
needs its pinned runtime and AOT compiler; native and typed experiments use local
compilers. Build artifacts and downloaded tools stay under ignored
`build/component-runtime/`.

For the first WAMR run, use its explicit local bootstrap (Python 3.12+, a WASM
Clang/linker and the supported host prerequisites in its README are required):

```sh
python3 experiments/component-runtime/wamr/run.py --bootstrap \
  --output build/component-runtime/wamr-result.json
```

Run the backend regression suites with:

```sh
python3 -B -m unittest discover -s experiments/component-runtime/wamr -p 'test_*.py' -v
python3 -B -m unittest discover -s experiments/component-runtime/native -p 'test_*.py' -v
python3 -B -m unittest discover -s experiments/component-runtime/typed -p 'test_*.py' -v
```

To run a subset or choose the aggregate report location:

```sh
python3 experiments/component-runtime/run.py --backend native --backend typed \
  --output build/component-runtime/native-typed.json
python3 experiments/component-runtime/run.py --backend wamr \
  --output build/component-runtime/wamr-comparison.json
```

The default aggregate is `build/component-runtime/comparison.json`. Every run
creates a new directory beside it containing individual JSON results and build/run
logs, so stale results cannot silently substitute for a failed execution. The
coordinator checkpoints after each backend. Exit status is 0 for a passing selected
set, 1 for failure, and 2 when a required backend reports unavailable. A subset
report explicitly records that all three backends were not executed.

The default per-backend timeout is 1800 seconds to permit an initial toolchain
build. `--timeout SECONDS` changes it. A timeout terminates that runner's process
group, including compiler children.

## What this comparison establishes

The workload is a custom unsigned-integer recurrence, computed in the component,
writing 300 RGB pixels. It is not a call to a firmware-provided effect. Three
reference frames use exactly the same seeds and arithmetic across implementations.

Each backend also exercises its own resident scheduler, independent instances,
resumable work, native bulk operations, output retention, service progress,
replacement and fault cleanup. In particular, code replacement must not invalidate
an output buffer still held by the simulated peripheral. Failed preparation must
preserve the working component.

## Interpretation limits

- These are **host experiments**, not Pico/ESP32 firmware integrations. Async
  output represents a peripheral retaining storage; it does not validate physical
  DMA, cache maintenance, LED timing or flash-write stalls.
- Local service tests do not establish Wi-Fi, Matter, TLS or MQTT capacity.
- Backend schedulers and instrumentation differ. Report kernel timing separately
  where available; do not rank their total harness runtimes as VM performance.
- Component allocation accounting has backend-specific exclusions. It is not
  process RSS, whole-device peak RAM or proof of MCU component capacity.
- Loading/validation and native calls may be synchronous. Resumable effect work
  does not establish a proven maximum interruption latency for every operation.
- Direct-native packages are trusted code. Neither a manifest nor the test loader
  contains arbitrary machine-code memory faults.
- This work supplies candidate execution targets. It does not implement Python,
  JavaScript, blocks or Pixelblaze frontends, or prove their language compatibility.

See the backend READMEs and the recorded [report](REPORT.md) for the measured
implementation details and unresolved architecture gates.
