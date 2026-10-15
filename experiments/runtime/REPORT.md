# Focused C3 runtime benchmarks — 14 October 2026

**Flash-resident WASM materially reduces RAM demand, but the tested Toywasm
configuration is too slow for the Python rainbow at 60 FPS.** wasm3 remains much
faster while retaining more interpreter data. Neither configuration runs the full
MQTT service under the existing memory and 2 ms startup limits.

The most useful additional finding is in the guest toolchain: replacing byte-loop
memory routines with standard WASM `memory.copy`/`memory.fill` instructions reduced
completed MQTT initialization samples from roughly 109 ms to 1.8–3.1 ms. This uses
the same MQTT application source and ordinary 64 KiB memory semantics. The first
completed bulk-memory probe still exceeded 2 ms; this is not a service feasibility
pass.

## Controlled comparison

The physical XIAO ESP32-C3 ran at 160 MHz, ESP-IDF 5.5.3, GCC 14.2.0 `-O2`, with
Wi-Fi, HTTP, boot-sequence support and the MQTT experiment enabled. The VM ceiling
remained 131,072 bytes. Every interpreter comparison used the same flash mapping,
dynamic animation upload allocation and demand-allocated HTTP response buffers.
The 32 KiB object staging buffer and 9,216-byte timing buffers remained reserved.

Backends were wasm3 v0.5.0 and Toywasm commit
`964e182c1a1b09694f9f8f87c32fd26ba8b15e34`. The latter has a LightVM compatibility
adapter and deadline hook. These results characterize these integrations and build
options, not every possible implementation of either interpreter.

Primary measurements used 21 timed samples after three warmups. Kernels had three
repetitions (63 samples each). Animation playback had three five-second windows,
each after a one-second warmup. Kernel times and explicit frame render/output times
come from the device clock. Playback frame gaps were buffered and downloaded after
each window. Upload wall times include USB, validation and flash operations and
must not be interpreted as compiler-only timings.

## Animation results

All times below exclude physical output, which is measured separately. Tracked RAM
includes interpreter allocations and charged animation buffers, not total device
RAM. Explicit-frame hashes matched across all 21 samples for each primary backend
and workload.

| Workload | wasm3 render median | Toywasm initial render median | Toywasm release render median | wasm3 / Toywasm release tracked RAM |
|---|---:|---:|---:|---:|
| Legacy integer rainbow, 100 pixels | 0.942 ms | 8.744 ms | 6.976 ms | 12,270 / 2,518 B |
| Browser Python control rainbow, 100 pixels | 4.155 ms | 18.519 ms | 17.782 ms | 20,559 / 8,336 B |
| Browser Python control rainbow, 300 pixels | 10.938 ms | 49.353 ms | 50.188 ms | 22,359 / 10,136 B |

| Workload | wasm3 median FPS | Toywasm release median FPS | wasm3 / Toywasm release worst frame gap |
|---|---:|---:|---:|
| Legacy integer, 100 pixels | 59.94 | 59.97 | 22.32 / 20.11 ms |
| Python control, 100 pixels | 59.92 | 41.06 | 19.70 / 37.25 ms |
| Python control, 300 pixels | 43.70 | 14.90 | 25.62 / 88.98 ms |

The RMT output callback took approximately 3.45 ms at 100 pixels and 9.57 ms at
300 pixels. Thus even wasm3's 300-pixel workload exceeds a 16.667 ms frame interval
when render and synchronous output are combined. A faster VM alone cannot remove
this output time. No visible strip output was verified.

The initial Toywasm build retained internal debug assertions and ESP-IDF's
`-fno-jump-tables` setting. A short jump-table-only trial (seven samples, one
one-second playback window per workload) did not improve the kernels and worsened
the animation measurements. The release variant combined enabled jump tables with
disabled internal assertions. It improved kernel timings but did not make the
Python animation meet 60 FPS. Runtime validation, bounds and cooperative deadline
checks stayed enabled. These compiler-option checks are not an exhaustive tuning
search; jump tables should not be enabled by default based on these results.

## Where native execution might help

The exact same `kernels.c` source was compiled to WASM with Zig 0.13.0/LLVM 18 at
`-O2` and into the firmware with GCC 14.2.0 at `-O2`. Each kernel performs 2,048
iterations and returns a checked result. Floating-point contraction was disabled.
The memory kernel uses a volatile 1 KiB working set; the import kernel calls
`led.pixel_count()`. The native equivalent is an ordinary C function call.

| Kernel | Native C median in wasm3 firmware | wasm3 median | Toywasm release median | wasm3 / native |
|---|---:|---:|---:|---:|
| Integer mixing | 142 µs | 3,019 µs | 54,445 µs | 21.3× |
| Double arithmetic | 4,454 µs | 5,593 µs | 17,871 µs | 1.26× |
| Indexed memory access | 180 µs | 3,768 µs | 61,813 µs | 20.9× |
| Repeated host import | 167 µs | 6,390 µs | 35,798 µs | 38.3× |

