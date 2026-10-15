# MQTT WASM experiment — 14 October 2026

Follow-up: [focused runtime benchmarks](../runtime/REPORT.md) compare matched
flash-backed wasm3/Toywasm builds and isolate MQTT initialization costs. The
measurements below remain the results of the original memory layout.

**The separate service is not feasible in the tested C3 Wi-Fi/HTTP profile.**
All three admissions failed while the rainbow animation continued normally.
The actual device ran out of native allocatable memory before reaching the
128 KiB tracked VM limit. The same guest also exceeded the default host's
128 KiB tracked budget during wasm3 code translation. A successful MQTT connection
in the separate protocol harness does not establish firmware feasibility.

Keep the feature experimental and off by default. For this C3 configuration,
evaluate native MQTT next, using the same buffered timing test and shared
parameter validation. Native MQTT avoids a second interpreter and compulsory
64 KiB guest page, but its actual cost and timing have not been measured here.
Reducing the interpreted code footprint or choosing a board with more available
RAM are other experiments; no ceiling or WASM memory semantics were changed.

## Artifacts and setup

The portable service is **10,733 bytes**, with SHA-256
`e38c8e76428aac21c413f0bc344ef8e8e4498ca4e1fda87cbc5ac462861e4025`.
It uses the unmodified **coreMQTT v2.3.1 serializer**, a freestanding C asynchronous
state machine, one ordinary **64 KiB** linear-memory page and a 4 KiB C stack.
[Build metadata](results/service.json) includes compiler and dependency hashes;
[build provenance](vendor/coreMQTT/SOURCE.json) verifies the vendored files.
Build/reproduction commands and the ABI are in [README.md](README.md).

The workload is the existing `samples/python/rainbow.py` control program:
**2,263 bytes**, 100 logical pixels, XIAO D1/GPIO3, GRB, brightness 32/255 and
16,667 µs configured frame interval. Its exact hash is in the raw results.
The native RMT driver ran and its output calls were timed, with approximately
3.46 ms last-reported output time. A connected strip was unnecessary and visible
light output was **not** verified. ESP-IDF 5.5.3, Wi-Fi, HTTP and boot-sequence
detection remained enabled, with the C3 VM ceiling fixed at **131,072 bytes**.

## C3 measurements

Each window lasted 30 seconds after five seconds of warmup. Frame gaps were
buffered inside firmware, then downloaded after sampling stopped. No gap samples
were dropped. The baseline and admission runs were collected separately using
the same firmware, guest hashes and settings; the runner retained the completed
baselines when resuming after a transient USB upload timeout.

| Window | FPS | Gap median µs | p95 µs | p99 µs | Maximum µs | Missed deadlines |
|---|---:|---:|---:|---:|---:|---:|
| Animation alone 1 | 59.993 | 16,859 | 17,384 | 17,840 | 18,512 | 0 |
| Animation alone 2 | 59.993 | 16,854 | 17,483 | 17,892 | 19,475 | 0 |
| Animation alone 3 | 59.989 | 16,861 | 17,457 | 17,854 | 19,434 | 0 |
| Failed admission 1 | 60.014 | 16,996 | 17,178 | 17,993 | 19,994 | 0 |
| Failed admission 2 | 59.990 | 16,997 | 17,065 | 18,028 | 22,980 | 0 |
| Failed admission 3 | 59.991 | 16,998 | 17,098 | 17,778 | 20,029 | 0 |

Each admission window includes staging and attempting to load the actual service,
followed by continued animation playback. These are **failed-admission** results,
not measurements with a loaded or connected MQTT guest. Their worst gap exceeded
the baseline maximum by 3,505 µs, within one configured interval. This demonstrates
recovery from failed admission, not the idle/traffic performance targets.

The animation used **22,822 tracked VM bytes**, including its source and control
buffers. Service admission reached **35,428 bytes**, then returned status 3,
`memory allocation failed`; cleanup returned to 22,822 bytes without stopping
animation or invalidating management. The unchanged tracked ceiling was 131,072
bytes. Native free heap was approximately **22,052 bytes**, its lifetime minimum
fell to **8,876 bytes** during admission, and the observed largest free block was
**13,824 bytes**. These native SDK totals are reported separately from VM accounting
and include existing Wi-Fi/HTTP and other native allocations. They cannot provide
a contiguous 65,536-byte linear-memory allocation. No MQTT TCP connection was
allocated on the C3, so native MQTT TCP cost was not measured.

