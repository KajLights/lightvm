# Component runtime experiment results

Recorded 14 October 2026 on ARM64 macOS 27.0.1, Python 3.12.13 and Apple Clang
21.0.0. These are working host implementations of three execution candidates.
They do not select the production architecture or establish board capacity.

The subsequent [ESP32-C3 device report](device/REPORT.md) supplies the requested
matched board measurements: actual 200-pixel RMT transfers, USB communications,
RAM-resident replacement, whole-image RAM and service gaps. The host observations
below remain historical evidence; the device report supersedes the proposed
next step and the statement that no MCU native loader has been built.

## Verification

The [saved aggregate](results/host-2026-10-14.json) records a fresh combined run
of all three backends. Each passes the fourteen common checks in the
[contract](CONTRACT.md), plus its additional scenario checks:

| Backend | Integrated scenario checks | Python regression tests |
| --- | ---: | ---: |
| WAMR AOT | 16 | 7 |
| Direct native | 23 | 12 |
| Typed interpreter | 22 | 5 |
| Independent coordinator | — | 14 |

All 38 Python regression tests pass. WAMR and typed integrated scenarios also
pass with AddressSanitizer and UndefinedBehaviorSanitizer enabled; their saved
reports are [WAMR](results/wamr-sanitized-2026-10-14.json) and
[typed](results/typed-sanitized-2026-10-14.json). Typed loader regression tests
also passed under those sanitizers. This does not constitute a security audit.

The coordinator computes its own unsigned-integer reference workload, without
importing a backend's implementation. All three produce the 300-pixel hashes
`28e71914`, `7e73067a` and `f1e1bdc6`. Missing evidence, wrong hashes, failed
checks, process errors and timeouts cannot become a passing comparison. Each
backend uses a genuinely changed replacement artifact and checks its new output.

## What the implementations demonstrate

| Question | WAMR AOT | Direct native | Typed interpreter |
| --- | --- | --- | --- |
| What executes? | Real target-specific WAMR 2.4.5 AOT; interpreter/JIT disabled | Separately linked shared libraries with manifests and a checked ABI | Validated binary package, 19 instructions and a fixed manifest header |
| How does work resume? | Explicit cursor/state in generated-style guest code; 32 pixels per requested quantum | Explicit 40-byte continuation; at most 64 work units per step | PC, registers and return stack retained; 61 instructions per quantum |
| What enforces ownership? | Host views and guest conventions; raw guest stores can bypass sealing | Checks in trusted component code and stable lifetime guards | Every guest buffer access checks the typed view, bounds, generation and write rights |
| How does replacement retain output? | Keeps the entire old instance alive | Keeps the output allocation/guard alive; old effect rotates buffers during preparation | Keeps the output buffer alive independently of the old instance; old effect rotates buffers |
| What work remains synchronous? | Load, instantiate and native operations | Manifest validation, dynamic loading and native operations | File staging, allocation/copy and native operations; instruction validation is sliced |

All three run independent effects, access native RGB storage directly, perform
native bulk operations on the same storage, service actual local socket traffic,
and replace an effect while a simulated output consumer retains its old buffer.
They also reject the tested malformed packages, resource failures, stale views
and invalid accesses, and finish with zero tracked allocations/reservations.

Passing a named check has a specific scope. In particular, native and WAMR
`runaway_bounded` tests exercise loops written with explicit yield points. They
do not prove that an arbitrary native or AOT loop can be interrupted. The typed
interpreter dispatches every accepted instruction through its budget checks;
long host calls remain outside that mechanism.

## Memory and timing observations

These figures describe different accounting boundaries. They must not be used
as whole-device RAM totals or as a fair throughput ranking.

