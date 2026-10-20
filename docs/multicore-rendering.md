# Multicore rendering and resource budgets

Pico, Pico W and Pico 2 W use both CPU cores by default. Core 0 owns application
commands, allocation, storage, USB, networking and physical output drivers. It
also renders. Core 1 executes bounded rendering jobs. PIO and DMA transmit LED
data independently of the CPUs.

There is no default refresh cap. A renderer advances when its input buffers and
an output queue slot are available. The output driver decides when transmission
has completed, including the LED reset interval. A completed frame can wait
behind one transmission; rendering then stops until that frame can be used.
Animation time uses elapsed time, including time spent waiting for output.

For WS2812 RGB, wire time is approximately 30 microseconds per pixel plus the
driver's 330-microsecond drain/reset guard. A 200-pixel chain has a theoretical
ceiling near 158 FPS; a 1000-pixel chain is near 33 FPS. Rendering, composition,
packing, polling and communication can lower these rates. More CPU throughput
cannot overcome a single chain's wire rate. Separate outputs transmit in parallel.

## Execution and ownership

`app.c` collects ready VM slices across outputs, composition layers and both A/B
slots. `vm.c` prepares pairs: input/time capture happens first on core 0, one
slice runs on core 1, and core 0 runs the other. The coordinator joins both
before publishing contributions or continuing application work. Odd jobs run
locally and can use the worker for eligible pixel ranges.

These execution epochs are deliberately synchronous and bounded. No job survives
the return to command processing. This supplies the lifetime fence for reset,
replacement, configuration changes, storage relocation and destruction, without
an asynchronous command protocol or a second set of application state. Existing
cooperative checkpoints still service unrelated work during long management
operations. Checkpoints are deferred inside an epoch, and never run on core 1.

The same executor also divides:

- Compiler-recognized pure maps targeting transient frame pixels.
- Native gradients, transforms and drawing into transient frames, when source
  images are resident in RAM.
- Composition and transition destination spans, retaining layer order per pixel.
- Brightness scaling and Pico WS2812 wire packing.

VM registers, instruction caches, RNG and stateful sprite operations remain
exclusive to one VM slice. Each map task has private evaluation scratch. Only
the final logical iteration publishes map registers. Persistent resource writes
remain ordered, preserving their partial-write behavior on faults. Failed
transient output is never committed.

Kernels execute serially when too small or when both CPUs already have work.
Workers never wait for child jobs. This avoids nested dispatch deadlocks and
keeps all allocation outside parallel execution.

The Pico executor uses SDK queues for job/completion publication. The hardware
FIFO remains reserved for SDK launch and flash lockout. Both ordinary storage
and the optional boot journal use `flash_safe_execute`; core 1 initializes the
SDK lockout handler before announcing readiness. Flash operations pause rendering.

## Resource profiles

All sizes below are compile-time defaults. `AUTO` selects the board/feature
profile; explicit CMake values override it without copying a board header.

| Resource | Pico W | Pico 2 W |
|---|---:|---:|
| VM heap, Wi-Fi + HTTP | 64 KiB | 192 KiB |
| VM heap, Wi-Fi without HTTP | 96 KiB | 256 KiB |
| VM heap, USB only | 128 KiB | 256 KiB |
| Instruction cache per VM | 256 B | 1024 B |
| HTTP/WebSocket connections | 2 with Wi-Fi | 4 |
| Core 0 / core 1 stacks | 16 / 8 KiB | 16 / 8 KiB |
| Native operating headroom | 16 KiB | 16 KiB |
| Maximum program / pixels per output | 32 KiB / 1024 | 32 KiB / 1024 |

Pico without radio uses the portable 128 KiB VM profile. ESP32 currently uses
the serial executor; this change does not introduce an ESP32 worker task.

