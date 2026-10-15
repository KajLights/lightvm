# WAMR backend for the matched ESP32-C3 device experiment

`../../build_wamr.py` builds two real, distinct WAMR 2.4.5 AOT packages for
RV32IMC/ILP32 using cached tools. `v1.aot` uses the supplied seed; `v2.aot` changes
initialization to XOR that seed with `0x13579bdf`. Both use exactly one 600-byte
RGB buffer, 200 pixels and 32 pixels per resumable call. Initialization clears at
most 64 bytes per call. The wrapper checks output target metadata and records
artifact SHA-256, compiler revision and sizes beneath
`build/component-runtime/device/wamr-artifacts/`.

The component uses the pinned upstream WAMR source at
`build/component-runtime/deps/wamr-2.4.5` (revision
`25bd7eb63e828e4bd242cc9b38d260b4b31c6605`, Apache-2.0 WITH LLVM-exception).
It enables AOT only and disables interpreter, JIT, WASI, pthread guest support,
shared memory, reference types and SIMD. Add the component as an ESP-IDF `wamr`
dependency and compile `../../backend_wamr.c` in the common device harness.
The upstream ESP port requires executable SRAM; the common experiment config
disables ESP32-C3 memory protection for both executable-code backends.
The common execution worker must be created with `pthread_create`, with an
explicit stack size. WAMR's ESP `os_self_thread()` calls `pthread_self()`; an
ordinary FreeRTOS/app_main task is not registered with IDF's pthread layer and
asserts during instance creation. The common harness uses the same pthread
arrangement for all three backends.
The actual ESP32-C3 smoke run passed with the unmodified upstream ESP memory
mapping and relocation port after this pthread integration fix. No speculative
IRAM/DRAM alias patch is included or required by the observed run.

The native pointer returned by `cr_pixels()` aliases WAMR linear memory. The
adapter blocks all mutating guest calls while sealed, and refuses destruction or
unloading while an owner still retains resources. The common harness manages
RMT completion, the complete retained instance, package staging and USB traffic.
This backend does not make a second RGB copy or allocate another output buffer.

Native stack bounds are explicitly registered using the current IDF task's
stack base plus a 2 KiB guard; upstream ESP stack discovery otherwise returns
NULL when FreeRTOS tracing is disabled. The execution contract uses one worker
task. Moving an instance to another worker would require updating that boundary.

The resumable loop is explicitly lowered fixture code; this is not arbitrary
WASM preemption. Sealing is a trusted scheduler contract, not per-buffer memory
protection against malicious AOT. AOT loading and instance creation remain
synchronous. The device harness measures SDK heap and load stalls; no host
timing or memory result is substituted for a device measurement.
