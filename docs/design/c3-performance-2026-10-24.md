# ESP32-C3 default profile and Pico comparison

This is the preserved report from **Add C3 optimized default profile**. Its
firmware changes were in a separate worktree when the
[optimisation protocol](../optimisation-protocol.md) imported this evidence.
Build instructions below require those profile sources; this report alone does
not mean the profile has been integrated into the current checkout.

24 October 2026. `LIGHTVM_ESP32_PROFILE=AUTO` now selects the measured `c3`
profile on XIAO ESP32-C3; S3 and C6 continue to select `standard`. The profile
combines faster bytecode/artwork reads with bounded single-core scheduling.
The connected C3 was tested with the archived Pico workloads, including the
original saved 200-pixel Stars/background scene.

On the installed final image, that scene delivers a five-window median of
**33.91 updates/s**, versus **4.46** for the preserved standard C3 build and
**40.18** in the archived Pico run: a **7.60×** C3 improvement, about 16% below
the historical Pico median. The fixed-population and other controls below show
where the two boards differ more substantially.

The [structured results](../benchmarks/c3-performance-2026-10-24.json) contain
per-workload medians, ranges, firmware hashes and validation outcomes. The
[evidence archive](../benchmarks/c3-performance-2026-10-24.tar.gz) retains raw
measurements, rejected candidates, frozen packages, harnesses, firmware images,
build settings and source changes. The structured results pin its SHA-256.

## Selected implementation

- Compile the shared core with `-O3`, retaining strict floating-point semantics
  and `-ffp-contract=off`. IDF's switch and ISR compilation options stay intact.
- Put the existing annotated HSV, image-read, image-draw and map kernels in IRAM.
- Map the LightVM flash partition once for its lifetime and read through that
  mapping. Writes and erases still use IDF's partition API and cache invalidation.
  Mapping failure reports unavailable storage; there is no second read backend
  inside the C3 profile.
- Use one render worker, a 2,000 µs slice and a 2,000 µs epoch target. Explicit
  resource settings override these defaults. These targets bound admission;
  they are not hard real-time deadlines for every native operation.
- Charge serial rendering immediately before admitting another layer. Previously
  the collector queued two jobs even on a one-worker executor, so the next
  admission could not see the first slice's cost. Two-worker batching is retained.
- Report the ESP CPU clock and main-task stack high-water mark, and record
  ingress timing at the actual USB read. Earlier ESP firmware did not instrument
  that point, so its zero ingress-overrun counter was not evidence of latency.
- Confirm USB disconnection for 100 ms before destroying its session. Incoming
  bytes cancel a pending disconnect even if the SDK connection indication is
  stale. This ESP port repair also applies to S3/C6 and the standard profile.

CPU remains 160 MHz, flash DIO 40 MHz, bytecode cache 256 bytes, VM heap 128 KiB,
and pixel capacity 1,024. Numeric precision, work weights and frame semantics
are unchanged. The ordinary `vTaskDelay(1)` remains in the main loop.

`-DLIGHTVM_ESP32_PROFILE=standard` disables the C3-specific optimisations;
`c3` explicitly requires a C3 target. Unknown profile names and `c3` on S3/C6
are configuration errors. The generic serial scheduling fix and telemetry remain
in current `standard` builds.

## Hardware and comparison method

The attached device is ESP32-C3 revision 0.4 with 4 MiB flash, built with
ESP-IDF 5.5.3 and Espressif GCC 14.2.0. Wi-Fi, HTTP, WebSocket, MQTT and boot
support were compiled in, with 1 MiB LightVM storage. The C3 network was
unprovisioned throughout. The benchmark uses D1/GPIO3, 200 logical GRB pixels,
brightness limit 32 and USB STATUS polling at 20 Hz.

Five alternating AB/BA pairs compare a preserved standard image with the selected
profile. Case order also reverses between pairs. Ordinary controls use one-second
warmup and three-second measurement. Saved Stars uses seven-second warmup and
30-second measurement. Population-controlled Stars has 20 long-lived sprites,
two-second minimum warmup and at least 25 completed warmup frames. Completed
layer updates and render cost are reported separately from presentations.

