# Pico asynchronous output: measurements and verification

DMA was first tested as an isolated Pico port overlay, then promoted to the
standard Pico/Pico W driver at the user's request. The attached Pico W is running
the standard firmware with all four original animations and settings restored.
The interpreter, scheduler and bytecode contracts are unchanged.

## Measured result (2026-10-16)

All outputs have 200 pixels. Each application observation lasts 30 seconds.
The baseline and DMA builds use SDK 1.5.1, Arm GNU 13.2.Rel1, Release, 125 MHz,
Wi-Fi, HTTP and boot-sequence detection. Baseline source is commit
`2e5f9d749cac806ce5f3a37c5d62e398418168ac`.

| Four-output workload | Blocking baseline fps | Standard DMA fps | Change |
|---|---:|---:|---:|
| GP16 Rainbow, 3.3-second cycle | 13.27 | 17.46 | +31.6% |
| GP17 Rainbow, 0.5-second cycle | 13.27 | 17.66 | +33.1% |
| GP15 warm Twinkle, 8 star slots | 13.27 | 17.83 | +34.4% |
| GP27 cool Twinkle, 40 star slots | 9.14 | 13.86 | +51.8% |

The initial prototype's matched controls measured 25.0 to 32.1/33.0 fps for two
Rainbows, and 48.5 to 48.8 fps for one Rainbow. This is a multi-output overlap
improvement; a VM still waits for its own output before its next render. There
is no new rendering double buffer or native drawing optimization.

The direct production-driver probe compares both drivers in one executable,
with 40 samples per case:

| Outputs | Blocking submit / total (ms) | DMA submit CPU time (ms) | DMA total (ms) |
|---|---:|---:|---:|
| 1 | 6.30 | 0.200 | 6.50 |
| 2 | 12.60 | 0.400 | 6.70 |
| 4 | 25.20 | 0.798 | 7.10 |

DMA cuts time inside submission by about 97%. Four wire transfers and their
reset guards complete about 3.55 times faster because the pins transmit in
parallel. Total animation speed remains limited by rendering and scheduling.
Twinkle work also varies with its random population; the isolated direct-driver
comparison establishes the output cost independently of that variation.

`show_us` in application telemetry includes other devices' rendering and delayed
completion polling. It may increase with DMA even when throughput improves.
It measures neither CPU occupancy nor precise wire duration. Sliced `render_us`
still reports only the last slice; frame counts are the end-to-end metric here.

## Cost and default selection

The production Pico driver adds one DMA channel, 800 bytes of wire-format SRAM
and 36 bytes of additional driver state per 200-pixel output: 836 bytes per output,
or 3,344 bytes for four, excluding allocator overhead. Firmware text increases
by 1,960 bytes; static RAM is unchanged. VM heap remained at 14,260 bytes in every
four-output check. The existing Pico W RAM gate passes with its full reservations.

Pico/Pico W always build `platforms/pico/pixels.c` and link `hardware_dma`; no
experimental option is needed. ESP32 uses asynchronous RMT. On S3 it first tries
DMA with a 1,024-symbol buffer, then ordinary asynchronous RMT if DMA resources
are unavailable. The S3 has one DMA-capable RMT TX channel; additional outputs
can still open. C3/C6 keep ordinary asynchronous RMT because they lack RMT DMA.
All five default board images were built and packaged with the normal browser
firmware packager. CI and the browser-installer workflow build these standard
targets, so generated installable images inherit the default DMA selection.

## Verified behaviour

The production probe passed 21 on-chip GPIO capture cases, decoding 200 pixels
each: every RGB ordering, brightness 0/100/255 and all four configured pins. A
separate PIO receiver samples the driven GPIO internally; no loopback wire is
needed. This verifies digital output at the chip, not electrical quality at a
strip. The direct-driver test deliberately mutates the caller buffer after
submission to verify retained staging; the public API still requires immutable
RGB until completion or cancellation.

Other checks passed: invalid-count and busy rejection; 64 close/reopen cycles
with active DMA; DMA exhaustion cleanup; forced stalled-DMA timeout, cancellation
and successful retry; and late completion polling without a false timeout.
The production driver uses the DMA non-triggering control alias when clearing
faults, so abort recovery cannot accidentally restart a remaining transfer.

Application checks passed paused-frame isolation during peer reset, synchronous
manual FRAME, flash upload while peers continue output, eight remove/recreate
cycles and a final 30-second four-output run without faults, restart or VM heap
growth. Saved configuration and both stored procedure byte streams were unchanged.
Six native host contracts and 73 Python tests passed across the host checks;
HTTP/recovery checks required loopback access and a current recovery build.
Pico, Pico W, S3, C3 and C6 builds passed. Only the attached Pico W was tested on
hardware; the ESP32 DMA preference was build-verified.

## Reproduce

Build normal firmware using the root README. For the additional hardware probe,
use the same normal flags and select this probe project:

```sh
cmake -S . -B build/pico-output-probe \
  -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico_w -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH="$PICO_SDK_PATH" -DPICO_TOOLCHAIN_PATH="$PICO_TOOLCHAIN_PATH" \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_BOOT_SEQUENCE=ON \
  -DLIGHTVM_PLATFORM_DIR="$PWD/experiments/pico-async-output" \
  -DLIGHTVM_BOARD_CONFIG="$PWD/platforms/pico/boards/pico_w.h"
cmake --build build/pico-output-probe -j8
```

This also builds normal `lightvm.uf2`; the alternate project adds
`output_probe.uf2` without changing the production driver. The probe includes
that production source plus the frozen original `blocking_pixels.c` control.

Before flashing, capture active/saved configuration, instance settings and
procedure bytes with `measure.py capture --port PORT --before build/.../before.json`.
Keep full flash backups under ignored `build/`: they can contain network credentials.
Save a verified rollback image and preserve reserved storage when flashing.
After each firmware change, `measure.py restore` recreates the active setup;
it restores settings/playback but restarts animation clocks and RNG state.

Flash the probe UF2, then run `read_probe.py --port PORT --output build/.../probe.json`.
Finally flash normal firmware and restore playback. `measure.py suite` measures
four, two and one animations while resuming temporarily paused peers afterward.
`checks.py` runs application lifecycle and upload checks and restores playback.
Scripts use the repository protocol client and a POSIX serial transport; pyserial
is not required. Aggregate evidence is in [results.json](results.json); raw samples,
flash backups and build logs remain under ignored `build/pico-async-output/`.
