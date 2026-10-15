# Same-board component runtime measurements

Measured 14 October 2026 on one Seeed XIAO ESP32-C3 at 160 MHz, device
`esp32-d4f98d0449c0`, using ESP-IDF 5.5.3. All three candidates sustained
approximately **60 frames/second** while driving the RMT peripheral for 200
WS2812 pixels on D1/GPIO3, servicing real USB traffic and replacing uploaded
components. Each candidate completed three repetitions and nine replacements.

**The typed interpreter is viable for this workload. WAMR buys much faster
execution at a substantial RAM and replacement-stall cost. Direct native achieves
similar speed to WAMR with much less RAM, but depends on a trusted compiler and
component contract.** These results support a conditional architecture choice;
they do not settle the richer language semantics or communications requirements.

The [aggregate](../results/device-c3-2026-10-14/comparison.json),
[raw results and build identities](../results/device-c3-2026-10-14/), and
[reproduction instructions](README.md) accompany this report. No host timing or
allocation count is substituted for a board measurement.

## Main comparison

RAM includes static instruction/data RAM and SDK-accounted internal heap usage,
including uploaded code, staging, stacks, USB, drivers, instances and retained
output. KiB means 1,024 bytes. The reported conservative SDK peak and directly
sampled peak agree in these runs.

| Metric | WAMR AOT | Direct native | Typed interpreter |
| --- | ---: | ---: | ---: |
| Peak accounted whole-image RAM | **163.0 KiB** | **119.1 KiB** | **117.1 KiB** |
| Baseline with two effects | 139.3 KiB | 115.9 KiB | 115.4 KiB |
| After replacement | 147.7 KiB | 117.2 KiB | 115.8 KiB |
| Median render-call elapsed time, one effect | **80 µs** | **87 µs** | **1,808 µs** |
| p95 / maximum render-call elapsed time | 85 / 122 µs | 93 / 100 µs | 1,822 / 1,909 µs |
| Output rate, range across three repetitions | 60.000–60.004 fps | 59.995–60.001 fps | 59.999–60.005 fps |
| p95 / maximum output completion gap | 17.615 / 17.776 ms | 17.625 / 17.764 ms | 17.581 / 18.077 ms |
| Output gaps greater than 25 ms | **0** | **0** | **0** |
| Maximum service gap during baseline | 1.144 ms | 1.138 ms | 1.143 ms |
| Maximum service gap during replacement | **4.856 ms** | **1.565 ms** | **1.168 ms** |
| Loader elapsed time, median / maximum | 2,296 / 2,339 µs | 447 / 455 µs | 41 / 50 µs |
| Activation elapsed time, maximum | 19 µs | 18 µs | 22 µs |
| USB round-trip during replacement, p95 / maximum | 1.248 / 2.209 ms | 1.323 / 1.909 ms | 1.312 / 1.611 ms |
| Completed output transfers recorded | 1,941 | 1,931 | 1,930 |
| Pending-transfer handoffs verified | 9 / 9 | 9 / 9 | 9 / 9 |
| Lost USB replies / correctness errors | 0 / 0 | 0 / 0 | 0 / 0 |

WAMR used **45.9 KiB more peak RAM than typed**, while its median render calls
were about **22.6 times faster**. Native used only **2.1 KiB more than typed** and
rendered about **20.8 times faster**. The 7 µs difference between native and WAMR
does not justify calling either inherently faster: their compilers, generated
code and access checks differ. They occupy the same practical performance class
in this fixture.

The service-gap measurement is the interval between firmware scheduler visits to
USB service. It captures loading, instance creation and intervening work even
when no echo request arrives during a stall. USB traffic was 50 requests/second,
so host round-trip maxima can be lower than the largest firmware service gap.
The 6,120 acknowledged echoes include warmup. No network reconnect or protocol
session restart is represented by this metric.

All output-gap distributions have roughly 1 ms of scheduler jitter. Zero gaps
above 25 ms means no observed gap at that diagnostic threshold; it does **not**
mean every 16.667 ms deadline was met or establish a hard real-time bound.

## What ran on the board

The common resident firmware runs two independent effects, each with one
600-byte RGB buffer. Each computes the same unsigned 32-bit recurrence with
independent seeds. One effect supplies the physical output; the other continues
rendering concurrently. A native operation converts RGB to GRB and applies
brightness 16/255 in the same allocation. The RMT driver reads that allocation
asynchronously; the owning instance and package remain alive until its actual
completion callback. ESP32-C3 uses RMT refills here, not DMA.

