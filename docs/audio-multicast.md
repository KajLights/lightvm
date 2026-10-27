# Native audio inputs and Python examples

The Pico Wi-Fi port receives kaj-fi **LVAF v4** feature packets and publishes
ordinary animation inputs. The firmware contains the receiver, configuration
and typed input adapter. The visualizations below are Python programs compiled
by the existing compiler and rendered by the existing VM.

The [preset-driven subscription design](design/feature-proposal-audio-subscriptions.md)
replaces this temporary global receiver configuration in the next protocol
implementation. It is a design; the current audio transport below remains active.

## Build and configure

Enable `LIGHTVM_WIFI=ON` and `LIGHTVM_AUDIO_MULTICAST=ON` in a Pico Wi-Fi build.
HTTP and MQTT are independent options. The feature defaults to OFF. The host
build exercises the decoder and input adapter through tests; a host UDP adapter
and an ESP32 network adapter are not implemented.

Add this optional root field to the existing `lightvm.config` document:

```json
"audio": {
  "sensor_id": "00001cdbd45cce94",
  "group": "239.255.42.99",
  "port": 49152,
  "allow_synthetic": false
}
```

Use the intended analyser's nonzero 16-digit hexadecimal ID. Group and port
default to the values shown; only 239.x.x.x groups and ports 1024–65535 are
accepted. Synthetic packets require explicit opt-in. Omitting `audio` disables
the adapter. Save the configuration to retain the subscription after reboot.
The browser configuration editor preserves this field when editing outputs;
subscription settings currently use JSON rather than a dedicated form.

The UDP socket binds the selected interface and port, joins the multicast group,
and also accepts unicast addressed to that controller. The temporary unicast
experiment therefore needs no separate receiver or change to input bindings.

## Inputs and timing

| Source | Type | Meaning |
| --- | --- | --- |
| `audio.level` | float 0–1 | Normalized RMS level |
| `audio.bass` | float 0–1 | 40–250 Hz energy |
| `audio.mid` | float 0–1 | 250–2,000 Hz energy |
| `audio.treble` | float 0–1 | 2,000–12,000 Hz energy |
| `audio.centroid` | float 0–1 | Logarithmic spectral centre |
| `audio.onset` | float 0–1 | Transient envelope |
| `audio.onsets` | int | Count of distinct accepted onset occurrences |
| `audio.band00` … `audio.band15` | float 0–1 | First sixteen transmitted spectrum bands, in increasing frequency order |

These 23 sources use 23 of the 32 publisher source slots. All 23
publish atomically before engine rendering. The receiver retains **one latest
metric snapshot**; a burst replaces older metrics while inspecting each packet's
events. There is no deliberate playback delay, interpolation or jitter FIFO.
The sender analyses at 46.875 Hz and publishes every fifth hop, **9.375 Hz**.

Receipt freshness advances only with a newer measurement. Duplicate or reordered
packets and subsequent input publication do not refresh it. Normal input bindings
default to a 1,000 ms timeout and their declared default value. Network disconnect
clears pending work; reconnect rejoins the group. A competing publisher claim
stops the adapter on its next publication with `conflict:true`; it does not keep
reclaiming names. Reapplying configuration explicitly claims the sources again.

The decoder validates exact size/version, finite bounded values, identity,
timestamps, event ordering and the spectrum layout. Packets are at most 692 bytes;
the callback processes at most eight per network poll. Decoding and publication
allocate no application heap. All spectrum records are validated; the first
sixteen are exposed as inputs. Missing bands publish zero rather than retaining
old values. The rainbow example expects the Sense's 16-band layout; changing
the sender's band count changes the frequency range associated with each index.

Events have a 350 ms sender lifetime and a 64-ID deduplication window per session.
Useful events in an older packet can increment the count without rolling metrics
back. Events too far behind the newest sender audio timestamp or locally stale
data are discarded. A live session cannot be replaced until measurements have
stopped for a second; four retired session IDs are remembered. This is trusted-LAN
identity filtering, not authentication or synchronized one-way age measurement.

`audio.onsets` is a latched counter, **not a per-animation event queue**. It persists
across sender sessions while this adapter exists, resets when configuration is
reapplied/rebooted, and wraps from INT32_MAX to 1. A renderer can observe several
new occurrences as one count jump. The examples flash on a change and skip an
initial historical value; pause/resume may still observe a count accumulated
while paused. Independent trigger consumption and pause expiry remain future work.

## Saved Python examples

