# Direct-native component experiment

This opt-in host prototype implements the shared [experiment contract](../CONTRACT.md).
It is an execution-target experiment, not a firmware change or a source-language
frontend. It requires Python 3.10+ and a C11 compiler supporting `-fstack-usage`
(tested with Apple clang 21 on arm64 macOS). It uses no Python packages.

From the repository root:

```sh
python3 -B experiments/component-runtime/native/run.py --output build/component-runtime/native/results.json
python3 -B -m unittest discover -s experiments/component-runtime/native -p 'test_*.py' -v
```

All generated C objects, shared libraries, compiler stack reports, manifests,
malformed test packages and JSON results are under `build/component-runtime/native/`.
The runner rebuilds them; it exits nonzero for a failed or unavailable experiment.
`CC` can select a compiler. Linux uses `.so`; macOS uses `.dylib`. Other host ABIs
and Windows are not supported by this prototype.

## What executes

`component.c` becomes two **separately linked and dynamically loaded artifacts**.
The Python resident core validates JSON compatibility fields, SHA-256, host target,
capabilities and resource sizes before `dlopen`, then checks the exported ABI
descriptor. Both initial instances share v1 code with separate continuation state
and native RGB allocations. The replacement v2 changes executable behavior: its
initialization XORs the supplied seed with `0x13579bdf`. The test checks the updated
output against an independent Python implementation, rather than only checking a
new filename or host-side seed.

`abi.h` defines version 1 of the component ABI: initialization, incremental
initialization, begin-frame, step and cancel. The continuation retains the current
pixel, PRNG state, frame, phase and fuel counters in 40 bytes on this host. A
`step` call performs at most 64 pixel/loop work units, preserves its state and
returns without retaining a native stack. Nested calls, recursion, dynamic native
allocation and blocking foreign calls are outside the component source profile.
The component functions are audited leaf functions; compiler `.su` reports must
show static frames no larger than the 512-byte policy. These are component frames,
not Python/ctypes/core stack costs, and the policy is not a machine-code verifier.

The bounded buffer view holds a pointer, byte extent, generation and pointer to
stable lifetime metadata. Generated-style code checks the view, write ownership
and pixel bounds, then accesses RGB memory directly inside its native loop. There
is no per-pixel Python or host callback. `core.c` provides native fill/dim operations
over that exact storage. Its native operations cap RGB extents at 300 pixels.

## Combined scenario and rejection tests

- Two effects produce the common numerical workload with independent state;
  quantum 17 visibly advances 17, 34, 51… pixels without restarting.
- Real local datagrams run beside rendering and incremental preparation. The
  producer admits at most 32 outstanding messages; the service receives at most
  two per tick, with excess producer messages dropped rather than queued forever.
- A complete frame is published to a simulated asynchronous peripheral. Its
  native allocation becomes readonly and remains retained across replacement.
  Partial and cancelled frames cannot be published.
- One additional 900-byte RGB allocation lets the **old** effect render another
  frame during replacement preparation while its earlier output remains held.
  The second effect and communications also progress. All this storage is admitted
  before handoff. Output completion releases the retired storage.
- Failed incremental initialization releases only the candidate's reservations;
  the old effect subsequently renders the expected next frame. The successful
  handoff installs v2 and retires the old instance at a completed-frame boundary.
- Short and malformed RGB views, invalid pixel indices, stale leases and writes
  during retention are rejected before modifying storage. After reclamation, a
  stale view is rejected by guard metadata **before dereferencing freed RGB data**.
- A deliberately nonterminating source loop still consumes no more than 64 work
  units per step. After 512 units the resident host cancels it while other work
  progresses. This establishes cooperative continuation checks, not preemption
  of an arbitrary native loop.
- Partial admission failure and injected allocation failure return all reserved
  credits. Invalid JSON, ABI versions, resource claims, capabilities, paths,
  artifact digests and invalid native executables are rejected. Tests additionally
  cover output cancellation, live-module unload rejection and cleanup.

Guard metadata must outlive every stale view. The harness discards stale views
before explicitly disposing guards; it does not claim automatic arbitrary-pointer
lifetime tracking. State and native payload references are discarded on cleanup.

## Measurements and interpretation

The JSON records compiler commands, artifact sizes and SHA-256 digests; separate
validation, `dlopen`, initialization and activation/teardown times; continuation
checks; mixed-scheduler timings; socket traffic; copies and retained storage; and
tracked native payload reservations. Diagnostic Python readbacks deliberately
copy bytes for correctness checks, but no RGB copy exists on the
render → native transform → simulated output path.

The C-only benchmark runs 2,000 complete frames through the same begin/step ABI,
checks and fuel boundaries without Python dispatch. It reports both elapsed and
process CPU time, including C call and clock overhead. The mixed scheduler and
per-call ctypes measurements include Python overhead and are labelled separately.
Frame gaps include correctness readbacks and are host observations, not MCU timing
predictions. There is no configured physical frame deadline or deadline-miss claim.

The 4,096-byte admission limit counts native continuation state, RGB allocations,
guard metadata and a fixed native output-record array. The scenario peaks at
3,976 bytes with two effects, replacement, a rotated output buffer and eight output
slots. File sizes are reported separately; shared-library mapped RAM/ROM is not
measured. Python objects/interpreter, libc allocator overhead, resident-core/native
stacks, OS sockets, dynamic-loader mappings and SDK memory are **outside this
budget**. Largest requested allocation is recorded; largest free contiguous block
and total process peak RAM are not measured. Zero remaining reservations is a
payload-cleanup result, not a process-RSS claim.

## Trust and unsupported guarantees

This implementation trusts a known, audited native producer. The manifest and
hash are compatibility/corruption checks, not a signature, authentication protocol
or binary verifier. Loading hostile native code can run constructors, bypass every
buffer check, corrupt memory and block forever. `ctypes` and `dlopen` provide no
component fault isolation. A production compiler must enforce the restricted source
profile and inserted checks, or the design needs a stronger isolation boundary.

Validation and dynamic loading remain synchronous and can stall services. They are
timed separately; only initialization is incrementally scheduled. No browser
compiler, relocatable MCU loader, flash persistence, interrupted-write recovery,
DMA, physical output, Wi-Fi/MQTT, full communication-stack replacement, multicore
ownership or board capacity has been demonstrated. Host maxima do not prove
bounded interruption latency or hard real-time scheduling.