The three execution paths are real:

| Candidate | Uploaded executable | Execution and continuation |
| --- | --- | --- |
| WAMR | WAMR 2.4.5 RV32IMC/ILP32 AOT, 2,072 / 2,080 bytes | AOT-only upstream runtime; explicit 32-pixel continuation |
| Native | Flat target-specific RV32IMC/ILP32 package, 632 / 644 bytes including manifest | RAM-loaded executable code with checked ABI, bounds/ownership checks and 32-pixel continuation |
| Typed | Binary typed package, 280 / 320 bytes | The host experiment's 19-opcode VM; 61 instructions per resume, four instructions per validation slice |

Both versions are uploaded over USB. Version 2 changes initialization by XORing
the seed with `0x13579bdf`. This is changed executable content, not a resident
firmware switch between two built-in effects. Firmware checks every completed
effect against an independent arithmetic oracle; the host independently checks
every recorded output frame. The held, transformed buffer is also hashed before
submission and after completion to detect premature mutation or reuse.

After three seconds of warmup, each repetition measures four seconds of baseline,
rejects a deliberately corrupted upload, performs three successful replacements
with 1.5 seconds of continued execution after each, and measures two more seconds.
Loading is deliberately triggered during an outstanding RMT transfer. Preparation
runs alongside the old effects; activation also occurs during an outstanding old
transfer, retaining the old instance until completion. All **27** handoffs verified
this overlap; all **nine** corrupt uploads were rejected without breaking output.

Median RMT submission-to-callback durations were 6.328, 6.324 and 6.322 ms for
WAMR, native and typed. This measures actual peripheral execution and interrupt
completion. A strip was not needed for the requested 200-pixel output workload;
the experiment does not verify optical output or electrical waveform quality.

## Memory and interruption accounting

| Additional accounting | WAMR AOT | Direct native | Typed interpreter |
| --- | ---: | ---: | ---: |
| Static RAM, bytes | 91,246 | 88,474 | 88,642 |
| Resident RAM before component upload, bytes | 119,850 | 115,894 | 116,062 |
| Peak static + allocated internal heap, bytes | 166,902 | 121,994 | 119,886 |
| Smallest largest data-capable block at checkpoints | 114,688 | 139,264 | 139,264 |
| Worker stack minimum spare space, bytes | 9,544 | 9,536 | 9,536 |
| Application firmware binary, bytes | 251,504 | 178,064 | 179,792 |
| Largest observed individual render step | 24 µs | 28 µs | 76 µs |

The common trace array alone consumes **24,576 static bytes**. It is included,
along with the common 12 KiB worker stack and 4 KiB USB queues in each direction.
These are instrumented experimental image totals, not minimum possible production
footprints. Stack spare space is already inside the charged allocation. Removing
trace storage or tuning stacks would reduce all candidates; that optimization
was deliberately kept out of the comparison.

Internal heap accounting includes executable allocations without adding them a
second time. Total RAM is linker-accounted static RAM plus internal heap total
minus its minimum free space. The SDK's sum of regional low-watermarks can be
conservative if different regions reach their minima at different times. The
1 ms samples also record simultaneous use, but may miss short transients.
Allocator metadata, reserved address-space gaps and ROM reservations are not
separately attributed by this accounting. Largest-block observations occur at
allocation/lifecycle checkpoints, not continuously.

The final state can use more RAM than baseline: the unchanged second effect keeps
the original module alive while the selected effect uses its replacement module.
Replacement peaks include staging and old/new lifetimes together. WAMR keeps
whole instances alive while their linear-memory output is retained; the chosen
single-buffer policy is identical across candidates.

Render timing sums elapsed time inside begin/step calls. It includes interrupts,
preemption and clock quantization, but excludes time between calls, native output
transformation and transmission. It is not isolated CPU time or complete
start-to-publish latency. The largest individual step excludes initialization
and loading. Loader timing includes `cr_load`, but **excludes instance creation**;
the complete service-gap measurement is the useful interruption comparison.
Initializers perform backend-specific setup, so their elapsed times are not an
equal-work speed benchmark.

