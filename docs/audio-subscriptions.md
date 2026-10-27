# Audio subscriptions and Python visualizations

Presets declare their own audio services. The native controller receives LVAF v5
features through leased UDP subscriptions or the browser command gateway; Python
animations continue to use ordinary typed INPUT bindings. There is no root
hardware `audio` setting or global multicast receiver.

## Build and provision

Fetch the portable wire/subscriber dependency pinned in `kaj-audio.rev` with
`python tools/fetch_audio_core.py`, or set `KAJ_AUDIO_CORE` to that revision's
`core` directory. Enable `LIGHTVM_AUDIO_SUBSCRIPTIONS=ON`; Pico W also needs
`LIGHTVM_WIFI=ON`. HTTP, WebSocket and MQTT are separate options. The host port
supports the same UDP and gateway contract for deterministic local checks.

Before replacing an earlier experimental firmware, export configuration, saved
programs, presets and current selection. Remove root `audio` and each output's
obsolete `home_assistant.presets` list from the exported hardware configuration,
then reapply/save it over USB. Add `sources` to the audio presets and re-save them
with the installed program checksums. There is no migration or old protocol
fallback. If experimental storage cannot be read, wipe/reprovision from the export
and restore the selected preset, output geometry and master brightness.

An audio preset can include:

```json
"sources": {
  "audio": {
    "type": "lvaf",
    "address": "192.0.2.40",
    "analysis_profile": 1
  }
}
```

Replace the documentation address with the Sense/host IPv4 address or ordinary
router DNS hostname. The default UDP port is 49152. `.local` is rejected until
peer mDNS resolution is implemented. An optional nonzero 16-digit hexadecimal
`source_id` pins the logical source; an omitted pin accepts the identity returned
by the endpoint. Synthetic fixtures require `allow_synthetic: true`.

For browser input, use `{ "type": "gateway", "name": "music" }` instead of the
UDP declaration. The [Audio Playground](audio-analysis-browser.md) forwards a
structured stream under that gateway name. Both transports share four canonical
provider slots. Identical endpoint, profile, identity and synthetic-input policies
share a provider across outputs and scenes; aliases remain local to each scene.

## Editing and observing sources

In **Configuration → Presets → Sources**, add/edit the alias, provider, address or
gateway name, algorithm and identity policy. Saving preserves current playback
and acquires no subscriptions. Activate the edited preset to apply its policy.
Live status is read-only and shows matching active providers; it does not probe an
inactive endpoint. **Pin observed identity** explicitly copies an observed ID into
the draft. If an alias changes, update its input bindings too.

`describe.audio_subscriptions_supported` advertises support. `audio_status` reports
providers with canonical endpoint/policy, active reference count, connection or
mismatch status, observed source/session/profile, spectrum edges, packet count,
age rejections and charged memory. Program/composition state includes the scene's
source declarations. `input_list` contains ordinary global publishers and board
signals; scoped aliases are resolved within their scene instead.

## Inputs and timing

| Alias suffix | Type | Meaning |
| --- | --- | --- |
| `level`, `bass`, `mid`, `treble`, `centroid`, `onset` | float | Normalized level, frequency energy, spectral centre or onset envelope |
| `band00` … `band63` | float | Spectrum band, when present in the advertised format |
| `onsets` | int | Distinct accepted onset occurrences for this binding |

The editor lists all declared spectrum inputs and offers onset counters for
integer inputs. Selecting a counter clears an old level mapping so later events
are not clamped to one. Counter observations are independent for each binding;
reattachment establishes a new baseline instead of replaying paused events.

The publisher analyses 1,024-sample hops at 48 kHz and sends every fifth hop
(9.375 Hz). The controller retains the latest metric snapshot with no deliberate
playback delay. Reordered eligible events can be deduplicated without refreshing
old metrics. Capture/clock freshness is bounded to 350 ms; increasing a binding's
timeout does not make an expired source live. Missing bands are unavailable.
INPUT defaults/hold policy still determine the displayed value during loss.

Profile 1 is Adaptive percussion v1; profile 2 is the explicit level-rise
comparison. A profile or identity mismatch stays unavailable. Source identity is
a trusted-LAN filter, not authentication. See the
[wire/lifecycle design](design/feature-proposal-audio-subscriptions.md) and
[shared analyser contract](design/audio-analysis-profiles.md).

## Saved Python examples

The [meter](../examples/compositions/audio-features.json),
[pulse](../examples/compositions/audio-pulse.json),
[16-band rainbow](../examples/compositions/audio-spectrum.json) and
[rainbow without flashes](../examples/compositions/audio-spectrum-no-flash.json)
recipes declare the `audio` alias. Replace their documentation address before use.
The rainbow has three instances covering six, six and four bands, sharing the
level reference for volume adaptation. The no-flash recipe binds onset inputs to
zero. No networking or DSP is compiled into these Python effects.

```sh
python -m lightvm.compiler examples/animations/audio_spectrum.py -o build/audio_spectrum.lbc
```

With an already connected Python `client`:

```python
import json, zlib
from pathlib import Path

code = Path("build/audio_spectrum.lbc").read_bytes()
recipe = json.loads(Path("examples/compositions/audio-spectrum.json").read_text())
recipe["sources"]["audio"]["address"] = "sense.example.net"
client.save_program("audio_spectrum", code)
for layer in recipe["layers"]:
    layer["crc32"] = zlib.crc32(code)
client.save_preset("audio_spectrum", recipe["layers"], output="lights", sources=recipe["sources"])
client.recall_preset("audio_spectrum", output="lights")
```

All installed presets appear in Home Assistant's effect selector for configured
outputs. The output's default preset chooses initial ON behavior; there is no
separate list of exposed presets.

## Validation status

Native gateway tests exercise structured frames larger than one command,
full-spectrum values, per-consumer events, persistence and stale-generation/age
rejection. The source-editor browser check uses the real native engine to verify
that stored recipes preserve both transports, saves leave subscriptions alone,
and explicit identity pinning takes effect on activation. Run
`npm run test:audio-sources-browser` against an audio-enabled HTTP host build.

The current Pico W build passes its unchanged RAM/stack gates: 134,512 static
bytes, a 64,400-byte automatic VM heap, 16/8 KiB main/worker stacks and 16 KiB
native headroom. Main/worker bounds are 12,184/5,104 bytes. These are software
bounds, not physical watermarks. Transactional resume/manual-transition admission,
physical optimized-S3 FFT parity and short Sense/Pico subscription checks remain
outstanding. Earlier multicast hardware checks do not validate this protocol.
