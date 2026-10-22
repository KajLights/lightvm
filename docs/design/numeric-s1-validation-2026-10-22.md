# S1 fixed-point runtime integration

22 October 2026. **S1 complete: scalar/runtime correctness and available build
gates pass. Physical performance validation remains S6 work.**

The work is based on validated Pico repair `a9c8611`, with the unchanged numeric
foundation rebased as `0c4490e`. The repair's archived evidence is preserved.
The [numeric contract](../numeric-types.md) and
[encoding reference](../bytecode-emitter.md) define the implemented behaviour;
the [machine-readable evidence](../benchmarks/numeric-s1-validation-2026-10-22.json)
records source, test-log, linked-image and stack-callgraph hashes.

## Delivered contract

The shared core now links exact Q16.16 arithmetic, explicit numeric/raw conversions,
comparisons/min/max/square root, and i32 bitwise/checked shifts. Fixed register type
7 and fixed-array kind 20 have independent loader checks. Resources retain bounded
preparation, four-byte elements, checked access, resumable fill/copy, permissions
and generation ownership. Declaration expressions and scalar instructions call
the same numeric evaluator. New operations use application feature floor 1.2.0;
the loader rejects a lower claimed floor, including in generic typed callbacks.

Python authoring retains each fixed literal's exact source token. Folding agrees
with the independent oracle, and leaves faulting operations in place so lazy
branches and earlier writes keep their order. Existing f32/i32 behaviour is
preserved. Direct fixed settings and live-input wire declarations are rejected.
Compiler recognition and runtime validation do not admit fixed values or the new
instructions into map bodies; S2 owns that extension.

No scheduling, worker ownership, service-point, constant-cache or SRAM-placement
policy changed. No alternate numeric execution mode or legacy decoder was added.

## Host verification

Host: arm64 macOS, Apple Clang 21.0.0, Python 3.12.13 and CMake 3.31.6.
HTTP, WebSocket, MQTT and boot-sequence support were enabled. Wi-Fi was disabled,
matching the validated repair's host configuration. Tests requiring local network
listeners ran outside the filesystem sandbox's network restriction.

| Check | Result |
| --- | --- |
| Normal native CTest suite | 17/17 pass |
| ASan/UBSan native CTest suite | 17/17 pass |
| Python/compiler/frontend/receiver regression discovery | 332 run, no failures, three skips |
| Pico build/accounting Python tests with bundled CMake on PATH | 20/20 pass, closing the CMake-related skip above |
| Numeric runtime/compiler suite, included in both CTest runs | 17/17 pass |

The remaining two skipped classes require host Wi-Fi setup/power-recovery support.
Their Wi-Fi-enabled firmware builds pass below; those host service behaviours were
not revalidated by the final Wi-Fi-disabled run. Initial runs inside the network
sandbox failed to bind listeners; a Wi-Fi-enabled host attempt also failed recovery
listener/restart cases. Those attempts are not counted as passing evidence.

ASan/UBSan used `-O1 -g -fsanitize=address,undefined
-fno-sanitize-recover=all -fno-omit-frame-pointer` and
`ASAN_OPTIONS=detect_leaks=0`; no LeakSanitizer claim is made. Native allocation
accounting independently checks that teardown returns VM heap usage to zero.

The independent rational/integer oracle covers 93 reviewed raw operation vectors,
15 exact literal cases, seeded word pairs, conversion and square-root boundaries,
all i32-to-fixed boundary values, and four host rounding modes. The entire scalar
corpus runs through manually encoded C fixtures that import no Python compiler.
Python emission is checked with optimization on/off, including exact literal tails
beyond host-float precision. An additional 1,024 random word pairs exercise every
new compile-time operation against the independent oracle.

Native lifecycle tests fail each allocation in load/preparation, cancel at every
incremental return, resume with quota one, reject stale generations and deny all
allocations during invocation. Two 67-element arrays have exactly 536 payload
bytes. Tests cover malformed types, permissions, opcodes, reserved fields,
resource identities/flags, declaration cycles and lowered feature floors. Actual
receiver tests render fixed state across frames and reject invalid replacement
packages while retaining the live generation and state.

Reproduce the host checks after configuring a host build with the services above:

```sh
cmake --build build/host
ctest --test-dir build/host --output-on-failure
LIGHTVM_RECEIVER=build/host/lightvm \
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
  python3 -B -m unittest discover -s tests -p 'test_*.py' -v
```

## Pico build, SRAM and stack gates

All three Release builds passed compilation, linking, storage-layout checks,
static RAM reservation checks and strict whole-path stack accounting. They used
Pico SDK 2.3.1 and Arm GNU Toolchain 13.2.rel1, two render workers and 8,192-byte
worker stacks. Pico W and Pico 2 W enabled Wi-Fi, HTTP, WebSocket, MQTT and boot
sequence; minimal Pico disabled network services and retained boot sequence.

| Profile | Static SRAM | Remaining after all reservations | Main path / reservation | Worker path / reservation |
| --- | ---: | ---: | ---: | ---: |
| Pico W | 132,976 B | 400 B | 12,048 / 16,384 B | 5,384 / 8,192 B |
| Pico 2 W | 136,076 B | 124,020 B | 12,168 / 16,384 B | 5,156 / 8,192 B |
| Minimal Pico | 37,336 B | 26,152 B | 12,044 / 16,384 B | 5,372 / 8,192 B |

Path totals include interrupt allowances. All exceed the required remaining
margins of 2,048 bytes on the main stack and 1,024 bytes on the worker stack.
Pico W static RAM is unchanged from the repair baseline, retaining its 64 KiB VM
heap, 16/8 KiB stacks, output-driver reservation and 16 KiB operating reserve.
The 400 bytes are after those reservations, not total free operating memory.

The added numeric calls are ordinary direct calls in the audited graph. Existing
callback/assembly contract fingerprints remain valid; they were not regenerated
to conceal a mismatch. The linked reports include software divide support and
fixed square root. On Pico W, the square-root function's own frame is 56 bytes and
the shared numeric evaluator's frame is 32 bytes. Those frame sizes do not replace
the whole-path totals above.

These are linked-image software bounds with the existing external ROM allowances,
not physical stack certificates. No device was flashed or measured in S1. There
is no claim about numeric throughput, optical timing or physical stack watermarks.
ESP32 firmware was not built in this validation; the shared CMake integration
includes the numeric module, but an ESP32 build and target measurements remain
required for that deployment. Broader bulk execution (S2/S3), expanded f32 math
(S4), full JavaScript/Blocks and external-emitter parity (S5), and the hardware
performance matrix (S6) remain open stages of the core plan.