The 9,216-byte timing buffers are an explicit native static cost. Removing them
would still leave the observed free memory below one guest page. During baseline
playback, last-sampled render time was approximately 4.1–4.2 ms; this is a sample,
not a render-time percentile distribution.

Loaded/disconnected, connected/idle, ten commands per second plus 100-command
burst, broker restart, interrupted connection and live service replacement
performance are **unavailable on the C3**, because the service cannot be admitted.
Service execution times and command latency are correspondingly unavailable.
The 95% idle/90% traffic FPS targets have **not** been satisfied or failed by a
running device service; admission has failed first.

The saved station network remained unavailable during the run. No alternate
credentials or Mac network changes were used. Independent replacement through
the shared HTTP management path passed on the host, but a physical C3 HTTP
replacement could not be exercised with the unavailable network and failed guest
admission. This remains an explicit hardware limitation of this result.

[Raw C3 measurements](results/c3-2026-10-14.json) contain all six windows, all frame
gaps, native/VM counters, three failed admissions and explicit unavailable scenarios.

## Protocol and isolation validation

The final host regression suite ran **125 Python tests**, with four pre-existing
feature-dependent skips. Feature-off regression also ran 125 tests, with eleven
appropriate skips. All executed tests passed. Five C contract tests passed in
both builds. The **24 MQTT tests** all passed, covering:

- Fragmented/coalesced packets, one-byte reads, partial writes and queue pressure.
- QoS 1 duplicates, ID reuse, acknowledgements and DUP retransmission with one
  outstanding outgoing publication.
- Malformed/oversized input, unsupported QoS, keepalive timeout and reconnection.
- Stable HA discovery/device identity, availability, Last Will and HA birth.
- Stale generations/revisions, manifest bounds and accepted command envelopes.
- Real wasm3 deadlines and buffer/handle validation, TCP cleanup after a fault,
  allocation cleanup and independent service replacement over HTTP while the
  rainbow continues with the same generation.

The full artifact was also run with one standard memory page in a Wasmtime
protocol harness against **isolated Mosquitto 2.0.22** on loopback port 18883.
It applied ten commands per second plus a 100-command burst (**110 commands**),
rejected one stale command, published four retained discovery configs and retained
online availability, emitted its offline Last Will after connection loss,
reconnected and rediscovered after broker restart and service reinitialization.
A new subscriber received retained discovery and availability. The broker was
separate from Kaj's existing MQTT/HA services and removed after testing.
[Raw broker exchange](results/mosquitto-2026-10-14.json) records the final artifact
hash and observed messages. This verifies an HA-compatible sample, not a live HA
installation or MCU timing.

The default 128 KiB host wasm3 profile rejected the full module during interpreter
code-page allocation, with the tracked peak staying under its ceiling. Small
one-page service fixtures were used for firmware isolation tests; their successful
admission does not imply admission of the full MQTT service. No larger VM budget
was substituted for the capacity test.

## Cross-builds and restoration

C3, S3, C6 and Pico W experimental cross-builds passed. They use the same separately
built service artifact. The S3/C6 firmware images have not been physically run.
Pico W used its explicit experiment profile, HTTP off and **114,688-byte VM
reservation**. Its RAM checker reported:

| Reservation | Bytes |
|---|---:|
| Static data/BSS | 117,804 |
| VM ceiling | 114,688 |
| Native stack | 16,384 |
| Maximum pixel driver | 3,072 |
| Operating headroom | 16,384 |
| Remaining after all reservations | **2,004** |

The 16 KiB operating-headroom requirement was retained. This is a passing build
reservation check, not proof that the full guest fits or runs on physical Pico W.
[Build sizes and hashes](results/builds.json) record the produced firmware images.

The C3's complete **4,194,304-byte flash image** and original settings were backed
up before deployment. The original image was restored after measurement and
verified across the full flash before reboot. Original device identity, feature
set, active configuration, network settings, stored procedures and playback mode
were checked afterward. Backup firmware and credentials remain in ignored
`build/mqtt-c3-backup/`; only the verification result and backup hash are retained
with this report. See [restoration evidence](results/restoration.json).

Physical Pico W execution, physical LED observation, C3 networking/HTTP replacement
with a reachable network, and Kaj's live Home Assistant installation remain
follow-up validation. The present C3 result already rules out this implementation
in its existing Wi-Fi/HTTP memory profile without changing the experiment's limits.