The preserved standard image uses core `-O2`, flash-resident kernels, partition
driver reads, 500 µs slices, 4 ms epochs and the original two-job collector. It
includes the new CPU/stack/ingress instrumentation. Thus the paired comparison
measures the entire change, including the serial scheduling repair; rebuilding
today's `standard` option alone does not reproduce that preserved baseline.
The source base is `fe97e17`; exact binary identities and changes are frozen.

The Pico references come from the archived **Plan Pico performance fixes** and
**Refine SDK** work and their checked-in evidence:

- [22 October saved-scene validation](pico-performance-repair-2026-10-22.md)
  for the original saved Stars composition.
- [23 October numeric/control validation](numeric-pico-validation-2026-10-23.md)
  for the frozen control packages, fixed 20-star population and numeric matrix.

The saved Stars comparison uses byte-identical color/Stars programs, the same
input bindings and settings: background brightness 12/255, Stars capacity 20,
duration 2 seconds, probability 5/s, white, fixed colors, brightness 1. The entry
crossfade is omitted from the steady-state measurement. The background reuses
its frame; Stars completions are the useful animation-update metric.

The historical Pico W ran at 125 MHz with two workers and connected Wi-Fi/MQTT.
It used GP16 and PIO/DMA output; C3 uses RMT. The saved-scene Pico run also used
brightness limit 255. There is no simultaneous Pico rerun or matched network
traffic experiment here. Logical frame correctness and USB responsiveness were
checked; optical output was not observed. Cross-board figures are a comparison
of these recorded setups, not a controlled CPU-only ranking.

## Five-pair tuning results

These five pairs measured the selected performance settings before the subsequent
USB disconnect repair. The later final-image confirmation is recorded separately;
the archive preserves both firmware hashes. Values are median completed layer
updates/s. The two mixed-scene rows are
different layers in the same scene. Saved Rainbow is a separate legacy package;
it is not substituted for the newer Rainbow control when comparing with Pico.

| Workload, 200 pixels | C3 before | C3 profile | Archived Pico W |
| --- | ---: | ---: | ---: |
| Saved Stars + background | 4.46 | **36.14** | 40.18 |
| Saved Rainbow | 47.48 | 103.89 | — |
| Fixed 20-star population | 2.66 | 21.88 | 41.47 |
| Map | 24.18 | 72.16 | 151.27 |
| Scalar | 4.31 | 11.26 | 9.62 |
| Draw | 35.46 | 46.76 | 95.94 |
| Sprites | 5.31 | 42.39 | 73.31 |
| Rainbow control | 38.79 | 91.61 | 147.69 |
| Sweep control | 18.24 | 71.98 | 144.32 |
| Mixed / map | 19.22 | 38.09 | 112.61 |
| Mixed / scalar | 2.65 | 5.64 | 6.95 |

The original saved Stars scene improves **8.10× (+710%)** over the preserved C3
baseline. Its final five-window range is 34.51–40.44 updates/s, compared with
the historical Pico range of 39.34–49.26. Its median is about 10% below Pico.
The random population makes this scene less repeatable than the fixed-population
test: the latter reaches 21.88 updates/s on C3 versus 41.47 on Pico. C3 therefore
does not generally match Pico's throughput, despite the large gain over its own
previous profile.

Median render cost for saved Stars falls from 53.69 to 11.12 ms (Pico: 14.72 ms).
The fixed population falls from 97.25 to 20.37 ms (Pico: 18.98 ms). Similar render
cost does not imply similar frame rate: yielding, output and coordination also
consume wall time. The structured results preserve both metrics for every layer.

Across 50 selected-profile windows and 5,749 STATUS samples, the worst per-window
USB p95 is 9.10 ms. There is **one observed ingress gap above 10 ms**, during
the fifth saved-Rainbow window; that window's command maximum is 10.85 ms.
The preserved baseline's 50 windows record zero such gaps and worst p95 9.17 ms.
These results support improved throughput with similar typical service latency;
they do not establish a strict 10 ms service guarantee.

The first attempt at the fifth selected-profile block lost upload state just
after flashing (`no upload or empty chunk`), before any timed sample. Cleanup
also failed in that session. After the campaign's final-image restore, the board
responded consistently with an empty configuration and three completed temporary
programs. Those programs were removed and the block repeated successfully.
The failed log and separate retry status remain in the archive.