Maximum load-start-to-activation times were 5.671 ms, 10.135 ms and 1.163 ms.
That is elapsed preparation/handoff latency, not service downtime. Native's
longest case waited for a suitable fresh output transfer while services kept
running. The raw `prepare_us` field also includes repeated ready-state polling
until that handoff point; the aggregate labels it `preparation_poll_elapsed_us`.

## Instrumentation correction

The first three runs per candidate used a common 1 ms probe that scanned the
worker's unused stack and queried heap fragmentation. Typed produced about
47 fps in those runs despite only about 1.83 ms of render calls. Its 66 resume
passes per frame repeatedly interleaved with that costly instrumentation and the
single-buffer output hold, pushing work across the next frame boundary.

I moved stack scanning outside the measured window and fragmentation queries to
lifecycle checkpoints, retaining the 1 ms free-heap sample and SDK low-watermark.
**All three candidates were rebuilt and rerun three times.** The typed instruction
quantum, algorithms, scheduling and buffer policy were unchanged. Typed then
sustained 60 fps in all three runs. The remaining memory probes totalled about
12–15 ms across each roughly 10.7-second measured window.

The [original raw runs and harness](../results/device-c3-2026-10-14/profiling-overhead-baseline/)
are retained. Their 47 fps result is an instrumentation-sensitive observation,
not an interpreter performance ceiling. The comparison above uses only the
corrected, matched reruns.

## Architecture decision

For the requested explicit buffers, resumability and independent replacement,
**I would advance the typed interpreter as the leading default execution model**,
with native operations for expensive bulk work. It met this concrete 60 fps
workload, had the smallest footprint and caused almost no additional replacement
stall above the common scheduler baseline. Dispatch enforces its instruction
budget and buffer access rules. The cost is a bespoke instruction engine and a
much smaller compute margin: richer arithmetic, more effects and source-language
semantics could reverse the throughput result.

**Keep WAMR as the alternative when compiled compute and the standard WASM
toolchain matter more than replacement headroom.** It clearly fits this board
for this workload. Its measured RAM premium and approximately 4.9 ms service gap
are concrete costs. WAMR still needs the same component scheduler, continuations,
lifecycle and buffer ownership policy. This test demonstrates native access to
WAMR-owned linear storage; it does not prove arbitrary driver-selected storage
can be mapped into a guest. Raw guest stores do not implement typed buffer
revocation, and arbitrary AOT loops are not made resumable by this wrapper.

**Use direct native for trusted components and as the performance reference.**
Its RAM and speed combination is excellent here. However, checked fixture code
and a manifest do not contain arbitrary machine code. A production choice needs
a trusted compilation path that preserves bounds checks, yield points, ABI and
lifetime rules. It also needs target-specific executable packages and loader
maintenance. The measured speed alone does not resolve that trust model.

Both executable-code backends require disabling the C3 memory protection feature
for their RAM-loaded code. Typed used that same SDK setting for a fair comparison;
this is not evidence that typed requires it. WAMR used unmodified upstream ESP
memory mapping/relocation code. The integration required a registered POSIX
worker and explicit native stack bounds; all three used the same worker layout.

Python, JavaScript, blocks and Pixelblaze can remain authoring inputs to the
chosen execution contract. No frontend is implemented or validated by this
integer-only fixture. Numeric semantics, floating-point/trigonometric workloads,
state, events and the permitted language subsets remain requirements for that
separate compiler layer.

The measured communications service is **USB**, and replacement is **RAM-resident**.
Wi-Fi/MQTT/Matter, persistent flash-write stalls, power-loss recovery, event floods,
network reconnects and long-duration fragmentation were not tested here. This
experiment validates one useful point in the design space, not the entire
documented architecture request or a proven worst-case timing bound.

## Restoration and verification

All three corrected firmware builds and all nine hardware runs passed. The
typed adapter's host checks passed; the shared typed regression suite
(five tests) and independent coordinator suite (fourteen tests) also passed after
the changes.

The board's original **complete 4 MiB flash image was restored and verified** with
esptool before restarting its original firmware. Device identity, active settings,
saved settings and original idle mode matched the pre-test snapshot. The
[restoration record](../results/device-c3-2026-10-14/restoration.json) records the
backup digest and successful checks. The full flash backup remains in ignored
`build/`, because it may contain private configuration.
