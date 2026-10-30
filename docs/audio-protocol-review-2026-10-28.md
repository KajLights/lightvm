# Audio analysis, communication and OSC review — 28 October 2026

Status: review completed; documentation refreshed after worktree consolidation.
This record recommends follow-up work. It does not change the implementation,
publication rate, deployed firmware or device configuration.

## Scope and revision correction

The completed review used LightVM `e3985c0583f78764192221373282da224816a0bd`
and kaj-fi `973491c4500d45471d1fc63aa3fadefb87eb15d5`. The documentation refresh
checked merged LightVM `4a3cfa3`, which contains that audio implementation; the
reviewed source registry, input counters, browser session and PCM publication
code are unchanged by consolidation. LightVM pins the consumed kaj-fi core to
`a8e536a96cd5722a71564d02c0bd9202799aa25b` with verified source hashes.

An initial pass used older LightVM checkout `dad07b7`. Its findings about a v4
receiver, missing receiver capture-age checks, the old browser detector/scalar
forwarding path, sixteen exposed spectrum bands and global event-counter limits
were superseded by the newer implementation. They are not outstanding defects
in the merged code. The current paths provide:

- LVAF v5 records and LVAS v1 subscriptions, with confirmed sessions and clock bounds.
- One portable analyser for Sense/native/WASM, with explicit profile selection.
- Structured browser frames retaining capture time and occurrence identity,
  bounded pending work, stale-frame rejection and explicit claim release.
- Scene-local aliases, all 64 bands, four shared provider slots and independent
  per-binding occurrence counters, including pause/resume baselines.

See the [subscription guide](audio-subscriptions.md),
[gateway contract](audio-gateway.md), [browser analysis](audio-analysis-browser.md)
and [acceptance matrix](audio-acceptance.md).

