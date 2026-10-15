# Minimal typed interpreter experiment

This opt-in host prototype evaluates a purpose-built execution target against
the [shared experiment contract](../CONTRACT.md). It adds no production firmware
dependency and does not select an architecture for LightVM.

## Reproduce

Requirements: Python 3.9 or newer, a C11 compiler (`CC` defaults to `cc`), and
POSIX local sockets. No packages or network downloads are required.

From the repository root:

```sh
python3 experiments/component-runtime/typed/run.py --output build/component-runtime/typed/results.json
python3 -m unittest discover -s experiments/component-runtime/typed -p 'test_*.py' -v
python3 experiments/component-runtime/typed/run.py --sanitize --output build/component-runtime/typed/sanitized-results.json
TYPED_SANITIZE=1 python3 -m unittest discover -s experiments/component-runtime/typed -p 'test_*.py' -v
```

Generated packages, executables and results are under
`build/component-runtime/typed/`. The runner compiles with warnings treated as
errors, captures diagnostics, records compiler/platform information and artifact
SHA-256 hashes, and exits nonzero if a check fails or a dependency is unavailable.
The sanitizer commands require AddressSanitizer and UndefinedBehaviorSanitizer
support in the chosen compiler.

## Execution model

`assembler.py` emits actual serialized bytecode. `vm.c` loads, validates and
executes it. There are 19 operations: halt, constant, move, wrapping u32 add/xor/
multiply/shifts/and, immediate add, unsigned less-than, conditional/unconditional
branch, checked byte load/store, state load/store and call/return. The effect is
ordinary arithmetic and a loop, including a callable PRNG function; there is no
pixel-effect opcode or host callback for each pixel.

The runtime separates numeric registers, view registers, persistent u32 state
and return addresses. A guest cannot manufacture a pointer or a view by writing
an integer register. Every instance has 16 u32 registers, two bounded view slots,
up to eight state words, an eight-entry return stack, a program counter and an
instruction counter. Instances share immutable validated code and have separate
mutable state. There is no GC or per-frame allocation.

Each instruction dispatch is a scheduling safe point. A resume call executes
at most 61 instructions and retains the precise PC, registers and call stack.
The mixed-effects test actually yields inside the PRNG call and verifies complete
frames from both seeds. A job exceeding 100,000 instructions faults, while an
ordinary quantum exhaustion yields and resumes. Cancellation occurs at the next
safe point. Native allocation, memory copying, file staging and bulk transforms
are synchronous and do not acquire this interruption guarantee.

The 32-byte binary header is the experiment's fixed manifest:

| Offset | Field |
| --- | --- |
| 0 | `TVM1` magic |
| 4 | Format/API version, currently 1 |
| 8 | Instruction count, maximum 4,096 |
| 12 | Initialization entry instruction |
| 16 | Frame entry instruction |
| 20 | State word requirement, maximum 8 |
| 24 | Return-stack requirement, 1–8 |
| 28 | Required capability bits, currently exactly 1 (bounded RGB storage) |

All integers are little endian. Each instruction occupies eight bytes: one-byte
opcode, three operand bytes and a u32 immediate. The loader checks exact package
length, version/capabilities, resource limits, all instruction encodings, register
classes, state indexes, shift counts and branch targets. Validation proceeds in
four-instruction slices. Cycles and recursive calls are permitted; dispatch-time
instruction and stack limits contain them. This verifier does not prove program
termination or enforce a high-level source language's semantics.

`effect.tvm` implements the common numerical workload. `replacement.tvm` has a
different initializer that establishes seed `0x11223344`, regardless of the
host-provided r0 value. The harness constructs the same serialized replacement,
validates it, initializes it in slices, activates it and verifies its output.

## Storage, services and replacement

RGB storage is a stable native allocation with owner/reference counts and an
in-flight lock. A VM byte store resolves a typed bounded view directly to that
allocation. The native bulk inversion uses the identical address; two inversions
restore the frame hash. There is no intermediate private pixel array or mandatory
per-pixel C function call across an ABI. The interpreter's own dispatch does
perform bounds and ownership checks on every byte access.

Submitting a frame retains its buffer and rejects subsequent native or guest
writes. A simulated peripheral checks the retained frame on deferred completion
ticks. The old effect rotates to another allocation, computes during preparation
alongside the other effect and socket service, and reaches a frame boundary
before activation. Activation releases the old instance while its earlier frame
remains valid until output completion. Generation handles reject stale views,
including slot reuse; a slot at maximum generation is permanently retired rather
than wrapping.

Peak replacement storage is **four 900-byte RGB buffers**: the retained prior
frame, the old effect's writable buffer, the independent effect's buffer and the
prepared replacement's buffer. Zero pixel copies therefore does not mean one
buffer suffices. Code and instance allocations also coexist during preparation.

Communications use real nonblocking `AF_UNIX` socketpair I/O. Each service tick
attempts to send one byte and consumes at most 16 bytes, checking received data.
This establishes service interleaving, not Wi-Fi, MQTT, USB or commissioning
support. Output is a host simulation, not DMA or physical LEDs.

## Checks and measurements

The C scenario checks the contract's 14 common requirements plus in-flight write
rejection, old-effect progress during preparation, stale guest accesses, generation
reuse/exhaustion, return-stack overflow/underflow and cancellation. Fault injection
covers instance allocation and RGB allocation after instance allocation, with
rollback preserving the active effect and no retained allocation leaks. A valid
infinite-loop artifact yields repeatedly, services socket traffic, and then faults
at the job budget. A byte-store artifact faults on out-of-bounds and offset
overflow. A stale view also faults through VM dispatch.

Python tests additionally run the loader on truncated/oversized packages, invalid
headers, every relevant instruction operand class, invalid jumps, invalid shifts,
unknown opcodes and 64 deterministic malformed instruction streams. These are
regression/adversarial samples, not a security audit or a proof of sandbox safety.

The JSON records three reference hashes, measured resume durations for six effect
frames, resume-call count, load/validation, initialization, activation, largest
observed slices, whole-harness duration, allocation accounting and socket progress.
Timing uses a monotonic clock (`CLOCK_MONOTONIC_RAW` where available); values are
**wall durations around the named operations**, not process CPU time. Resume
durations exclude the socket service and orchestration; whole-harness duration
includes them and the adversarial scenarios. Measured maxima are not hard bounds.

Tracked bytes cover code/package allocations, instance metadata/continuations and
native buffer metadata/storage. They **exclude** process/libc overhead, executable
text, socket buffers and C stacks. In particular, this host harness has a
**32,800-byte input staging array on its stack**, and the copied `Instance` used
to probe write denial also lives on the stack. The allocation figures must not be
presented as total device RAM use. The staging strategy must be replaced or charged
explicitly when assessing an MCU loader. The default 1 MiB admission ceiling is
a host test setting; tests force rejection at the currently used byte count.

## What this establishes

The narrow VM can represent user-defined arithmetic, bounded buffers, persistent
state and nested calls with resumable execution and explicit ownership. Its
storage and continuation costs are inspectable without adopting a dynamic object
heap. It demonstrates the requested host data path, including a functioning old
effect throughout replacement preparation.

It does not establish MCU throughput, flash-resident execution, DMA compatibility,
cache handling, flash-write interference, multicore scaling, priority/deadline
policy, worst-case native-call latency, full communications-stack replacement or
browser compilation. Python, JavaScript, blocks and Pixelblaze frontends remain
future consumers of the execution contract; richer numeric and language semantics
could materially change the VM's required instruction set and memory costs.