A larger numeric upload then timed out. A diagnostic repeat progressed through
13 exact scalar/bulk pairs before losing a staged configuration, with the same
boot identity throughout and no reset/panic in its serial trace. The ESP loop
destroyed sessions immediately on an SDK disconnected indication, which derives
from missing USB SOF packets. The final image requires 100 ms of continuously
missing connection evidence and no incoming data before discarding the session.
Host tests cover intermittent gaps, traffic recovery, sustained loss and reconnect.
The final-image hardware checks repeat the failed matrix and exercise this path
through repeated uploads/configuration changes. The earlier timeouts remain
retained observations; their exact causes were not independently established.

The trace also records `USB_UART_CHIP_RESET` when opening a fresh serial
connection on this host. Deasserting DTR/RTS does not prevent every OS/USB reset
transition. Each benchmark starts from the observed boot and keeps one connection
open for its complete measurement block.

## Final-image confirmation and sustained operation

After the USB repair, all ten controls were repeated once, followed by four more
saved-Stars windows. These are fresh measurements of the installed image, separate
from the five-pair tuning experiment. Saved Stars' five-window median is 33.91
updates/s, range 32.91–39.41, with 11.84 ms median render cost and a flat 17,048-byte
VM heap. Its stochastic population prevents attributing the change from the
earlier 36.14 median to the USB fix alone. The final fixed-20-star confirmation
window records 20.56 updates/s and 20.89 ms render cost.

The final confirmation's 14 windows record zero ingress gaps over 10 ms and
worst per-window USB p95 of 8.27 ms. Two additional 120-second soaks give:

| Scene | Completed updates/s | VM heap bytes | USB p95 ms | Ingress gaps >10 ms |
| --- | ---: | ---: | ---: | ---: |
| Saved Stars | 34.06 | 17,048, flat | 7.52 | 0 |
| Mixed map / scalar | 37.11 / 5.67 | 14,604, flat | 7.36 | 1 |

The single mixed-soak gap means a strict 10 ms ingress guarantee remains unproven.
No reboot or allocation growth occurred within either soak. The image was then
verified byte-for-byte in flash, rebooted, and checked for exact execution of a
persisted program through the mapped store. The test program was deleted and
the empty active configuration restored. The installed firmware SHA-256 is
`367991f4015f9ced086c67f3eb1575cea549b1f9787573bf1bcc5134621ff492`.

## Why this profile

The screening sequence isolated the main contributors. For the fixed sprite
control, the original screen measured approximately 5.3 updates/s and 49.6 ms
render cost. A 2 ms slice with core `-O3` and IRAM kernels reached 16.9 updates/s
and 27.4 ms; adding mapped storage reads reached 42.8 updates/s and 9.85 ms.
These single windows identify candidates; the paired results establish the final
comparison.

Larger slices traded service time for throughput. The 4 ms candidate reached
about 41 updates/s on the saturated 20-star workload, but mixed-layer USB p95
rose to about 12 ms. A 3 ms candidate also produced ingress gaps above 10 ms.
Correcting serial batching removed the mixed-layer admission problem, and the
selected 2 ms slice/epoch combination kept the screened controls responsive.
Jump tables and less frequent idle yielding did not justify their latency tradeoff.
All these screens remain in the archive, including unsuccessful candidates.

The first, uninstrumented baseline screen stopped responding before completion
and was terminated. Its cause was not established. Its partial results are
retained as diagnostic evidence and excluded from the five-pair estimates.

## Bulk numeric throughput on the final image

Three alternating scalar/bulk pairs per workload at 200 pixels give 36 windows
on the final firmware, with one-second warmup and three-second measurement.
Values are median render microseconds. Pico's column comes from its archived
five-pair, two-worker run of the same frozen packages.

