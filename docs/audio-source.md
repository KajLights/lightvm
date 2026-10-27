# Music as an animation source

This guide covers browser microphone capture. For the standalone Sense analyser
and Pico receiver, see [native audio inputs and Python examples](audio-multicast.md).

The [shared-analysis design](design/audio-analysis-profiles.md) requires replacing
the browser's current extractor with the Sense's adaptive percussion algorithm,
using a common C/WASM core, and adding selectable algorithms for browser testing.
The [subscription/gateway design](design/feature-proposal-audio-subscriptions.md)
allows an animation to use both paths at once and supports Mac/Pi publishers.
These are planned changes; the current browser behavior is described below.

Open **Audio Playground**, or visit `/#/audio`. **Start listening** works without
a controller: grant microphone access and watch the six signal meters. Choose a
microphone, line-in interface or OS loopback device. Device names appear after
permission; stop listening before changing the input.

Capture needs HTTPS or localhost. When the controller serves the editor over
plain HTTP on the LAN, run `npm run dev` locally and connect to that controller.
The microphone never plays through the speakers. Raw audio stays in the browser;
only derived scalar levels are sent to the controller.

## Wiring an animation

1. Preview or recall an animation with live inputs, such as `eye.py`.
2. Start listening and choose **Send music to controller**.
3. In **Live animation inputs**, select the intended output, slot and layer.
4. For Eye, leave colour on **Light colour**, choose **Music → Level** for intensity,
   and **Music → Bass** for width. Position can retain its default timer.
5. Adjust each binding's response range. Save the bindings with a preset to reuse them.

The master **Light → Brightness** in Mixer or HA continues to dim the whole
output. Music-driven intensity is additional local behavior. A different animation
can consume the same signals without any changes to audio acquisition or rendering
code. RGB inputs can select a music signal through an explicit two-colour palette.

| Signal | Meaning |
| --- | --- |
| `music.level` | RMS level |
| `music.bass` | Energy in 40–250 Hz |
| `music.mid` | Energy in 250–2,000 Hz |
| `music.treble` | Energy in 2,000–12,000 Hz, limited by sample rate |
| `music.centroid` | Spectral centre on a logarithmic frequency scale |
| `music.onset` | Energy-rise pulse with warmup and refractory interval |

All signals are floats in 0–1. Sensitivity adds gain before normalization. Raise
the noise floor to suppress room noise. Release controls decay; attacks use a
30 ms envelope. The analyzer uses a 2,048-sample Web Audio FFT and sums linear
spectral power within each band. Centroid weights frequency by amplitude. These
are visual control levels, not calibrated loudness; onset is not a BPM or beat tracker.

## Ownership and lifecycle

The browser claims the six `music.*` names as one publisher group. Each publication
contains all six values atomically, a boot ID, publisher generation and increasing
sequence. Updates run at most every 33 ms, with one request in flight and only the
newest pending frame. Ownership loss or a controller change stops publication;
reconnecting never silently reclaims it.

The adapter does not edit or restore animation bindings. **Stop sending** leaves
those bindings in place so their timeout/fallback policy applies (one second and
the declared scalar default unless configured otherwise). Continuous source values
are never saved to flash. One source can drive multiple outputs and A/B layers.

Capture continues when moving between Editor, Mixer and Audio Playground. A visible
banner provides a stop button on other routes. Hiding the browser tab, unloading
it or choosing **Stop listening** stops capture and forwarding. A pending permission
grant is safely released if capture was cancelled. Response tuning remains in
memory for this session, while presets retain the consumer mappings.

## Validation

Run `npm test` and `npm run build`. For the real native protocol integration,
compile `examples/animations/eye.py` and run:

```sh
LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm \
LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-source-native
```

The test uses isolated storage, explicitly binds Eye to generic music features,
checks rendered pixels and generation preservation, then verifies stopping leaves
the selected bindings intact.

API references: [Web Audio AnalyserNode](https://www.w3.org/TR/webaudio/#the-analysernode-interface)
and [Media Capture getUserMedia](https://w3c.github.io/mediacapture-main/#dom-mediadevices-getusermedia).