The external comparison inspected Realtime_PyAudio_FFT's Python implementation at
[`3f583111d418a4ec1441f01b2e4b299b39e346d4`](https://github.com/aiXander/Realtime_PyAudio_FFT/tree/3f583111d418a4ec1441f01b2e4b299b39e346d4)
and its Rust OSC implementation at
[`bbc199359309470c18ad8400d5bbb2a1ddafd692`](https://github.com/aiXander/Realtime_PyAudio_FFT/blob/bbc199359309470c18ad8400d5bbb2a1ddafd692/engine/server/src/osc.rs).
The comparison concerns that project's actual OSC messages, not every possible
application built with OSC. No performance benchmark of that application was run.

## Recommendation

Retain LVAF/LVAS for embedded feature delivery. Its bounded state, explicit
identity/session admission, freshness and event semantics suit the current
controllers. Consider an OSC adapter when interoperability with external music
or visual tools is useful. Replacing the wire encoding alone would not reproduce
the reference project's response or eliminate latency caused by publication
cadence, analysis windows, envelopes, Wi-Fi or rendering.

OSC can carry a richer application schema, including identities and timestamps.
Doing so would still require defining the same audio semantics and receiver
policies; the reference's address vocabulary does not supply those guarantees.
No OSC adapter has been implemented or selected as required work by this review.

## Protocol comparison

| Concern | LightVM / kaj-fi | Reference project's OSC path |
| --- | --- | --- |
| Features | Six metrics, 0–64 bands with exact edges, identified onset occurrences | `/audio/lmh`, `/audio/fft`, three per-band onset addresses, `/audio/bpm`, `/audio/meta` |
| Transport | Leased unicast UDP; browser uses structured command ingress | Configured UDP destinations; independent messages |
| Identity and ordering | Source, session, publication sequence and receiver lease; gateway ownership generation | No source/session/event identity or sequence in the OSC payloads |
| Freshness | Capture/send times, bounded monotonic clock mapping, 350 ms metric/event ceiling | No capture timestamps or age policy carried by these messages |
| Occurrences | Up to three successful local submissions per receiver before expiry; bounded deduplication | One integer `1` pulse on detection, with no event identity or repetition |
| Coherence | One atomic six-feature/spectrum/event record | Separate LMH, BPM, FFT and onset packets; no OSC bundles/timetags |
| Size, excluding UDP/IP/link overhead | `92 + 8 × bands + 24 × events`; 220 bytes for 16 bands/no events, maximum 700 | LMH 32 bytes; BPM 20; onset 28; 128-bin FFT 656 |

These sizes carry different information and are not equivalent-payload benchmarks.
The reference's [sender](https://github.com/aiXander/Realtime_PyAudio_FFT/blob/3f583111d418a4ec1441f01b2e4b299b39e346d4/server/io/osc_sender.py)
uses preallocated buffers and nonblocking UDP, so generic claims that OSC requires
heavy serialization are not supported by this implementation. Its FFT packet is
`12 + round_up_to_4(n + 2) + 4n` bytes: at 512 bins it is 2,576 bytes, above a
common 1,472-byte IPv4 UDP payload budget. Such sizes risk fragmentation or send
failure depending on the path. See [RFC 8085 section 3.2](https://www.rfc-editor.org/rfc/rfc8085.html#section-3.2).

The [OSC 1.0 specification](https://opensoundcontrol.stanford.edu/spec-1_0.html)
supports bundles and timetags but does not provide clock synchronization or
standard audio feature meanings. Neither the reference's unbundled messages nor
LightVM's immediate latest-snapshot playback guarantees synchronized presentation
across controllers. A separate synchronization requirement would need its own
measurement and design; this review does not recommend adding a playback buffer.

LVAF's repeated events remain best effort: a successful local send is not an
acknowledgement of delivery. Source pinning and return-route confirmation are
trusted-LAN controls, not authentication. The 350 ms ceiling bounds accepted
timestamp age, including clock uncertainty; it is neither intentional delay nor
a guarantee of acoustic-to-light response time.

## Analysis differences matter independently of transport

Our analyser uses 48 kHz mono PCM, a 2,048-sample periodic Hann window and
1,024-sample hops. Display bands integrate calibrated power and map it into
bounded 0–1 values using configured gain/floor and attack/release. Profile 1
detects percussion-biased spectral novelty before display clipping; profile 2
is the explicit level-rise comparison. The shared core runs outside the lighting
controller, which consumes features through ordinary INPUT bindings.

The reference's
[example configuration](https://github.com/aiXander/Realtime_PyAudio_FFT/blob/3f583111d418a4ec1441f01b2e4b299b39e346d4/configs/main.example.yaml)
uses 256-sample audio blocks, a 1,024-sample FFT window and 512-sample FFT hops,
with 128 log bands. At 48 kHz these imply 187.5 level updates/s and 93.75 FFT
updates/s. Its low/mid/high filters use different ranges and envelopes; adaptive
scaling, per-band onset detection and tempo estimation also differ. These are
configuration-derived rates, not measured network throughput or latency.

The reference's [publication layer](https://github.com/aiXander/Realtime_PyAudio_FFT/blob/3f583111d418a4ec1441f01b2e4b299b39e346d4/server/io/osc_publisher.py)
sends levels/BPM per audio block and FFT per FFT hop. `/audio/fft` can contain raw
dB or processed values depending on configuration, and gain can raise processed
values above one. That choice is not identified in the FFT message. An importer
must define units, scaling, spectrum interpretation and event semantics explicitly;
it must not relabel the external detector as our analysis profile 1.

## Timing finding and proposed experiment

On 26 October the requested multicast workaround was 5–10 measurements/s. Sending
every fifth hop gives 9.375 Hz while detection remains at 46.875 Hz. The proposed
extra 100 ms receiver jitter delay was never implemented. The subsequent unicast
A/B test held cadence, analysis and animation constant to isolate transport.
The [network record](https://github.com/KajLights/kaj-fi/blob/main/docs/network-validation.md)
explicitly says no attempt was made to raise publication rate.

Current [native publication](https://github.com/KajLights/kaj-fi/blob/a8e536a96cd5722a71564d02c0bd9202799aa25b/core/publication.c)
and [browser publication](../web/audio/pcm-adapter.mjs) retain five-hop decimation.
Updates are 106.67 ms apart. An onset detected on an intervening hop waits
0–4 hops, up to 85.33 ms, before the next scheduled publication. This is additional
to capture/window effects, analysis envelopes, network transit and rendering.
The current rate is not evidence of a fundamental unicast or LVAF limit.

Proposed next experiment, not executed by this review:

1. Compare the existing 9.375 Hz baseline with every two hops (23.4375 Hz) and
   every hop (46.875 Hz), keeping PCM, detector settings, rendering and payload
   layout fixed. Coordinate native/browser cadence and its documented contract.
2. Measure one and multiple receivers, then repeat at 0/16/64 bands. Separate
   physical Wi-Fi evidence from simulated host peers.
3. Record capture-to-send age, receiver clock uncertainty/age rejections,
   arrival spacing, coalescing, gaps, event misses/duplicates, send pressure,
   command response, DSP/render budget and memory. Sequence gaps alone do not
   distinguish producer overwrites from network loss.
4. Keep the freshest snapshot and bounded occurrence history. Do not add an
   intentional jitter queue or replay delayed metric frames. If cadence changes,
   revisit event repetition spacing and TTL tests as part of that change.

For scale only, a 220-byte record at 46.875 Hz is approximately 10.3 kB/s per
receiver before transport overhead. That arithmetic does not prove sustainable
Wi-Fi behavior, especially with multiple destinations.

## Numerical boundary and diagnostic limitations

The [ordinary corpus and physical S3 checks](audio-acceptance.md) pass their
documented numerical/event criteria. In the separate
[14-case threshold report](validation/audio-boundaries-2026-10-27.json), native
and S3 agree throughout. WASM produces one extra profile-1 onset at native
threshold equality: its score is `2.9802322387695312e-8` higher, giving strength
`1.1267714938867357e-7` instead of zero. The occurrence also starts the onset
envelope, so the visible difference is not limited to a tiny scalar error.
This known boundary exception does not justify claiming universal identical
event decisions across FFT backends. Preserve it in acceptance evidence; any
decision to enforce exact boundary parity needs a separately reviewed detector
and conformance change. Different microphones have no acoustic-equivalence claim.

The documentation refresh also checked kaj-fi's diagnostic listener. It rejects
stale incoming records but prints its last accepted metrics during a gap until
session/lease reset. Printed values alone are not a freshness indication.
LightVM independently expires source availability as capture age advances.
The current listener behavior is now documented; explicitly displaying cached
versus live status would be a separate diagnostic improvement.

## Finding disposition and verification

| Finding or recommendation | Disposition after consolidation |
| --- | --- |
| Older receiver/browser/scoping gaps from the initial checkout | Superseded by the implemented subscription/shared-analysis work; not open findings |
| Contradictory multicast/v4/pending-integration documentation | Corrected in current kaj-fi and LightVM guides; dated experiments remain historical |
| Higher-rate unicast publication | Open measurement proposal; cadence unchanged |
| Native/WASM threshold equality | Known, explicitly recorded exception; no detector change |
| OSC interoperability | Optional adapter recommendation; no replacement or adapter implemented |
| Diagnostic cached metrics during gaps | Documented limitation; optional live-status improvement remains open |

Fresh review checks against `e3985c0` passed 30 native CTest contracts with
AddressSanitizer/UBSan in the host/audio/HTTP/WebSocket configuration, 19 focused
Python UDP/gateway/dependency checks, and 25 browser-side unit/native-integration
tests. Loopback publishers were simulations. These checks are separate from the
27 October acceptance report's 33 native contracts and broader test matrix;
different build configurations must not be conflated.

No hardware was exercised during this review. Physical claims refer only to
the recorded 27 October checks, not a new test or extended stress certificate.
The post-merge documentation update checks commands, links and current source;
it does not claim to rerun firmware acceptance on the consolidated tree.
