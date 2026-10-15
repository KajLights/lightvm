# Matched ESP32-C3 component experiment

This separate ESP-IDF project runs the three execution backends on one
XIAO ESP32-C3, at 160 MHz, with 200 WS2812 pixels transmitted on D1/GPIO3.
The shared core drives actual asynchronous RMT hardware and services real USB
requests while two independent effects render. A connected strip is unnecessary
for these electrical-output workload measurements; visible LED behavior and
waveform quality require separate observation.

Read the [same-board results and architecture evaluation](REPORT.md). Raw runs,
build identities and the restoration record are saved under
[`../results/device-c3-2026-10-14/`](../results/device-c3-2026-10-14/).

## Build

Use the pinned WAMR tools installed by the parent experiment and ESP-IDF 5.5.3.
The defaults below use the existing local SDK/toolchain paths; `--idf` and
`--tools` select another installed copy. The measurement runner needs pyserial.

```sh
python3 experiments/component-runtime/device/build.py
```

Each backend gets its own firmware, map, SDK configuration and size report under
`build/component-runtime/device/{backend}-firmware/`. Separate artifact builders
produce the actual uploaded native/AOT/typed code. The native effect is not linked
into its resident firmware. Both executable-code backends require the C3 memory
protection feature to be disabled; the typed image uses the same SDK setting for
this comparison. This is a documented security tradeoff, not arbitrary-code
isolation.

Preserve a fresh full 4 MiB flash backup and settings before deployment, following
the [existing board backup workflow](../../mqtt/README.md#c3-backup-measurement-and-restoration).
Keep full images and settings under ignored `build/`, because NVS can contain
credentials. Flash each build's `flash_args`, then run its measurement:

```sh
build/venv/bin/python experiments/component-runtime/device/measure.py \
  --backend typed --port /dev/cu.usbmodem1101 \
  --v1 build/component-runtime/device/typed-artifacts/v1.tvm \
  --v2 build/component-runtime/device/typed-artifacts/v2.tvm \
  --output build/component-runtime/device/typed-measurements.json
```

For WAMR use `--backend wamr` and `wamr-artifacts/v1.aot`, `v2.aot`.
For native use `--backend native` and `native-artifacts/native-v1.crn`,
`native-v2.crn`. `--smoke --repetitions 1` runs a short hardware check before
collecting the full three repetitions. Restore the original full image and
verify its flash digest after the experiment, including failed runs.

Aggregate a completed set with `python3 experiments/component-runtime/device/analyze.py`.
To reproduce the saved comparison without a board, use:

```sh
python3 experiments/component-runtime/device/analyze.py \
  --input experiments/component-runtime/results/device-c3-2026-10-14 \
  --output build/component-runtime/device/recomputed-comparison.json
```

## Matched workload

- Two independently seeded instances execute the same u32 recurrence and retain
  one 600-byte buffer each. The frame interval is 16,667 µs.
- One selected effect publishes output. A native transform changes RGB to GRB
  and limits brightness to 16/255 in the same allocation. No output copy occurs.
- RMT retains that allocation until its actual completion interrupt. C3 does not
  use DMA for this channel; the driver refills its hardware symbol buffer.
- USB carries 50 numbered echo requests per second, plus actual package uploads.
  The component scheduler services this stream between execution quanta. No
  reporting dump runs inside the measured window.
- After warmup, each repetition measures four seconds of baseline, rejects a
  corrupted upload, performs three real code replacements with 1.5 seconds after
  each, and measures another two seconds. Uploaded code alternates v2/v1/v2.
- v2 changes the initializer to XOR its seed with `0x13579bdf`. Every rendered
  frame is checked against the independent arithmetic oracle before conversion;
  the host checks every collected output sample again.
- Preparation continues alongside the old effects. Activation waits for an old
  frame to enter a fresh RMT transfer, then retains its instance and package until
  that transfer completes. Every successful handoff must record this overlap.
- All three have the same buffer policy: an instance pauses while its own buffer
  is held. The second instance continues; peak replacement includes three buffers.

Native steps process 32 pixels; WAMR steps also process 32 pixels; typed steps
execute 61 instructions. They share a scheduling contract, not an instruction
count that would imply equivalent work. Initialization uses each runtime's bounded
continuation, while actual package loading is measured as a separate operation.

## Accounting and limits

The size report counts static shared instruction/data RAM. The runtime samples
all **internal** heap regions, including executable allocations, and records the
SDK's lifetime minimum free heap. Static RAM plus allocated heap is the reported
RAM envelope. The SDK minimum sums regional low-watermarks that need not occur
simultaneously, so that total is a conservative aggregate peak. A separate sampled
minimum reports observed simultaneous use; sampling can miss short transients.
Heap metadata, alignment/reserved gaps and ROM-only reservations are not allocated
application storage and are not silently treated as available component memory.

The common 1,024-entry output sample array consumes 24,576 static bytes and is
included in every image. USB queues, task stacks, the SDK, driver allocations,
package upload storage, loaded code, instance metadata and retained buffers are
also included. Worker stack high-watermark is reported separately as spare
space inside its already charged 12 KiB allocation, not additional RAM to add.
Free heap is sampled every millisecond. Expensive fragmentation queries run at
lifecycle checkpoints, and stack scanning runs after measurement stops; running
those scans every millisecond materially distorted the initial typed results.
The retained baseline and controlled reruns are explained in the report.

Component-call timing sums elapsed microseconds around begin/step, including
interrupts, scheduler preemption and clock quantization. It is not isolated CPU
time. RMT duration runs from submission to completion callback, including driver
startup. USB round-trip latency includes host scheduling; firmware service gaps
separately expose stalls on the board. Neither maximum is a proven worst case.
Loader timing excludes instance creation; service gaps include it. Preparation
timing includes repeated ready-state polls until a suitable transfer handoff, so
it must not be read as isolated initializer cost.

The run exercises **USB communications and RAM-resident replacement**. It does
not establish Wi-Fi/Matter/MQTT capacity, flash-write interference, persistent
installation recovery, multicore behavior, optical correctness or complete
source-language semantics. Those remain separate architecture gates.
