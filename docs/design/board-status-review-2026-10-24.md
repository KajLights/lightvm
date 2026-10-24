# Board-status review — 24 October 2026

Reviewed the uncommitted additions from **Check S3 board overheating**, based on
`2006da4`, for input ownership, platform boundaries, network latency, build
profiles, editor bindings and tests.

## Findings addressed

- **Pico radio queries could block the engine.** The proposed 500 ms sampler
  called both `cyw43_arch_gpio_get` (to wake the radio before VSYS sampling) and
  `cyw43_wifi_get_rssi`. Both reach the synchronous `cyw43_do_ioctl` loop; the
  pinned Pico SDK sets `CYW43_IOCTL_TIMEOUT_US` to 1,000,000. The ADC's separate
  100 µs timeout does not bound those waits. The GPIO wrapper also hides the
  driver's error. Removed these calls from telemetry. Pico W/Pico 2 W omit RSSI
  and VSYS until a bounded reader exists, including shared-pin VSYS in USB-only
  builds. Temperature, clock, heap, uptime and network state remain supported.
  The compiled wireless telemetry call graphs contain no CYW43 calls.
- **Changing a source kept incompatible physical units.** Switching an existing
  music mapping to temperature retained a 0–1 input range; switching temperature
  to voltage retained the temperature range. Sensor selection now adopts the
  new suggested input range, while retaining output limits/colours and timeout
  policy. Returning from a board sensor to music restores the 0–1 source range.
  Selecting the same source retains a custom range.
- **Paused polling left old readings looking fresh.** The Board status view
  now adds elapsed browser time to the device-reported sample age. Readings
  expire after two seconds even when polling is suspended for a hidden page,
  controller operation or a pending request. A fake-timer regression verifies
  this without issuing more requests.

The implementation follows the existing platform hooks and input-binding
mechanism. Native sources use separate bounded storage, reserve `board.*`, and
preserve all 16 publisher slots. Sampling is independent of rendering frequency,
reads occur on the engine owner, failed samples use existing fallback behaviour,
and no telemetry is persisted. ESP32 temperature is disabled between readings;
its RSSI uses the current associated-AP record. The ESP32 post-link check prevents
weak default hooks from silently replacing hardware readers.

## Validation and limits

- Minimal (22/22) and service-enabled (28/28) host CTests, including mock Pico ADC tests for
  conversion, timeout/error recovery, restored channel state and shared-pin
  exclusion. Browser regressions cover numeric and palette source switching
  and expiry while polling is paused: 154 pass, 18 pre-existing skips. TypeScript
  checking and the production editor build pass. Wi-Fi setup (10 tests), power
  recovery (4 tests), and minimal-host inputs (17 tests) pass. The complete
  Python 3.12 service-profile integration run passes (331 tests, 41 skips),
  with 19 JavaScript protocol tests passing (one skipped). The initial macOS Python 3.9 run
  could not import an existing packaging module's union type annotation;
  rerunning with the project's CI Python version resolves that environment issue.
- Fresh C3, S3 and C6 Wi-Fi/HTTP builds with boot-sequence support pass, including
  strong telemetry-symbol checks. Pico USB, Pico W Wi-Fi/HTTP and Pico 2 W
  Wi-Fi/HTTP builds pass the software RAM and stack gates.
- A read-only USB check of the attached S3, already running the source chat's
  sensor firmware, returned temperature, 160 MHz CPU frequency, heap and uptime
  with no boot error. In AP mode, network online was true and station RSSI was
  unavailable, as intended. This review did not flash firmware.
- No physical Pico was available for sensor calibration or network throughput
  measurements. Build/call-graph checks establish that the periodic Pico sampler
  no longer invokes the radio driver; they are not a hardware throughput result.

The default Pico W profile with **HTTP + WebSocket + MQTT + boot sequence all
enabled** exceeds its reservation gate on unchanged `2006da4` by 632 bytes.
With telemetry it exceeds by 1,328 bytes (696 additional static bytes). This is
a pre-existing failure of this resource combination, not a newly failing configuration;
its RAM guard remains intact. The reviewed Wi-Fi/HTTP profile has 14,184 bytes
remaining after reservations; Pico USB has 25,320 and Pico 2 W Wi-Fi/HTTP has
139,096. The maximum combination needs a separately sized resource profile.

SDK evidence: [CYW43 ioctl wait loop](https://github.com/georgerobotics/cyw43-driver/blob/main/src/cyw43_ll.c),
[Pico timeout configuration](https://github.com/raspberrypi/pico-sdk/blob/2.3.1/src/rp2_common/pico_cyw43_driver/include/cyw43_configport.h).
The actual review used the pinned local SDK files verified by the stack manifest.