| Backend | Peak tracked payload | Important excluded or retained storage |
| --- | ---: | --- |
| WAMR AOT | 63,236 bytes | Retaining 900 RGB bytes pins a 20,032-byte instance. Includes page-rounded linear memory; excludes executable mappings, stacks and 9,404 bytes of artifact staging. |
| Direct native | 3,976 bytes | Includes state, guards, output queue and four RGB buffers. Excludes the Python resident harness, shared-library mappings, stacks, sockets and allocator overhead. |
| Typed interpreter | 4,992 bytes | Includes packages, continuations and four RGB buffers. Excludes a **32,800-byte input staging stack array**, other stacks, executable text, sockets and allocator overhead. |

All three report zero pixel-data copies on the effect → native transform →
simulated output path. Diagnostic correctness readbacks are separate. The native
and typed scenarios need four simultaneous RGB allocations during replacement;
WAMR uses three and pauses an instance while its own output is retained. These
are different overlap policies, not equivalent memory configurations.

WAMR's declared linear memory is reduced to 6,064 logical bytes for this fixture,
but occupies 16,384 bytes per instance on this host. That host page size is not
an MCU prediction. The experiment also caught that ordinary WAMR malloc counters
missed mmap-backed linear memory; the recorded configuration routes it through
the tracked allocator.

For orientation, the recorded native C-only loop took 1.62 µs per 300-pixel frame
over 2,000 frames; WAMR reported 2.49 µs over 1,000 frames including its scheduler
socket checks; typed resume calls totalled 69.16 µs across six frames, or 11.53 µs
per frame excluding service work. Different dispatch quanta, instrumentation,
sample counts and host boundaries prevent a controlled speed comparison. These
measurements do not answer whether any backend meets an MCU frame deadline.

Native loading took approximately 64–65 ms per library in this run and blocked
the harness. WAMR load/instantiate and typed file staging also remain synchronous.
Incremental initialization alone therefore does not satisfy the architecture's
full live-preparation requirement. Observed maxima are not worst-case bounds;
WAMR's zero-nanosecond activation sample is below clock resolution, not free work.

## Implications for architecture selection

**WAMR removes the need to build an instruction engine, but it does not remove
the component machinery.** This prototype still supplies continuations,
scheduling, manifests, admission, views, output lifetime and replacement. Its
explicit bypass test confirms that an in-range guest store can modify published
storage despite the sealing convention. It demonstrates native access to
WAMR-owned linear storage, not arbitrary externally allocated peripheral buffers.
Those ownership and placement requirements remain gates for this option.

**Direct native is a useful performance and data-layout reference.** The full
tested path includes bounds checks, resumability and retained-buffer ownership
with small tracked payload. However, the reference component is hand-written
trusted code. No compiler yet guarantees those checks, and no MCU module loader
or arbitrary-machine-code containment has been built. Its small payload should
not conceal the host harness and loader costs excluded from the measurement.

**The typed VM is a credible candidate for the next device experiment.** It
enforces continuations and buffer rules at dispatch, represents nested calls and
custom arithmetic, and uses a compact 280-byte initial artifact. It is presently
a narrow u32/RGB8 machine. Numeric semantics, richer data types and language
support could change both its size and speed. Its staging stack must also be
replaced or budgeted before any MCU memory claim is credible.

The next comparison should use one board, one output driver, one scheduler/load
scenario and the same buffer-overlap policy. Measure total RAM, code placement,
physical asynchronous output, service gaps during installation and recovery
under faults. Keep native as the performance reference; test typed execution
against a concrete frame budget; require WAMR to demonstrate consumer-selected
buffer placement and the chosen ownership policy before investing in its speed
optimization. The present evidence justifies those gates, not a final winner.

Python, JavaScript, blocks and Pixelblaze remain possible authoring inputs to the
execution contract. These experiments implement no source-language frontend or
language compatibility claim. Agreeing their numerical, state and buffer semantics
is separate work from choosing the execution engine.

See [reproduction commands](README.md) and backend details:
[WAMR](wamr/README.md), [native](native/README.md), [typed](typed/README.md).