**Native C is an unsandboxed reference, not a measured WASM JIT/AOT backend.** It
omits WASM dispatch, memory checks and deadline polling; native and WASM compilers
also differ. The ratios locate useful targets, not promised compiler speedups.
The wasm3 hook checks every loop iteration; Toywasm's configured interrupt check
interval is 32 instructions. Both represent the project's bounded execution
policy, not equal instruction-count instrumentation.

The floating-point result is particularly useful: native execution already spends
most of the time on arithmetic, whereas integer loops, memory operations and
guest/host crossings have substantial additional overhead. Selective compilation
and fewer crossings deserve priority over a general claim that JIT makes every
workload proportionally faster.

## MQTT: loading, initialization and steady steps are distinct

The original 10,733-byte MQTT artifact was unchanged. With the 100-pixel Python
animation running, wasm3 failed all three admissions. Before admission it had
88,644 native free bytes and a largest block of 77,824 bytes. Its tracked peak
reached 33,881 bytes before native allocation failed, and returned to 20,559 bytes
after cleanup. The host comparison also rejected the full artifact under the
128 KiB budget. Flash-backed source alone therefore does not establish feasibility.

Toywasm admitted the original service in all three attempts. Combined tracked
memory was 21,396 bytes immediately after loading, but this is not its execution
footprint: Toywasm backs linear memory incrementally. The compatibility adapter's
host-memory accessor materializes the full ordinary page during initialization.
After a startup attempt the combined tracked allocation was about 82,012 bytes.
The original artifact then faulted on the unchanged 2,000 µs startup deadline.

To measure the source of the timeout, an explicitly separate diagnostic command
stopped animation and service scheduling, then called initialization under a
500,000 µs diagnostic ceiling. The production limit remained 2,000 µs. It measured
three completed initializations and 21 disconnected service steps:

| Guest | Completed initialization samples | Typical disconnected backoff step | First connection-attempt step |
|---|---|---:|---:|
| Original byte-loop libc | 108.589, 108.229, 111.732 ms | 57 µs | 5.239 ms |
| Standard WASM bulk-memory libc | 3.136, 1.796, 1.768 ms | 57 µs | 4.924 ms |

These are sequential diagnostic samples after a failed ordinary startup, not
three independent cold boots. The later samples are warm. The initial connection
step includes native TCP work; cooperative guest checks cannot preempt that work.

The bulk-memory artifact is 12,585 bytes. Its 17 Wasmtime protocol tests passed,
including fragmented I/O, QoS handling and reconnection. Its ordinary C3 startup
still faulted under 2 ms. Consequently **connected-idle, traffic, burst and
reconnection throughput on this C3 remain unmeasured**. The saved Wi-Fi network
was unavailable; no Mac network changes or external broker connections were made.

## Decisions supported by the measurements

1. Retain flash-backed code and allocate buffers according to lifetime. Both
   matched builds had about 121 KiB native free heap while idle, materially more
   room than the earlier profile; this is separate from interpreter selection.
2. Keep wasm3 as the current speed baseline. The tested in-place interpreter saves
   RAM but is not an adequate replacement for the current Python animation load.
3. Evaluate standard bulk-memory emission and batched operations. This experiment
   found a large, reproducible initialization improvement without native MQTT.
4. Separate initialization/preparation policy from per-step execution policy.
   Any change must still measure preparation stalls during live replacement;
   increasing a deadline is not itself a timing solution.
5. Next compare a sandboxed flash AOT backend on the same artifacts, concentrating
   on integer/memory-heavy code and import overhead. These runs have not measured
   AOT code size, compiler scratch RAM or compilation latency.

## Validation, artifacts and restoration

Both matched host backends passed 125 Python tests (four feature-dependent skips)
and all five C contract tests. Toywasm's 22 control tests passed with AddressSanitizer.
Native/WASM kernel return values and all primary animation frame hashes matched.
Each completed run verified tracked VM allocation returned to zero after unload.
An expanded allocation-failure test exposed a wasm3 v0.5.0 cleanup bug; the
build-local copy now retains old function/global array pointers and counts when
reallocation fails. The same regression now passes. SDK dependency and lwIP symbol
collisions in the interrupted Toywasm integration were also fixed.

One release-run configuration upload was rejected before any timed samples. Its
failed checkpoint is retained; a fresh run completed successfully. The short
jump-table trial is labeled separately from the full repeated comparisons.

The original full 4,194,304-byte flash image was restored and verified by digest
before reboot. Device identity, runtime configuration, network configuration,
original features, active procedure and idle playback mode matched the initial
snapshot. Saved store bytes are covered by the full-flash verification. Backups
remain in ignored `build/` because they can contain credentials.

- [Reproduction instructions](README.md)
- [Summary and raw-data directory](results/summary.json)
- [Measured firmware sizes and hashes](results/firmware.json)
- [Restoration verification](results/restoration.json)
