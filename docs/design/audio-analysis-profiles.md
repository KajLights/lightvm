# Shared audio analysis and browser experiments

Status: design requirement, 27 October 2026. No analyser or service changes are
implemented by this document. It complements the
[subscription design](feature-proposal-audio-subscriptions.md).

The webservice/browser analyser must be updated to run the **same default
algorithm as the Sense**, including spectral scaling, smoothing and the adaptive
percussion onset detector. Giving different algorithms the same signal names or
adding profile labels to the current browser extractor is not sufficient.

## Current differences to remove

The webservice gateway here means LightVM's HTTP/WebSocket management ingress;
today its music publisher runs in the browser. The gateway transports features,
not raw PCM, and does not currently perform the FFT itself.

| Concern | Current browser | Current Sense / required common default |
| --- | --- | --- |
| Capture and scheduling | AudioContext sample rate; timer reads every 33 ms | 48 kHz PCM; contiguous 1,024-sample hops |
| FFT | 2,048-point AnalyserNode output | 2,048-point periodic Hann window; explicit power calibration |
| Level | RMS without explicit DC removal | DC-removed RMS |
| Spectrum | Three summary bands only | Same summaries plus configurable display spectrum, normally 16 bands |
| Onsets | Increase in clipped display level | Adaptive, percussion-biased spectral novelty |
| Occurrences | Decaying `music.onset` scalar only | Envelope plus timestamped, identified onset events |
| Publication | Up to about 30 updates/s | Latest analysis every five hops, 9.375 updates/s |