| CMake setting | Purpose |
|---|---|
| `LIGHTVM_VM_HEAP_BYTES` | Aggregate admission ceiling for VM state, frames and resident resources |
| `LIGHTVM_MODULE_BYTES` | Maximum compiled program size; also affects HTTP response storage |
| `LIGHTVM_PIXEL_CAPACITY` | Per-output pixel ceiling and worst-case driver reservation |
| `LIGHTVM_BYTECODE_CACHE_BYTES` | Per-instance instruction cache; power of two, 64–16384 bytes |
| `LIGHTVM_HTTP_CLIENTS` | Concurrent HTTP/WebSocket connection budget, 1–8 |
| `LIGHTVM_NATIVE_HEADROOM_BYTES` | Reservation for native dynamic allocations |
| `LIGHTVM_WORKER_STACK_BYTES` | Pico core-1 stack, 4096–32768 bytes in multiples of 32; default 8192 |
| `LIGHTVM_RENDER_WORKERS` | 1 or 2; default 2 where a concurrent executor exists |
| `LIGHTVM_MAX_REFRESH_HZ` | 0 for readiness-driven output; optional 1–1000 Hz cap |
| `LIGHTVM_RENDER_SLICE_US` | Target yield interval at VM operation boundaries; default 500 µs |
| `LIGHTVM_INVOCATION_LIMIT` | Total dispatch limit per initialization/render; default 10,000,000 |

For example, append these options to the normal Pico 2 W configuration command:

```sh
-DLIGHTVM_VM_HEAP_BYTES=262144 \
-DLIGHTVM_PIXEL_CAPACITY=512 \
-DLIGHTVM_HTTP_CLIENTS=2 \
-DLIGHTVM_BYTECODE_CACHE_BYTES=2048 \
-DLIGHTVM_MAX_REFRESH_HZ=0
```

The linker excludes both stacks from the native heap. Every Pico build reads
the compiled ELF geometry and resource settings, writes `ram-report.json`, and
rejects reservations that exceed either total SRAM or the contiguous heap.
Instruction caches are charged within the VM ceiling, not reserved twice.
Headroom is a reservation, not an enforced malloc partition; stack and native
allocation peaks still need workload testing. Changing storage size remains a
separate flash-layout choice documented in the board guide.

## Pico 2 W math and measurement

The Arm M33 build uses hardware single-precision floating point. It also wraps
the five basic double arithmetic operations with the pinned SDK's RP2350 DCP
implementation. Compiler conversions and libm remain in use. The build option
`LIGHTVM_PICO2_DCP=OFF` permits controlled comparisons. Global fast-math and
floating-point contraction remain disabled. The ELF gate verifies that enabled
DCP arithmetic wrappers survived linking; `describe.pico2_dcp_enabled` reports
the compiled choice.

`describe` reports the worker count, refresh cap, slice/invocation limits,
instruction-cache size, connection budget, native headroom, cumulative worker
assignments/time, and `worker_stack_peak_bytes`. Pico samples stack high water
using a canary scan on the worker when requested; this is an observed watermark,
not a proof that every possible workload fits. The host reports zero stack
watermark. Output `frames` counts completed presentations.

Use `tools/benchmark_controller.py` for comparable single-animation timing,
including `--board pico2_w`. Compare one/two-worker builds with identical
features, memory limits, bytecode, strip geometry, clocks and seeds. Distinguish
render time, completed output FPS and control latency. A wire-limited workload
can show little FPS change while leaving substantially more time for layers,
sprites and communication.

`test_render_executor` runs real host threads and compares complete VM state and
frames with serial execution, including nested kernels and repeated lifecycle
changes. ThreadSanitizer covers that test in CI. Platform contracts cover borrowed
DMA buffers, bounded render-ahead, failures, composition pinning and refresh
pacing. Hardware results, comparison methodology and remaining physical checks
are recorded in [multicore validation](multicore-validation.md). Reproduce the
layer/transition/output cases with `tools/benchmark_multicore.py`, and compare
transform/math frame hashes with `tools/check_render_math.py`. Both tools restore
the active hardware configuration and remove their temporary named programs.
Visible LED acceptance requires strips.
