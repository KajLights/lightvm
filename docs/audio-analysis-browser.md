# Browser audio analysis

Audio Playground consumes PCM through an input-only AudioWorklet and runs the
pinned kaj-fi C analyser compiled to WASM. Profile 1 is **Adaptive percussion v1**;
profile 2 is the explicit **Level rise baseline v1**. Both run over identical PCM
with independent histories. The browser has no second detector implementation.

## Build and run

Install Node dependencies and Emscripten 4.0.7, then:

```sh
python3 tools/fetch_audio_core.py
source /path/to/emsdk/emsdk_env.sh
npm run audio:build
npm run build
python3 tools/serve_editor.py
```

`KAJ_AUDIO_CORE=/path/to/kaj-fi/core` can select an existing clean checkout. Its
HEAD must equal `kaj-audio.rev`. The build rejects dirty core/build inputs and
copies the common parameter/profile manifest alongside the module. Vite rejects
missing or stale WASM assets rather than shipping a different analyser. Unit tests
can run without generated assets; the browser conformance check requires them.
The `audio-web` CI job builds and packages the editor and runs silent conformance.

## Capture, clocks and bounds

Microphone capture requires HTTPS/localhost, AudioWorklet, and AudioContext output
timestamp/latency support. It requests echo cancellation, noise suppression and
automatic gain control off, and reports the actual settings (including unreported
values). Choose a channel or explicitly average all channels. Source gain and
onset threshold are separate controls with the common manifest's units and ranges.
Calibration edits require a new capture session. No node connects to speakers.

The adapter accumulates callback blocks into 1,024-sample hops at 48 kHz. Other
input rates use a preinitialized 64-tap/1,024-phase windowed-sinc resampler. Float
samples use the shared clipping/rounding contract; 48 kHz signed-16 fixture input
round-trips without resampling. FFT/WASM memory and FIR/history buffers are ready
before input is connected. Each profile publishes every fifth ready hop (9.375 Hz).

Capture time comes from sample position, correlated to `performance.now()` with
`getOutputTimestamp()`, subtracting reported output/base latency, reported input
latency and a conservative guard of at least 25 ms. The clock must stabilize after
worklet initialization before capture begins. It stays fixed within a stream;
callback delivery time never refreshes queued PCM. FIR lookahead remains visible
as age because timestamps refer to the captured sample centres. Unreported device
input latency is explicitly recorded; room microphones are not numerical parity
fixtures. See the [Web Audio clock definition](https://www.w3.org/TR/webaudio/#dom-audiocontext-getoutputtimestamp).

Discontinuous sample positions, missing input, channel-count changes and explicit
reset clear histories and event state. A clock step outside the guard resets the
stream; context suspension or track removal stops capture and requires a fresh
start. Switching the selected algorithm releases forwarding and resets both
profiles. New epochs get new session IDs.

There is one transferable worklet mailbox: DSP and history continue if the UI
stalls, while the mailbox supplies the latest metrics and eligible events when
returned. The fixed WASM heap is 1 MiB. History holds 3,000 hops for both profiles
(7.68 MB of preallocated doubles, about 64 seconds); reaching that bound marks the
export truncated. Export copying and JSON conversion run outside `process()`.
History truncation does not stop live analysis. No queues grow with capture time.

## Comparison and forwarding

Choose a local PCM16/float32 WAV or mono 48 kHz signed-16 little-endian `.pcm`
fixture, up to 32 MiB and 60 seconds. Replay is silent. Explicit sample boundaries
exclude graph pre-roll and post-end silence. Local plots show both algorithms on
the same 2.5-second capture timeline with spectrum, level, novelty, threshold and
identified onset marks/strength. Export retains every ready hop, even when the UI
was stalled, plus profile/source/session IDs, parameters, dependency revision,
input hash/settings and timing metadata. Source identities remain stable within a
capture; a reset starts a new session and clears the exported history.

Comparison is local until **Send music to controller**. Send the selected profile
under one gateway name, or both under distinct `_1`/`_2` names. The publisher uses
[structured gateway ownership and clock challenges](audio-gateway.md), one request
in flight, one newest pending snapshot, and at most three repeats per eligible
event. It drops stale data and stops on ownership loss. Stop sending releases the
claim without stopping local analysis or editing animation bindings. Fixture
streams are synthetic; consuming scenes must explicitly allow them. Merely opening
the comparison never changes Sense configuration.

## Reproducible checks

```sh
npm test
npx playwright install chromium
npm run test:audio-browser
```

`CHROME_EXECUTABLE` can select an installed Chromium/Chrome. The check uses a local
Vite server and a silent 12.8-second committed corpus. It independently compiles
the native C core, compares both profiles against WASM at every hop, then runs the
actual AudioWorklet with a 500 ms main-thread stall. It checks event identity and
timing, exact band edges, fixed memory, irregular callbacks, gap reset, UI export,
desktop/mobile layout, and zero speaker connections. It saves a concise report
and screenshots in `build/`. Adapter unit tests independently cover calibrated
44.1/48/96 kHz conversion, alias suppression and timestamp delay.

The [27 October result](validation/audio-browser-2026-10-27.json) contains 599 ready
hops per profile, native metric error at most `1.1920929e-7`, zero AudioWorklet/WASM
error and identical events. This does not establish physical S3 optimized-FFT
parity, acoustic equivalence, threshold-boundary behavior or full UDP/gateway
delivery equivalence. Those remain separate acceptance work.