The browser's AnalyserNode has specified Blackman windowing and FFT scaling;
disabling its smoothing does not turn it into the Sense's Hann transform.
Use captured PCM and the common DSP implementation for the default profile.
See the [Web Audio FFT definition](https://www.w3.org/TR/webaudio/#fft-windowing-and-smoothing-over-time).

## One implementation, several hosts

Extract kaj-fi's portable `main/features.[ch]` and `main/onset.[ch]` behind a small
PCM-in/features-out library interface. Keep acquisition, clocks, transport and
platform FFT adapters outside it. Maintain one versioned source of truth in
kaj-fi; pin that dependency in LightVM rather than copying the detector into
TypeScript. This does not put audio analysis in the Pico lighting firmware.

- Sense: native C core with its optimized FFT adapter.
- Browser: the same C core compiled to WebAssembly, driven by an AudioWorklet
  PCM adapter. Initialize memory/FFT tables before capture; keep network calls,
  allocation and UI work outside the audio callback. Bound all handoff queues.
- Mac or Raspberry Pi service: native core with an audio-device/file adapter and
  the same subscription publisher interface. No ESP-IDF or LightVM dependency
  is required for the portable core.

AudioWorklet supports custom JavaScript/WebAssembly processing on the audio
rendering thread; callback block sizes must be accumulated into analysis hops,
not mistaken for the hop size. The UI may redraw at a different rate without
changing detector time. [Web Audio AudioWorklet](https://www.w3.org/TR/webaudio/#AudioWorklet)

Capture adapters select a channel or explicitly average channels, resample to
48 kHz when necessary, and feed the same signed-16-bit PCM contract used by the
Sense. Define clipping and rounding once in that adapter contract and include
them in fixtures. Disable speech processing where supported; report when capture
processing cannot be disabled. A fixed PCM fixture bypasses device processing
and resampling when testing DSP parity. Actual microphones need not give equal
values for the same room sound: position, gain and frequency response differ.

Timestamp the end of the captured analysis window in the publisher's monotonic
microsecond clock. Include capture/resampler delay when mapping sample positions
to that clock; do not stamp a delayed callback or a queued frame as fresh audio.
Gaps, device changes and suspend/resume reset DSP history and the stream session.
No callback or UI timer may silently turn a gap into contiguous samples.

## Profile 1: adaptive percussion, version 1

`analysis_profile = 1`, display label **Adaptive percussion v1**, is the required
default for the updated browser, Sense and host services. It specifies the whole
feature interpretation, including the detector, rather than only a wire version.

- Mono 48 kHz, window 2,048, hop 1,024; subtract the window mean. RMS uses the
  unwindowed, DC-removed samples. FFT uses a periodic Hann window and an unscaled,
  natural-order transform. One-sided power scale is `2 / (N * sum(window²))`.
- Sum linear FFT power for bass [40,250), mid [250,2000), treble [2000,12000) Hz,
  using bin centres in those intervals (bins 2..511 in total). Normalize power
  `P` as `clamp((10*log10(max(P,1e-20)) + gain_db - floor_db) / (-6-floor_db), 0, 1)`.
  Display bands use the same integrated-power calculation, never sums of already
  normalized levels. Their exact transmitted edges are authoritative.
- Centroid uses amplitude-weighted frequency, then
  `clamp(log2(max(100, centroid_hz)/100)/log2(80), 0, 1)`. Gate summary bands,
  centroid and display bands to zero when the normalized unsmoothed level is zero.
- Level, bands and centroid use attack/release envelopes at **analysis-hop time**:
  `alpha = 1-exp(-hop_seconds/tau_seconds)`. Values below `1e-5` settle to zero.
- Use the Sense's 32-band adaptive spectral novelty detector, including peak
  whitening, frequency-neighbour suppression, median/MAD threshold, percussion
  gate, background/context gates, 500 ms warmup, rearming and refractory period.
  Its reference definition and regression cases are in kaj-fi's
  [onset design](https://github.com/KajLights/kaj-fi/blob/main/docs/onset-detection.md)
  and `main/onset.c`; do not replace it with a display-level delta.
- `onset` is a float envelope: one on detection, 160 ms exponential decay.
  An occurrence separately has an ID, capture time and threshold-excess strength.
  Consumer `onsets` is the per-invocation integer counter described in the
  subscription design. A pulse value is never treated as an event ID or a beat clock.
- Export the six existing floats and normally 16 spectrum bands; the supported
  0..64 band configurations retain their exact bin intervals. Settings and layout
  are fixed within a stream session. Publish latest snapshots at 9.375 Hz; faster
  local browser plots may show every analysis hop without increasing network rate.

The reference parameter set is `gain_db=0`, `floor_db=-50`, `attack_ms=30`,
`release_ms=220`, `band_count=16`, `onset_threshold=0.08`,
`onset_refractory_ms=200`. Existing live Sense tuning can be different. All hosts
expose the same names, units, valid ranges and defaults from the common library.
Diagnostics and browser comparison exports include the complete parameter set,
algorithm/library revision and capture processing settings. Equal profile IDs
mean equal algorithms; numerical comparison additionally requires equal tuning
and PCM. Tuning changes reset history and start a new session.

The spectrum example's automatic brightness levelling remains a **consumer**
operation. Do not add another AGC to one publisher or level each spectral band
independently. Detector whitening/context adaptation remains inside the common
detector. Keep source gain, detector sensitivity and effect brightness distinct.

## Selectable algorithms and reproducible browser comparisons

The browser exposes an algorithm selector. Profile 1 is the default. Alternative
detectors are deliberate experiments with their own stable numeric profile ID,
versioned label, parameter schema and fixtures. ID zero is invalid; IDs are never
reused for changed semantics. Do not label an experimental algorithm as profile 1.
Registry additions live here and in a future generated manifest shared by
publishers and receivers. No additional numeric ID is assigned until its contract
is defined. Each registry entry specifies required fields/bands, sample geometry,
publication cadence, event semantics and parameter ranges; implementations
advertise the entries they actually support. Unknown IDs remain unavailable.

Initially vary the onset detector over the common PCM/FFT/level pipeline. This
makes the effect of an algorithm change visible without changing spectral scale
at the same time. A later alternative that changes other analysis stages needs
a profile describing those differences. A simple level-rise detector may be an
explicit comparison baseline; it is not a fallback when the common core fails.

Allow two algorithms to consume identical live PCM or a locally selected fixture
with separate histories. Show level/spectrum, novelty and threshold where
available, onset marks/strength and timing on the same timeline. Reset both at a
defined sample boundary, hold calibration equal, and export algorithm IDs,
parameters, input identity/hash, event times and results for repeatable checks.
Replaying the same fixture on a computer should be sufficient for most algorithm
iteration; only final performance/acoustic checks need the Sense.

Comparison is local by default. Forwarding chooses one result explicitly, or
uses distinct named gateway sources for both. Algorithm switches create a new
session and publisher generation, clear pending data/events and warm up again;
receivers reject mismatched expected profiles instead of silently changing an
animation's semantics. Merely opening a comparison must not retune the Sense.
Experimental browser profiles need not be compiled into microcontroller builds.

## Conformance before claiming equivalence

Run one committed, independently generated PCM fixture corpus through native C,
WASM and the S3 FFT adapter with identical hop boundaries and settings. Check:

- Silence, DC, bin-centred/off-bin tones, sweeps, gain steps, clipping and gaps;
  identical band edges and finite, bounded levels.
- The existing kick/snare, strings, vibrato, whisper and music-stop onset cases
  over the tested dynamic range. Check occurrence count/order, rearming and no
  events on reset or silence; compare per-hop output before transport decimation.
- Initial parity acceptance: level/band/envelope absolute error at most `1e-4`,
  centroid at most `1e-3`, identical occurrence counts and event hops for fixtures
  separated from decision thresholds. Add threshold-boundary cases separately;
  report numerical disagreements rather than loosening all tolerances to hide them.
- 44.1/48/96 kHz adapters, irregular callback blocks and main-thread stalls. Test
  resampler calibration and timestamp delay separately from identical-PCM parity.
- Gateway and UDP delivery of the same analysis: equal values and event identity
  after deduplication despite loss, repeated packets, delayed messages and reconnect.

These are planned acceptance criteria, not results from the current browser.
The current browser extractor does **not** yet conform to profile 1.