| Workload | C3 scalar µs | C3 bulk µs | C3 reduction | Pico bulk µs |
| --- | ---: | ---: | ---: | ---: |
| G1-l64-o-19 | 8,743.6 | 3,263.1 | 62.7% | 1,729.8 |
| G2-l17 | 9,631.1 | 3,573.0 | 62.9% | 2,021.6 |
| Q1-p32768 | 14,119.7 | 4,976.1 | 64.8% | 3,111.9 |
| B1-lazy-piecewise | 14,156.8 | 5,360.0 | 62.1% | 3,815.6 |
| M1-all-pairs | 52,126.8 | 31,303.7 | 39.9% | 31,716.1 |
| M2-all-pairs | 60,940.7 | 33,296.5 | 45.4% | 30,344.8 |

The worst window's USB p95 is 8.50 ms, with zero observed ingress gaps over
10 ms in these 36 windows. These are final-image scalar/bulk comparisons, not
another before/after firmware experiment. Larger pixel counts are covered by
the correctness matrix; numeric throughput here is measured only at 200 pixels.

## Correctness and build validation

- All animation examples match host pixels with identical compiled bytecode,
  including manual frame deltas, live color, snapshots, playback, pause/resume
  and reset heap retention.
- 96 hardware scalar/bulk pairs pass: 32 frozen deterministic cases at 33, 200
  and 1,024 pixels, five explicit frame deltas per variant. The final matrix
  retains one boot identity for its entire run. Default unseeded Stars/Rainbow
  are excluded from exact device replay.
- Four scalar/bulk lazy-fault and recovery cases pass, including 40 rejected
  candidates retaining the live frame/state.
- 32 flash replacement cycles pass exact program readback, pinned old-generation
  frames, rejected live deletion, deletion/recreation and activation. VM heap
  stays at 10,372 bytes in each cycle. These directly exercise cache coherence
  for the new memory mapping.
- The 25 existing host CTest suites pass in Release and ASan/UBSan builds.
  The added USB disconnect test passes in both builds. Another 2,210 host
  numeric replays, 10 performance tests, 18 numeric-runtime tests, 19 composition
  tests and four serial-transport tests pass.
- The C3 USB-only default build and S3/C6 standard builds pass. Invalid profile
  names and an explicit C3 profile on C6 are rejected at configuration time.
  The shared scheduler change cross-builds for Pico W with all services and
  passes its RAM/software stack gates: 132,784 static bytes and 592 bytes left
  after declared reservations. No new Pico hardware validation was performed.

## Memory and deployment

For the tested service-enabled images, linked IRAM grows from 87,040 to 92,672
bytes (+5,632), and DRAM data/BSS from 117,108 to 117,116 bytes (+8). The application
image grows from 1,089,856 to 1,141,904 bytes (including the USB fix). Mapping also consumes IDF MMU/cache
mapping resources; these linked static totals do not count that dynamic metadata.
No additional VM heap or bytecode-cache allowance is reserved by the profile.

The connected board initially held an older experiment (`9db9f42-dirty`) with a
1 MiB app partition and 512 KiB store. Its complete flash was backed up locally
before installation. That private backup is excluded from the evidence archive.
The current firmware uses the repository's 2 MiB app partition and the explicitly
selected 1 MiB store. The old experimental state was not migrated. Reinstall the
bootloader, generated partition table and application together when moving from
that old layout, then reprovision configuration/programs. The C3 profile itself
does not require a new storage format or partition layout.

## Reproducing the C3 build

With ESP-IDF 5.5.3 and its tools activated:

```sh
idf.py -C platforms/esp32 -B "$PWD/build/c3-profile" \
  -DLIGHTVM_BOARD=xiao_esp32c3 -DLIGHTVM_ESP32_PROFILE=AUTO \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_WEBSOCKET=ON \
  -DLIGHTVM_MQTT=ON -DLIGHTVM_BOOT_SEQUENCE=ON \
  -DLIGHTVM_STORAGE_BYTES=1048576 build
idf.py -C platforms/esp32 -B "$PWD/build/c3-profile" -p PORT flash
```

The normal USB-only build also selects the profile with its normal 512 KiB store.
The archive includes the exact tested SDK configuration and CMake cache. Its
campaign scripts contain local tool/device paths; adjust them before replaying.
Use `tools/benchmark_animation_profile.py` with the frozen campaign packages for
the original workloads, and `tools/benchmark_numeric.py --pin D1 --no-reset`
for the frozen scalar/bulk matrix. Both restore active configuration and remove
their temporary programs.
