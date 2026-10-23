# Multicore rendering and resource budgets

Pico, Pico W and Pico 2 W use both CPU cores by default. Core 0 owns application
commands, allocation, storage, USB, networking and physical output drivers. It
also renders. Core 1 executes bounded rendering jobs. PIO and DMA transmit LED
data independently of the CPUs.

There is no default refresh cap. A renderer advances when its input buffers and
an output queue slot are available. The output driver decides when transmission
has completed, including the LED reset interval. Each stage can hold one complete
waiting frame. Backpressure protects that stage's buffers; it does not stop
independent child compositions or unfinished layer renders. Animation time uses
elapsed time, including time spent waiting for output.

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

Once a preset recall reaches B, its hidden A scene stops autonomous rendering.
The instance remains available: taking over with manual A/B controls resumes
both slots without resetting their generations or state. A completed composition
may finish an already-started slow layer while its front is pinned, but does not
start more unchanged fast-layer frames until it can publish. A reusable layer
with a changed input can update its independent committed buffer while the
completed composite waits. Composition slices still pin
the committed layer contributions they are reading.

Execution epochs have a 4 ms admission target. Collection stops at 3.5 ms,
leaving time for composition and presentation. Ready composites publish before
collection; a postponed dirty composite gets its next bounded chunk first.
Output, layer and A/B cursors rotate across epochs;
an instance receives at most one scheduled slice per epoch. A native operation,
final join or output packing can overrun the target. Measure the whole epoch and
foreground service gap; the target is not a proven maximum command latency.
No job survives the return to command processing. This supplies the lifetime
fence for reset, replacement, configuration changes, storage relocation and
destruction. Checkpoints are deferred inside an epoch and never run on core 1.
Layer traversal resumes after the last admitted VM. A complete round also
rotates its first VM to exchange core assignments. During an epoch only the
collector admits VM jobs: publication cannot start a serial fallback render
that bypasses this cursor. A valid cached layer consumes no rendering slice.
This replaces the quadratic age/cost search measured on the Pico W. A bounded
transition chunk also runs before layer admission, preventing busy A/B layers
from consuming every epoch before either completed child can publish. Native
instructions check the slice deadline on admission as well as between chunks;
cheap tail instructions may finish and commit a completed frame.

Pico polls USB and digital inputs both after network work and after engine
work. Each USB poll drains at most 256 bytes in one SDK read. Protocol replies
are assembled into whole frames; the CDC receive/transmit FIFOs are 256/1,024
bytes, with 64-byte endpoint buffers so exact packet-sized commands complete
without requiring a host zero-length packet. Progress JSON is batched into
512-byte protocol payloads. Frequent status integers use bounded decimal
formatting and protocol CRC uses a 64-byte read-only nibble table.

`foreground_*` retains the interval between full `app_tick` starts. The separate
`ingress_*` counters measure actual USB/digital-input service opportunities;
STATUS exposes cumulative `fg10` and `io10` counts over 10 ms. The loop interval
can exceed 10 ms even when the additional service point keeps ingress responsive.
Neither counter is an optical measurement or a bound on SDK network callbacks.

Immutable register initial values use four charged bytes per register (at most
512 bytes per VM), eliminating repeated flash reads for constant operands.
Pico W keeps the measured image/map kernels and their small channel/HSV helpers
in SRAM to reduce shared XIP cache contention. The strict RAM report includes
this code; it is not an allocation outside the reported budget. Digital input
acquisition borrows the immutable descriptors owned by HardwareConfig instead
of keeping four duplicate descriptors; apply rebinds every retained adapter
before the old hardware configuration is freed.

[Scene preparation](scene-preparation.md) now uses resumable controller-owned
operations. Parsing, verification, resource initialization, first render,
composition, baseline capture and persistence unwind between bounded units.
Old playback continues, and clients can query/cancel after reconnect. Preparing
one candidate globally limits peak memory. Native operations and flash units
still require measured hardware service bounds.

