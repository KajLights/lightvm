# Shared device core validation — 2026-10-23

The extraction was developed in an isolated LightVM worktree based on `fe97e17`.
The original checkout and its existing local edits were not changed. Kaj-fi
consumes this same source directory through an explicit CMake dependency; it
links no VM, animation or LED code. Both applications retain their own schema,
platform storage hooks and owner-task policy.

## LightVM

- HTTP/WebSocket/MQTT/Wi-Fi/boot-sequence host profile: all 25 CTest contracts
  passed, including storage, network policy, HTTP, WebSocket and DNS-SD.
- Minimal host profile with optional networking and boot sequence disabled:
  all 19 CTest contracts passed.
- Python 3.12 full suite: 344 tests, 38 skipped, one failure in
  `test_sweep.SweepTests.test_matching_times_ignore_frame_schedule_and_repeated_frames`.
  The same test also failed in a full run of the unchanged `fe97e17` baseline
  (330 tests, 40 skipped). Its isolated five-test sweep suite passed in both
  trees. This existing timing-sensitive rendering failure remains unresolved;
  the full suite is not reported as green.
- Shared client transport tests: 4 passed. Explicit ESP32 no-reset USB mode
  leaves DTR/RTS alone and clears POSIX hangup-on-close. Pico behavior is retained.
- Editor protocol tests: 19 passed, one skipped. Web build/type checking passed;
  Vitest: 150 passed, 18 skipped. Mixed discovery tests ensure the light editor
  filters audio-only devices by capability.
- ESP32-S3 with Wi-Fi, HTTP, WebSocket, MQTT and boot sequence: compiled with
  ESP-IDF 5.5.3. This LightVM image was not flashed over the audio test device.
- Pico W with Wi-Fi/HTTP/MQTT: compiled with Pico SDK 2.3.1 and Arm GCC 13.2.Rel1.
  RAM gate and linked-image call-graph stack audit passed. Static SRAM is
  132,432 bytes; after VM, stack, driver and operating reservations, 944 bytes
  remain. Main/worker stack margins are 4,280/3,128 bytes including the audited
  interrupt allowance. These are software gates, not physical stack certification.

The Pico audit includes the shared target, new DeviceHost callbacks, storage
policy hook and settings paths. SDK hashes and external assumptions were not
relaxed. No Pico was reflashed as part of this extraction.

## Kaj-fi

Both synthetic and Sense PDM builds compile with ESP-IDF 5.5.3. Host sanitizer
checks cover all 65 spectrum counts, settings serialization/validation, actual
attack/release and onset tuning behavior, DSP, wire protocol and a 10,000-hop
soak. Thirteen Python tests include real loopback multicast.

A standard S3 was flashed with synthetic input. On-device checks passed all
seven tuning fields, invalid/stale revisions, upload ownership, band changes
0/64/16 with fresh multicast sessions, HTTP discovery, mDNS kind/capabilities,
explicit save, five consecutive saves, reboot persistence and USB without reset.

The final 60-second default 16-band measurement included 56 successful HTTP
statistics requests. Mean DSP time was 1,970.65 microseconds; the maximum since
boot was 5,747 microseconds against a 21,333-microsecond deadline. No DSP overruns,
new capture gaps or local send failures occurred during that window. The receiver
saw 2,445 packets, 369 forward sequence gaps and zero malformed/signal failures.
RSSI ranged from -83 to -80 dBm. The board stayed in its original location.

Weak Wi-Fi can fill transmit buffers at 64 bands. The publisher uses bounded
backoff and discards old frames so management remains usable. These measurements
do not establish reliable low-latency RF delivery. Sense microphone capture,
acoustic tuning and long-duration/reconnect acceptance still need physical
hardware. Kaj-fi's `docs/validation.md` contains the detailed report and its
`tools/check_device.py` reproduces runtime management checks.

Hosted CI is configured but was not run here. Kaj-fi's `device-core.rev` pins the
LightVM commit containing this library; publish that commit before running its
cross-repository hosted workflow.
