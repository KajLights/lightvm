# Music as an animation source

Open **Audio Playground**, or visit `/#/audio`. **Start listening** runs the shared
Sense/WASM analyser locally without a controller. Choose a microphone, line-in or
OS loopback input, or select a PCM fixture and **Replay fixture silently**. Device
names appear after microphone permission; stop capture before changing input or
calibration. No audio is played through speakers.

Capture requires HTTPS/localhost and current AudioWorklet/clock support. When a
controller serves the editor over plain HTTP on the LAN, run the editor locally
and connect to that controller. See [build and capture details](audio-analysis-browser.md).

## Wiring an animation

Declare a named gateway in the preset or composition's `sources` object, then bind
ordinary INPUTs through its alias. For example, the relevant scene fields are:

```json
{
  "sources": {
    "music": {"type": "gateway", "name": "desk", "analysis_profile": 1}
  },
  "layers": [{"id": "main", "program": "eye", "inputs": {
    "width": {"source": "music.bass"},
    "intensity": {"source": "music.level"}
  }}]
}
```

The complete saved preset still needs its schema and program CRC references.
Choose `desk` as the **Gateway source name** and **Send music to controller**.
Fixture input is synthetic; add `allow_synthetic: true` to the declaration when
intentionally consuming fixtures. A mismatched expected profile remains unavailable.
The browser does not edit animation bindings or retune the Sense.

| Signal under the declared alias | Type and meaning |
| --- | --- |
| `music.level` | Float 0–1, normalized DC-removed RMS |
| `music.bass` | Float 0–1, power in 40–250 Hz |
| `music.mid` | Float 0–1, power in 250–2,000 Hz |
| `music.treble` | Float 0–1, power in 2,000–12,000 Hz |
| `music.centroid` | Float 0–1, logarithmic spectral centre |
| `music.onset` | Float 0–1, decaying detection envelope |
| `music.onsets` | Int32 counter, one increment per newly observed onset |
| `music.band00` … `music.band63` | Float 0–1, transmitted spectrum intervals only |

Profile 1 uses adaptive spectral novelty biased towards percussion. Profile 2 is
an explicit level-rise comparison. `onset` is an envelope, not an event identity
or a beat clock. Parameters and ranges come from the common core manifest. Source
gain, detector threshold and animation brightness are separate controls. The
Mixer/HA master brightness still dims the whole output.

## Comparison and lifecycle

**Compare both algorithms** uses identical PCM and calibration with independent
histories. Plots share capture time; **Export comparison** includes every retained
analysis hop, events, source/session/profile IDs, parameters, dependency revision,
input hash and capture settings. Forward one selected result, or both using the
names shown in the UI. Switching algorithms releases forwarding and starts fresh
histories. Comparisons are local by default.

The [structured gateway](audio-gateway.md) checks ownership, clock uncertainty,
profile/session identity and capture age. The sender retains one newest pending
snapshot; slow reliable transport does not make old data fresh. Ownership loss or
a controller change stops sending. **Stop sending** releases the claim, leaves
scene bindings in place and allows declared fallbacks to apply. Captured metrics
and lease tokens are never persisted in presets.

Capture continues across workspace routes with a visible stop control. Hiding
the browser tab, unloading it or choosing **Stop listening** stops capture and
forwarding. Suspension or input removal requires a fresh start. A permission grant
arriving after cancellation is released without connecting it. Calibration choices
remain in memory for the current page session.

## Validation

See the [reproducible browser checks](audio-analysis-browser.md#reproducible-checks).
The native gateway integration can also run against a host build and a compiled
`examples/animations/eye.py`:

```sh
LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-source-native
```

It uses isolated storage, declares a scoped gateway, sends the browser's binary
codec through the real engine, checks INPUT values and rendered pixels, and
verifies that release preserves bindings and scene generation.
