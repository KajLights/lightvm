# Power-cycle sequence detector

`LIGHTVM_BOOT_SEQUENCE` is an optional native feature, disabled by default. Enable
it with `-DLIGHTVM_BOOT_SEQUENCE=ON` in a host/Pico CMake build, or
`-D LIGHTVM_BOOT_SEQUENCE=ON` in an ESP-IDF build. It works without Wi-Fi or HTTP.
With Wi-Fi and HTTP also enabled, its trigger activates the recovery policy below.

## Gesture and timing

Count power-ons **1, 2, 3**. Turn the controller off again before five seconds on the
first two boots; on the third, the detector's flag becomes true. Each power-on
must reach the durable counter write to register. There is no five-second delay
in startup. Saved hardware configuration loads normally. Recovery suppresses automatic
startup playback and preserves the remembered preset and power target.

The counter increments early in application initialization, saturating at three.
After five seconds from detector initialization, the first application tick at
or beyond the deadline durably clears the counter once. The triggered flag stays
true for the rest of that boot, even when the counter returns to zero. A subsequent
boot starts a new sequence once the clear has succeeded.

Time spent powered off does not clear the sequence. A short boot followed by an
overnight power-off still counts on the next power-on. Software controller resets,
watchdog resets and brownouts also count; protocol `RESET`, which resets the
guest program, does not. The timer relies on the application loop making progress.

## Recovery access point

Build with all three options enabled:

```sh
-DLIGHTVM_BOOT_SEQUENCE=ON -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON
```

On the third short boot, Pico W, Pico 2 W and the XIAO ESP32 ports enter a temporary **open**
`lightvm-xxxxxx` AP with the full API at `http://192.168.4.1:80`. This uses the
existing AP/DHCP/discovery implementation, channel 1 and worldwide radio country
`XX`. Recovery overrides saved disabled networking, station credentials, AP
name/password and HTTP port only for the runtime; saved values remain intact.
The hardware configuration loads, while automatic startup playback stays idle.
Management can explicitly activate a stored program during recovery.

The recovery AP remains for the entire boot. Clearing the journal counter after
five seconds does not end recovery. Network commits save settings for the next
boot, including retained passwords, without restarting the AP or dropping the
current HTTP session. Failed saves leave both the AP and old settings intact.
`network_get` shows the editable saved settings; `network_status` shows the
actual recovery mode/port with `recovery:true`. The editor explains the override
and offers **Save network for next boot**.

Keep the recovery boot powered for at least five seconds before restarting to
resume the saved network settings. Restarting sooner, or a failed
counter clear, can trigger recovery again. Time while powered off does not clear
the counter. No network, hardware configuration or program is erased on entry.
There is no new build switch or guest reset command: the policy is enabled by the
three existing build options together. With either Wi-Fi or HTTP excluded, the
detector remains diagnostic and saved startup behavior is unchanged. Firmware
without the detector also keeps its existing AP/initial-setup behavior.

The host can exercise the policy in tests with all three options and a loopback
HTTP override; it does not provide a physical Wi-Fi AP. Initial setup and recovery
are separate reasons for an AP: recovery has precedence and retains saved values.


## Native hook and diagnostics

Native startup policy can query `app_boot_sequence_triggered(const App *)` after
`app_init`. It returns false when the feature is excluded. Enabled builds include
an independent `BootSequence` state in `App`, initialized before saved startup
configuration is applied; later startup policy can query it at that point too.
The shared application evaluates it before saved startup activation, and the
native network ports apply the recovery override. There is no callback
registration, guest import or new management command.

Management `describe` always reports `boot_sequence_supported` and
`recovery_supported` (the latter requires all three build options). When the
detector is supported,
it additionally reports:

```json
"boot_sequence": {"count": 3, "triggered": true, "error": ""}
```

`count` is the last successfully read or persisted counter value. `triggered`
is the latched result. A nonempty `error` reports unavailable storage or a journal
I/O/verification failure. Failed initialization leaves detection inactive; normal
startup and management continue. A failed counter clear is reported and is not
retried every frame. An unsuccessful write may nevertheless have completed before
an error; the next boot scans actual storage and accepts complete valid records.

## Independent storage

The journal uses two 4096-byte erase sectors, with one 256-byte page per update.
Each record has a format version, a 64-bit sequence number, counter and CRC over
the complete page except its final checksum. Boot selects the highest valid
sequence, ignores torn/corrupt records and never reuses a consumed page. Without
a valid record, the counter starts at zero. Exhausting the sequence number reports
an error rather than wrapping.

Updates append without erasing. At sector rollover, the other sector is erased
and verified, then the new record is programmed, verified and synchronized. The
sector holding the latest valid record remains intact throughout rollover. A
normally running controller writes twice per boot: increment, then clear. Configuration
snapshot saves never write or erase this journal.

| Port | Enabled journal location |
|---|---|
| Pico / Pico W (2 MiB flash) | 8 KiB immediately before the configured store; default `0x17e000..0x17ffff` with a 512 KiB store; firmware overlap fails the link |
| Pico 2 W (4 MiB flash) | Same placement rule; default `0x37e000..0x37ffff` with a 512 KiB store; firmware overlap fails the link |
| ESP32-C3 / ESP32-S3 / ESP32-C6 | `lightvm_boot` partition follows the configured store, size `0x2000`; default `0x190000` with a 512 KiB store |
| Host | `<storage-path>.boot-sequence`, 8192 bytes, associated with `--storage`; no storage path means detection is unavailable |

On ESP32, enabling the feature requires installing the generated partition table
as well as the application image. CMake updates its build-local SDK configuration
when the option changes. Use the normal full `idf.py flash` workflow rather than
app-only flashing when first enabling the feature.

Disabled builds omit the detector state/code, native journal hooks, host sidecar
and additional embedded reservation. Existing snapshot sizes and formats stay
unchanged. Disabling the feature does not erase previously written journal data;
re-enabling it can resume that counter. Full-chip erase also erases the journal.

## Verification

`boot_sequence_journal` exercises the exact timer boundary, saturation, sector
rollover, corrupt pages, every-byte interrupted first writes and injected read,
program, erase, verification and synchronization failures. Protocol integration
tests exercise real host process restarts, flag retention, guest reset, unavailable
metadata, detector-only startup and byte-for-byte snapshot preservation. Recovery
integration tests cover idle startup, manual activation, HTTP/USB management,
credential retention, next-boot saves and normal restart after counter clear. CI builds
both feature settings across the maintained native ports. Cross-builds establish
build compatibility; physical power-switch timing still needs hardware validation.
