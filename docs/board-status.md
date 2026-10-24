# Board status and native input sources

Configuration → **Board status** shows supported native measurements, units and
current availability. Animation/transition input controls and saved recipes offer
these same measurements under **Board → …**. No external publisher or browser
connection is needed to keep a binding running.

This follows picolights' microcontroller status (temperature, voltage, frequency,
free memory and uptime), while retaining physical units and explicit unavailable
readings. Identity, IP and hostname remain controller/network metadata rather
than scalar animation inputs. Reset remains a management action, not a sensor.

| Source | Type / unit | ESP32 C3/S3/C6 | Pico / Pico W / Pico 2 W |
| --- | --- | --- | --- |
| `board.temperature_c` | float / °C | Internal chip sensor | Internal ADC temperature sensor |
| `board.supply_voltage_v` | float / V | Not available without additional wiring | Pico only: VSYS through the onboard divider |
| `board.cpu_frequency_hz` | int / Hz | Current CPU clock | Current system clock |
| `board.heap_free_bytes` | int / bytes | Allocator's free 8-bit-capable heap | Allocator free blocks plus uncommitted heap space |
| `board.heap_min_bytes` | int / bytes | Allocator low-water mark since boot | Not available |
| `board.wifi_rssi_dbm` | int / dBm | Wi-Fi builds, connected station only | Not exposed (blocking radio query) |
| `board.network_online` | bool | Wi-Fi builds | Wi-Fi builds |
| `board.uptime_s` | float / s | Yes | Yes |

The host port exposes uptime and, in Wi-Fi-enabled test builds, network state.
Unsupported measurements are absent from
`input_list`; a supported sensor that fails is present with `valid: false`,
`value: null` and `age_ms: null`. Disconnected station RSSI and AP-mode RSSI are
unavailable, not zero. Network online follows the normal network service state;
an active AP counts as online even without a station client.

The ESP32 temperature reader switches between overlapping −10…80 °C and
50…125 °C ranges so a hot chip does not simply disappear at 80 °C. Values outside
the supported ranges or driver errors remain unavailable. This measures silicon
temperature, not room temperature or a separate hot regulator. There is no
automatic thermal shutdown/throttling in this feature.

Pico temperature and VSYS conversions assume a 3.3 V ADC reference; neither is a
precision calibrated instrument. VSYS is the board supply, not the CPU core or
LED supply. On Pico W and Pico 2 W, VSYS shares a pin with the CYW43 clock.
Waking the radio to release this pin, and requesting RSSI, both require
synchronous CYW43 firmware ioctls with a one-second timeout in Pico SDK 2.3.1.
These two sources are therefore omitted on wireless Pico boards until a bounded,
nonblocking reader is available. This also excludes VSYS from non-Wi-Fi builds
on wireless boards: an uninitialized radio cannot safely release the shared pin.
The periodic sampler does not call the radio driver or hold its lock.
Each ADC read has a 100 µs timeout; a sample discards four initial conversions
and averages four. Acquisition runs on the engine owner, never a render worker.

ESP32 supply voltage is deliberately absent: the existing XIAO profiles do not
provide a wired supply-divider ADC channel. A nominal regulator setting would
not measure the supply. Adding external voltage sensing requires an explicit
pin/divider configuration and pin ownership support.

## Bind and save

For an animation input named `heat`, map 30…80 °C to 0…1:

```json
{
  "heat": {
    "source": "board.temperature_c",
    "map": { "input_min": 30, "input_max": 80, "output_min": 0, "output_max": 1 },
    "timeout_ms": 1500,
    "fallback": "default"
  }
}
```

Select the sensor in the input's driver menu and adjust **Response range**.
The initial range is a suggested animation mapping, not the sensor's accuracy or
hardware operating limits. Numeric readings can also drive a colour palette.
Save/capture the binding in a program or preset recipe as for any named source.
Only the binding is persisted; live samples and uptime are never restored.
Bindings to unsupported board sources use the normal fallback policy, making
recipes safe to move between board types.

Native sources are sampled at most once per 500 ms, independent of frame rate or
how many inputs bind them. A failed read stops refreshing its receipt time; the
existing binding timeout and `default`/`hold` policy determine fallback. A sensor
recovering resumes publication. Temperature circuits are disabled between reads.
No samples are written to flash.

`input_list` accepts an optional exact `name` filter, for example
`{"op":"input_list","name":"board.temperature_c"}`. A missing source returns
`[]`; non-string, empty and overlong filters are rejected. This avoids serializing
every sensor and its metadata when a monitor needs only one measurement.