- [`audio_spectrum.py`](../examples/animations/audio_spectrum.py) is the current
  16-band test: fixed rainbow sections run from red low frequencies at the wired
  end to violet high frequencies, with brightness proportional to each band's
  energy. A new onset flashes the entire strip white for 180 ms. All three
  instances follow the same `audio.level` reference, with 180 ms gain reduction
  and 3 s gain recovery. In the logarithmic input domain, subtracting this shared
  reference compensates for volume without equalizing away the spectrum shape.
  `headroom=0.32` and `span=0.40` set the display range. Reference has a 0.28 floor,
  holds through quiet gaps, and a level gate fades out background below 0.20.
  The [composition](../examples/compositions/audio-spectrum.json) uses three
  instances of the same bytecode, each binding up to six bands, level and onset
  count (at most eight INPUTs). The final instance uses four bands.
  Disjoint sections combine normally; ordinary scalar inputs and the existing
  renderer handle everything. Sixteen native range fills draw the complete strip.
  The [no-flash composition](../examples/compositions/audio-spectrum-no-flash.json)
  uses the same bytecode and auto-levelling, with each `onsets` input bound to
  constant zero. Save it as `audio_spectrum_no_flash` to keep both choices in
  the preset selector without changing the original flashing version. Direct
  switching between the two presets has been verified on the 200-pixel Pico
  with automatic memory budgeting.
- [`audio_features.py`](../examples/animations/audio_features.py) divides the strip
  into six meters, from the wired end: green level, red bass, amber mid, blue
  treble, purple centroid position, and magenta onset envelope. The final section
  flashes white on a new occurrence count. Uncoil the strip to read these meters.
- [`audio_pulse.py`](../examples/animations/audio_pulse.py) uses the whole strip,
  so it remains readable while coiled. Level/bass drive amber brightness; a new
  occurrence briefly turns it white. Its `floor` and `span` settings expand the
  useful normalized energy range, defaulting to 0.35 and 0.35. It adds no smoothing
  buffer. An onset is a detected attack, not a guaranteed musical beat.

The accompanying [meter bindings](../examples/compositions/audio-features.json)
and [pulse bindings](../examples/compositions/audio-pulse.json) use ordinary input
recipes. Compile from the repository root:

```sh
python -m lightvm.compiler examples/animations/audio_features.py -o build/audio_features.lbc
python -m lightvm.compiler examples/animations/audio_pulse.py -o build/audio_pulse.lbc
python -m lightvm.compiler examples/animations/audio_spectrum.py -o build/audio_spectrum.lbc
```

For example, save and recall the spectrum with the existing Python client:

```python
import json, sys, zlib
from pathlib import Path
sys.path.insert(0, "tools")
from client import Client, SerialTransport

client = Client(SerialTransport("/dev/cu.usbmodem3", no_reset=True))
try:
    code = Path("build/audio_spectrum.lbc").read_bytes()
    client.save_program("audio_spectrum", code)
    layers = json.loads(Path("examples/compositions/audio-spectrum.json").read_text())["layers"]
    for layer in layers:
        layer["crc32"] = zlib.crc32(code)  # Saved presets pin a program version.
    client.save_preset("audio_spectrum", layers, output="lights")
    client.recall_preset("audio_spectrum", output="lights")
finally:
    client.close()
```

Adapt the port/output to the device. A transient `client.upload(...)` starts paused;
call `client.resume_program(...)` for live playback. To restore the current preset
and master brightness after reboot, save output startup mode `restore_last`.

Every saved preset appears automatically in Home Assistant's effect selector
for outputs with `home_assistant` configured. There is no separate exposed-preset
list; the output's `default_preset` only chooses its initial ON behavior.

## Diagnostics and validation

`describe.audio_multicast_supported` advertises the build option. Management
`{"op":"audio_status"}` reports selection/session, listening, online, ownership
conflict, sequence, receipt age, accepted packets, publications, gaps, coalescing,
reordering, invalid/ignored packets, event duplicates/expiry and ingress overflow.
`max_service_us` is local receipt-to-input-publication time, not network latency.
It includes foreground stalls such as saving programs. `input_list` and normal
program state expose the actual input values and fallback state.

The 26 October Pico W build uses the existing `LIGHTVM_PIXEL_CAPACITY=200` option
for the attached 200-pixel GRB strip on GP16. It retains a 64 KiB VM heap, 16/8 KiB
CPU stacks and 16 KiB native reserve, with 21,628 bytes remaining after reservations.
The generic 1,024-pixel profile already fails the RAM reservation gate at the base
revision; the receiver adds 36 bytes of static state. Its dynamic state and 23
input slots are charged to the existing VM allocation budget.