After preparation, verified straight-line render programs can reuse their
committed contribution while effective input values are unchanged. The device
proves eligibility from bytecode. It rejects clocks, RNG, persistent writes,
sprite lifecycle work, feedback, branches/maps and unsupported effects. Private
colour styles are allowed only when fully reconstructed before use. There is no
extra pixel cache. The virtual clock continues, input ramps and source expiry are
polled, and manual `FRAME` always executes. A sleeping valid background is useful
saved work, not a stalled layer.

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
Native draw/map operations use smaller chunks in that case, so a large serial
operation does not monopolize a paired VM slice. Solid fills pack their colour
once, and pointwise colour transforms reuse the exact result for adjacent equal
pixels within each worker's chunk.
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
| `LIGHTVM_RENDER_SLICE_US` | Target yield interval at VM operation boundaries; Pico W defaults to 2000 µs, other profiles to 500 µs |
| `LIGHTVM_RENDER_EPOCH_US` | Coordinator admission target, default 4000 µs; valid range 100–100000 µs |
| `LIGHTVM_INVOCATION_LIMIT` | Weighted VM work limit per initialization/render; default 10,000,000; see [accounting semantics](bytecode-emitter.md#prepared-pixel-kernels) |

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

MQTT-enabled Pico builds emit `ha-stack-report.json` for the original callback
regression. Every Pico build also runs the strict `stack-paths.json` gate, using
GCC frames, linked machine calls, audited callback/recursion contracts, assembly
bounds and interrupt allowances. Unknown edges, source fingerprint changes or
insufficient margin fail the build. Both configured W profiles pass the software
gate. External ROM allowances and stress watermarks still require physical
validation; this is not a physical stack certificate. See the
[accounting method and results](design/pico-stack-accounting.md). Core-0 watermark
initialization marks only unused stack below the live frame and excludes the guard.

HA light/key adapters produce owned typed commands and return from parsing before
execution. Management requests share one heap parser. Program admission parses
its manifest once for both settings and inputs, charging parser/document scratch
to the VM budget and freeing it before verification checkpoints or VM execution.
That temporary peak may reject a replacement that cannot coexist with its old
scene; the old scene remains usable.

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
assignments/time, `worker_stack_peak_bytes` and `main_stack_peak_bytes`. Pico samples stack high water
using a canary scan on the worker when requested; this is an observed watermark,
not a proof that every possible workload fits. The host reports zero for the
worker watermark and null for the unavailable main watermark. Output `frames`
counts completed presentations. Additional counters report job maximum duration,
paired-job imbalance, coordinator join wait, epoch total/maximum/overruns and the
maximum interval between top-level `app_tick` calls. That interval includes long
commands and storage, and is only a proxy for ingress service opportunities.

Composition status includes a server sample time and each layer's render count,
distinct presented contribution count, displayed frame, age and completion-to-first-
presentation delay. Stamps follow the actual sampled buffers through composition
and transitions; a newly finished layer cannot relabel an older in-flight image.
These are software presentation provenance, not optical measurements or proof
that a layer was unoccluded. Zero-opacity layers and unused endpoint scenes are
excluded; opaque overlays and arbitrary custom transitions are not analysed for
visibility. `dependency_current` describes the reusable **committed contribution**,
which may still be waiting to appear on the output.

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

`tools/replay_performance.py --process build/host/lightvm --output replay.json`
performs exact fixed-step comparisons using preserved original fixtures, fixed
populations and 300 seeded animated frames with brightness changes. Dispatch
reductions do not predict the same percentage increase in Pico throughput.
`benchmark_multicore.py` defaults to seven seconds of warm-up and five 30-second
samples, recreates scenes between trials and reverses case order on alternating
rounds. It records source/bytecode hashes, settings, raw status latencies, firmware
identity (when supplied), clocks, compiler and both stack watermarks. Use
`--network-regime` to record actual traffic conditions and `--firmware` only for
the image verified as flashed. Use `--layer-samples` in a separate diagnostic run
to retain contribution snapshots; measure that observer's overhead separately.
The time-indexed sprite fixture supports autonomous comparisons; probabilistic
stars require fixed-step replay for exact equivalence. The remaining board gates
are tracked in the [performance plan](design/pico-performance-plan.md).

`foreground_over_10ms` counts foreground gaps above 10 ms. Difference boundary
snapshots over a steady interval without flash/setup to evaluate that interval;
the cumulative maximum alone includes earlier storage stalls. Command/report
serialization inside the interval still counts and must not be subtracted.
Pico CDC replies combine a protocol header and payload into one bounded write,
using a 528-byte stack buffer included in the linked stack-path audit.