`input_list` exposes `origin: "board"`, a human-readable `label`, `unit`,
`suggested_min` and `suggested_max`, alongside the standard value/validity/age.
`describe.board_status_supported` identifies firmware with this interface.
`describe.reset_reason` also reports the last ESP32 reset cause (for example
`usb`, `panic`, `task_watchdog` or `brownout`); unsupported platforms return
`null`. This is diagnostic evidence, not a proof of the physical cause.

The `board.` namespace is reserved even for unsupported/future measurements;
`input_claim` rejects it. Built-ins occupy separate bounded storage, preserving
all 16 externally published source slots. Their publisher generation is zero;
external `input_publish` cannot write them.

## Verification

Host tests cover sample pacing, failures/recovery, non-finite and out-of-range
values, a valid zero-degree reading, reserved-name rejection, source mapping and
the independent 16-source external quota. Browser tests cover sensor selection,
units and response mapping. Firmware is cross-compiled against ESP-IDF 5.5.3 and
Pico SDK 2.3.1. Validation passed: 20/20 minimal-host CTests, 26/26 full-service
host CTests, 17 input integration tests in each host profile, 152 browser tests
(18 existing skips), TypeScript checking and the production web build. ESP32 C3,
S3 and C6 plus Pico, Pico W and Pico 2 W firmware builds pass; all three Pico
RAM/stack software gates pass. Physical sensor accuracy and thermal behaviour still require hardware validation. The C3/S3 smoke test below verifies
readout and firmware health, not calibrated accuracy or a thermal operating limit.

References: [ESP-IDF temperature driver](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/peripherals/temp_sensor.html),
[Raspberry Pi wireless pin sharing](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html),
and the [Raspberry Pi VSYS example](https://github.com/raspberrypi/pico-examples/tree/master/adc/read_vsys).


### C3/S3 USB smoke test — 24 October 2026

The first installed ESP32 image exposed only the core's weak defaults: readers
in a separate IDF archive member had not been linked. Native readers now live
in the existing `platform.c` object. A post-link check rejects ESP32 images
without strong definitions of the metric hooks and CPU clock reader; corrected
C3/S3/C6 builds pass it.

Corrected firmware was flashed with esptool hash verification to C3
`esp32-d4f98d0449c0` (existing 1 MiB store retained) and S3
`esp32-a4cb8fe03008` (512 KiB store retained). Both reported valid temperatures,
160 MHz clocks, allocator statistics and network state, with no boot or animation
errors. Both retained their boot IDs throughout the two-minute sampling period.

| Board | Samples / duration | First | Minimum–maximum | Last |
| --- | --- | --- | --- | --- |
| XIAO ESP32-C3 | 13 / 120 s | 52.1 °C | 51.1–52.1 °C | 51.1 °C |
| XIAO ESP32-S3 | 13 / 120 s | 62.1 °C | 56.1–63.1 °C | 56.1 °C |

The S3 follow-up read was 57.1 °C. These are internal silicon measurements,
not board surface temperatures. No pre-update numeric baseline was recorded;
this test cannot attribute cooling to the firmware. Local sample evidence is
in the ignored `build/board-temperatures-20261024.jsonl`. The C6 initially did not enumerate in the USB hardware inventory. Swapping its
USB connection with the S3 made C6 `esp32-10bda3999904` visible while the S3
ceased enumerating, implicating the connection path. The C6 had the older
1 MiB app partition and LVS1 version-2 store. Its full 4 MiB flash was backed
up to the ignored `build/c6-before-sensors-full-flash.bin`, then the current
2 MiB app/512 KiB LVS2 layout was installed with hash verification. Old-format
settings were not migrated; the clean store initialized successfully and boot
reported no errors. The first chip temperature was 47.6 °C at 160 MHz. Local
follow-up samples are in `build/board-temperatures-c6-20261024.jsonl`; the backup
hash and layout details are in `build/c6-sensor-update-manifest.json`.


The C6's 13 samples over 120 seconds ranged from 47.6 to 54.6 °C, ending at
54.6 °C and still warming. Its boot ID stayed unchanged and its heap remained
stable during sampling. A contemporaneous C3 read was 52.1 °C. The S3 was not
USB-visible on the other connection after the swap; its last measured value
before the swap was 57.1 °C. All three boards have received the corrected
sensor-enabled firmware, but these observations do not establish thermal
steady state or calibrated temperature accuracy.


### Merge review — 24 October 2026

The [merge review](design/board-status-review-2026-10-24.md) removes blocking
Pico radio queries, fixes source-range switching, and records fresh builds and
the pre-existing maximum-service Pico W RAM limit. The capability table above
reflects the reviewed implementation.
