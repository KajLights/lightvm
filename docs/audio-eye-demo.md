# Live audio Eye demo

Open **Audio demo** in the browser workspace, or visit `/#/audio`. It works
without a controller: **Start listening**, grant microphone access, and watch
the meters and schematic strip preview. Choose a microphone, line-in audio
interface or OS loopback device. Available device names appear after the first
permission grant; stop listening before changing the input.

Capture needs HTTPS or localhost. When the controller serves the editor over
plain HTTP on the LAN, run `npm run dev` locally and connect to that controller
from the local editor. The microphone is never routed to the speakers. Raw audio
stays in the browser; nothing is recorded or uploaded.

The default mapping is:

| Eye input | Audio signal | Output range |
|---|---|---|
| Position | Spectral centroid, normalized on a log frequency scale | 0.05–0.95 |
| Width | Bass energy | 0.04–0.40 of the strip |
| Hue | Treble energy | 0–0.85 turns |
| Brightness | RMS level | 0–1 |

Each input can select any of the six signals and its own output range. Reverse
the range ends to invert the response; equal ends provide a constant value.
Sensitivity adds gain before normalization. Raise the noise floor if the room
noise keeps the Eye bright. Release sets the decay time; attacks use a 30 ms
envelope. These are visual control levels, not calibrated loudness measurements.

The analyzer uses a 2,048-sample Web Audio FFT, with bass at 40–250 Hz, midrange
at 250–2,000 Hz and treble at 2,000–12,000 Hz (limited by the input sample rate).
It sums linear spectral power within each band before normalizing. Spectral
centroid weights frequency by amplitude. The onset signal is an energy-rise
pulse with warmup and a refractory interval. It is not a beat tracker, BPM
estimate or musical phase. Native Web Audio is sufficient for this demo;
The separate Meyda page supplies richer signals through the same Eye input
contract. A dedicated rhythm analyzer could add beat tracking later.

## Meyda comparison

Choose **Meyda comparison** beside **Web Audio**, or open `/#/meyda`. This page
uses the pinned Meyda 5.6.3 package. A single microphone capture supplies both
analyzers about 30 times per second. The main Eye and its input mappings use
Meyda, while the comparison table and reference Eye show the native adapter's
response to the same captured sound. Both paths share the same sensitivity,
noise floor, smoothing, band boundaries and level-rise onset detector.

Meyda also exposes these normalized mapping signals:

| Signal | Normalization |
|---|---|
| Spectral flatness | Meyda's 0–1 flatness |
| Spectral roll-off | 99% magnitude roll-off frequency / Nyquist frequency |
| Zero-crossing rate | Crossings / (frame sample count − 1) |

The page shows raw RMS, centroid and roll-off in Hz, flatness, crossings per
frame, Meyda loudness, twelve chroma pitch classes and 24 Bark loudness bands.
Chroma values indicate relative pitch-class strengths; they do not identify a
key. Bark bar heights are relative to the strongest current band; the raw total
loudness is in Meyda units and is not calibrated SPL or LUFS.

Extraction calls Meyda's synchronous `extract()` on the same 2,048 PCM samples
captured for the native analyzer. It does not use Meyda's ScriptProcessor-based
real-time wrapper. A local Meyda instance owns its sample rate and cached
filters. Both FFTs use Blackman windows, and the adapter divides Meyda's FFT
magnitudes by the buffer length to match Web Audio's scaling. RMS uses Meyda's
extractor on unwindowed PCM so both level controls agree. Small window and
sampling differences remain; Meyda's centroid also includes the full spectrum,
where the native adapter considers 40–12,000 Hz. Timings show main-thread
feature work; the browser performs the native FFT separately.

The reference Eye is shown when all four mappings use shared signals. When a
mapping uses flatness, roll-off or zero crossings, the table still compares the
six shared signals. Both pages can drive the running controller Eye. Switching
pages stops capture and releases controller input ownership; common response
controls and mappings remain while the component stays open. Meyda-specific
mappings return to defaults when switching to Web Audio.

To drive physical lights:

1. Install firmware implementing live animation inputs and compile the matching
   `eye.py` example from **Develop**. Preview it on the selected output.
2. Return to **Audio demo**, start listening, and select **Drive running Eye**.
3. Adjust the mappings while sound plays. **Release Eye** restores its previous
   input bindings if they have not been changed elsewhere.

The adapter discovers the running animation's actual input declarations. It
requires float inputs named `position`, `width`, `hue` and `brightness`, each
accepting 0–1. It claims four named sources, `audio.eye.position`,
`audio.eye.width`, `audio.eye.hue` and `audio.eye.brightness`, then binds the
specific output, A/B slot, layer and program generation. Source samples use the
returned publisher generation, controller boot ID and an increasing sequence.
One publication contains all four values atomically. Updates run at most once
every 33 ms, with one request in flight and one newest pending frame.

Stopping, navigating away, hiding the page, changing controller/output/instance
or losing source ownership stops forwarding. Stopping capture also releases
the microphone, including permission grants that arrive after cancellation.
Restoration uses the binding revision from takeover, so it cannot overwrite a
later manual edit. A controller that loses the browser's publications falls back
to the Eye's declared values after one second. Start listening and take control
again explicitly after returning to the page.

The repository includes the live input core and canonical `eye.py` example.
This demo is a browser adapter for the same protocol; local preview and capture
also work without a controller. A connected Pico must run matching firmware
that reports `live_inputs: true`.

Run `npm test` and `npm run build` for browser checks. To also exercise the
adapter against the companion native core, compile its Eye example and run:

```sh
LIGHTVM_AUDIO_RECEIVER=/path/to/lightvm \
LIGHTVM_AUDIO_EYE=/path/to/eye.lbc npm test -- audio-eye-native
```

That integration test publishes through the real protocol, verifies a lit
pixel snapshot, preserves the program generation and restores the original
bindings. It uses an isolated temporary controller store.

API references: [Web Audio AnalyserNode](https://www.w3.org/TR/webaudio/#the-analysernode-interface)
and [Media Capture getUserMedia](https://w3c.github.io/mediacapture-main/#dom-mediadevices-getusermedia).
Meyda references: [feature definitions](https://meyda.js.org/audio-features)
and [source and API](https://github.com/meyda/meyda).