Host validation passed all 30 CTests under ASan/UBSan, the four compiled Python
example tests, Pico stack-tool tests and browser configuration tests/build. Short
physical checks verified the six values and 200 rendered RGB pixels, one onset
count for three repeated copies, malformed/wrong-sensor rejection, one-second
fallback, saved presets, and real Sense reception. The software stack gate bounds
main/worker paths at 12,152/5,104 bytes including interrupt allowances. This is a
short functional test, not a long stress qualification.

The 16-band extension passed all 30 CTests, 23 Python example/input tests and the
same Pico RAM/stack gates. Checks cover atomic spectrum publication, clearing
missing bands, source slot 31, all sixteen rendered section colors/brightnesses,
silence, shared white flashes and duplicate counts. An eight-second live check
confirmed all sixteen inputs varied, all four bytecode layers rendered without
faults/fallbacks, and unicast remained online without invalid packets or overflow.
Sense readback confirmed every analysis/onset setting was unchanged. The Pico
is left on the saved `audio_spectrum` preset at 25% master brightness with
restore-last startup. Evidence is in ignored `build/audio-receiver/spectrum-*`.
The final range-fill version averaged about 1.24 ms per layer render and used
34,120 bytes of the unchanged 65,536-byte VM heap. Replacing the initial larger
version required clearing its running layers before validating the replacement,
rather than retaining both scenes within that budget. Saved presets pin program
checksums; remove/re-save the experimental preset when overwriting its program.

See [the network experiment](../../kaj-fi/docs/network-validation.md) for the
multicast/unicast comparison and [the design record](design/feature-proposal-audio-multicast.md)
for deferred subscription and event-consumption work.

## Adaptive spectrum and onsets, 26 October 2026

The auto-levelled rainbow remains a saved Python example compiled by the ordinary
compiler; the Pico firmware and renderer did not change. Five example tests and
19 input tests pass on the host interpreter. Coverage includes all 16 sections,
shared event flashes, equal brightness after a shared +0.30 level shift, recovery
after volume drops, contrast retention, and no rising gain during a quiet gap.
An eight-second live check verified all four layers and all 16 band inputs without
faults or fallback and without new invalid packets or receive-queue overflow.

The Sense now uses [adaptive percussion novelty](../../kaj-fi/docs/onset-detection.md).
Its saved display gain is 0 dB and floor −72 dB, replacing +12 dB/−60 dB. This
preserves the physical lower floor and adds 12 dB of headroom before transmitted
levels saturate. Python supplies visual sensitivity instead. No level controller
can recover information already clipped at the microphone. The 25% master
brightness, unicast path and saved `audio_spectrum` preset remain active.

## Selectable preset follow-up

A recall check exposed a peak-memory problem in the initial four-layer example:
selecting Rainbow and then returning to Spectrum failed while the old scene
remained resident. The current example groups six, six and four bands into three
Python instances. It keeps the same 16 sections, common level reference and white
onset flashes without changing firmware, interpreter limits or heap reservations.
All 24 example/input tests pass with this layout.

The updated bytecode and `audio_spectrum` preset were saved on the Pico over
Wi-Fi. Two complete Rainbow → Spectrum selection cycles succeeded; all three
layers rendered without faults. The readback showed 36,846 bytes in use with the
prior Rainbow scene retained and a 64,098-byte peak under the 65,536-byte ceiling.
The saved Home Assistant selector now contains `rainbow`, `stars` and
`audio_spectrum`. Restore-last startup and 25% master brightness are retained.
Local evidence is in ignored `build/audio-receiver/three-layer-spectrum-*`.

The larger existing Stars scene can still exceed peak loader memory when
returning directly to Spectrum with the preceding Spectrum scene retained.
Select Rainbow first, then Spectrum; that intermediate recall releases the
larger retained scene. This is an existing scene-overlap/admission constraint,
not an onset or transport failure. No firmware or heap limit was changed for
this example. See `build/audio-receiver/spectrum-stars-via-rainbow.json` for the
verified sequence.

### Automatic memory budgeting — 27 October 2026

The subsequent [automatic memory budgeting change](design/automatic-memory-budgeting-2026-10-27.md)
raises this Pico's admitted VM memory to 87,160 bytes while preserving the native
reserves. Direct Stars → Spectrum and flash ↔ no-flash spectrum switches now pass,
with a measured 74,582-byte peak. The Rainbow detour described above is no longer
needed for these presets. Both saved spectrum presets remain selectable.
