# ESP32-S3 audio analyser and multicast inputs

Status: Proposed

Date: 19 October 2026

Build a standalone ESP32-S3 audio analyser that publishes six audio features over
UDP multicast. LightVM controllers subscribe to the stream and expose its values
through the existing live-input registry. Each animation chooses its own mapping
from sound to movement, colour, brightness or transition progress.

Receivers track the sender's elapsed clock using incoming packets and maintain a
100 ms jitter buffer. This smooths variations in arrival time and tracks gradual
clock drift. The estimated offset includes the receiver's normal network delay:
different receivers can retain different baseline offsets. Precise clock
synchronization, two-way timing exchanges and compensation for that baseline
delay are outside the first implementation.

The existing [browser audio demo](../audio-eye-demo.md) and
[live-input core](../inputs.md) provide the feature semantics and consumer model.
Microcontroller capture, multicast publication, clock tracking and buffered
reception are new native components proposed here.

## Architecture

```mermaid
flowchart LR
    M[Digital microphone or line input ADC] --> A[ESP32-S3 capture and analysis]
    A --> U[UDP multicast feature frames]
    U --> R1[LightVM clock tracker and jitter buffer]
    U --> R2[Another LightVM receiver]
    R1 --> S[Six named input sources]
    S --> B[Per animation input bindings]
    B --> P[Animation render snapshots]
```

The analyser runs independently of a browser and of the light controllers. It
transmits derived features; raw audio stays on the analyser. Controllers can join
an existing stream without the analyser maintaining a subscriber list. Each
receiver initially selects one configured sensor, consuming six of the current
sixteen controller-wide source slots. One subscription can feed many animation
instances and transition progress.

## Hardware and capture

Use an ESP32-S3 with native C/C++, ESP-IDF audio capture and ESP-DSP processing.
The S3 supplies Wi-Fi, floating-point support and DMA-backed audio input. Its
dual-core processor and 512 KB internal SRAM make this a practical workload;
measure the complete capture, analysis and networking budget on the chosen board.
See [Espressif's device specifications](https://www.espressif.com/en/products/socs/esp32-s3)
and [I2S driver](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/peripherals/i2s.html).

| Input | Proposed hardware | Considerations |
| --- | --- | --- |
| Integrated microphone | XIAO ESP32S3 Sense with its digital PDM microphone | Convenient first prototype; configure hardware PDM conversion and validate the chosen capture rate. |
| External microphone | ESP32-S3 development board and an I2S MEMS microphone breakout | Allows microphone placement; requires power, ground, bit clock, word-select and data, plus channel selection where applicable. |
| Direct music feed | Mixer/player line output into an I2S audio ADC module | More consistent music measurement without room noise; select input conditioning, supplies and clocking appropriate to the ADC. |

Seeed documents the [Sense microphone](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/).
An external example is the [SPH0645 breakout](https://www.adafruit.com/product/3421),
whose quoted response begins around 50 Hz. Deep-bass performance requires checking
the microphone and enclosure response. A [PCM1808-based ADC](https://www.ti.com/product/PCM1808)
is one candidate for line input; its analog supply and input requirements must be
handled by the selected module. Feed it a correctly conditioned line signal.

Capture the waveform rather than a threshold output from a basic sound sensor.
Use fixed gain initially so automatic gain control does not flatten musical
dynamics. A mono microphone is sufficient. For stereo line input, initially mix
the digitized channels to mono with headroom; phase cancellation is a known
limitation of that simple downmix.

Proposed analysis parameters:

| Parameter | Initial value |
| --- | --- |
| Capture rate | 48,000 samples/s |
| Analysis window | 2,048 mono samples, about 42.67 ms |
| Hop size | 1,024 samples, about 21.33 ms |
| Feature publication | One frame per hop, nominally 46.875 frames/s |
| Buffer allocation | Fixed DMA, window, FFT and network buffers at startup |

The 48 kHz rate preserves the current upper analysis bound of 12 kHz. Validate
that rate on the actual microphone and driver; the Sense tutorial's 16 kHz example
does not establish 48 kHz operation. A lower-rate experiment must explicitly
reduce the treble bound and document its changed response.

DMA captures continuously. An analysis task removes DC, applies a window,
calculates the spectrum and features, and hands the newest completed frame to
networking. Keep FFT work out of interrupt callbacks. Bound processing to the hop
budget; if analysis overruns, discard obsolete work and report the discontinuity
rather than sending a delayed backlog. After Wi-Fi reconnects, resume with current
features instead of replaying frames generated while disconnected.

## Feature contract

Publish the following binary32 floats, each finite and in the inclusive range
0..1. These are visual control values, not calibrated acoustic measurements.

| Feature | Meaning |
| --- | --- |
| `level` | Normalized RMS amplitude of the audio window |
| `bass` | Normalized energy in 40 Hz <= frequency < 250 Hz |
| `mid` | Normalized energy in 250 Hz <= frequency < 2,000 Hz |
| `treble` | Normalized energy in 2,000 Hz <= frequency < 12,000 Hz |
| `centroid` | Amplitude-weighted spectral centroid, mapped onto a logarithmic 0..1 scale |
| `onset` | Decaying energy-rise pulse with warmup and a refractory interval |

Follow [the current extractor](../../web/src/audio-features.ts): sum linear power
within each band before converting to its normalized level, gate silence, and
use elapsed-time attack/release smoothing. Start with its 30 ms attack, 220 ms
release and -50 dB floor, with configurable sensitivity. Port its centroid and
onset semantics, including the distinction between a transient and a predicted
musical beat. Match FFT/window scaling using shared synthetic signal fixtures;
matching FFT size alone does not reproduce Web Audio's numerical response.

Continue sending complete frames during silence. Silence is valid zero-valued
state; a missing stream is a separate condition. `onset` remains scalar state,
with no guarantee that every pulse triggers an action after packet loss or a
slow render. Reliable events and BPM/beat-phase tracking are later extensions.

## Multicast transport and frame format

Use IPv4 UDP multicast on the local installation network, with TTL 1. A proposed
configurable group is `239.255.42.99`, within the
[IPv4 Local Scope](https://www.rfc-editor.org/rfc/rfc2365.html#section-6.2),
and an example configurable UDP port is `49152`. Keep feature transport separate
from mDNS discovery and available without enabling HTTP or MQTT.

Receivers explicitly join the configured group on the active network interface
and rejoin after reconnecting. Select the sensor by its stable configured ID;
do not follow whichever sender happened to transmit last. This follows LightVM's
trusted-LAN model: sensor identity and session fields prevent accidental mixing
and stale playback, but do not authenticate packets. Multicast delivery depends
on the access point and network configuration; validate it on the installation's
actual Wi-Fi. See [wireless multicast considerations](https://www.rfc-editor.org/rfc/rfc9119.html).

Use one fixed 72-byte datagram per frame. The proposed first wire format is:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | Magic ASCII `LVAF` |
| 4 | 1 | Format version, initially 1 |
| 5 | 1 | Feature count, exactly 6 |
| 6 | 2 | Reserved, zero |
| 8 | 8 | Stable nonzero `sensor_id` |
| 16 | 8 | Nonzero `session_id`, newly randomized whenever the analyser stream restarts |
| 24 | 8 | `sequence`, increasing from 1 within the session |
| 32 | 8 | `audio_time_us`, end of the captured analysis window |
| 40 | 8 | `send_time_us`, sampled immediately before submitting this datagram to UDP |
| 48 | 24 | Six IEEE 754 binary32 values in the order listed above |

Encode integers and float bit patterns in network byte order. Decode fields
explicitly rather than transmitting or casting a C structure. Reject the entire
datagram on a length, version, reserved-field, identity, timestamp or feature
validation failure. Neither counters nor timestamps wrap within a session.

Both timestamps use the same sender monotonic clock in microseconds, without an
epoch or time of day. Derive audio timestamps from captured sample positions
anchored to that clock, independently of when the processing task wakes up.
Require `audio_time_us <= send_time_us`; sequence, audio time and send time must
advance together in stream order. The separate send timestamp lets the receiver
estimate arrival timing without treating varying analysis duration as network
delay. Wi-Fi and driver queuing after submission remain part of transport delay.

At the nominal frame rate, 72-byte datagrams carry 3,375 payload bytes/s, excluding
UDP/IP and Wi-Fi overhead. There are no acknowledgements or retransmissions.

## One way clock tracking

Each receiver records local monotonic receipt time `r` as close to network ingress
as the platform permits. Let `u` be the packet's `send_time_us`. Its observation is:

```text
observed_offset = r - u
               = clock_offset + transport_delay
```

Maintain a slowly changing estimate `b` of that observation. Translate a sender
timestamp `s` into local time with `s + b`. This aligns the receiver with its own
typical arrival timeline. It cannot separate clock offset from transport delay
or establish exact alignment between receivers with different paths. This is
the [shared-clock limitation](https://www.rfc-editor.org/rfc/rfc7273.html#section-1)
of synchronization from relative stream timing.

A small initial algorithm is sufficient:

1. Acquire observations for 250 ms, requiring at least four valid frames with
   increasing sequence and timestamps. Initialize `b` from their median, so a
   single unusually delayed first packet does not establish the baseline.
2. During playback, use only valid, newly advancing packets that arrive before
   their existing deadline as clock observations. Reordered, duplicate and late
   frames never move the estimate.
3. Apply an elapsed-time exponential average with an initial 10 s time constant.
   Clamp observation residuals to +/-20 ms and limit changes in `b` to 1 ms per
   second of local elapsed time. These are prototype tuning values, intended to
   follow gradual drift without chasing individual Wi-Fi delays.
4. Reacquire after a session change, network reconnect, one second without a
   usable advancing frame, or a sustained timing discontinuity. Flush queued
   frames before reacquisition; never correct a large error by jumping playback
   through old frames.

Use timestamp differences against a local reference origin and signed offset
arithmetic. Do not subtract unsigned absolute clocks directly, and do not adjust
the controller's system clock, animation time or timer drivers. The estimate is
private to this subscription. Track drift through the changing offset initially;
a separate clock-rate estimator is unnecessary for the first prototype.

A persistent increase in network delay can eventually move the baseline, just
as clock drift can. A stream already delayed when acquisition begins can also
establish a delayed baseline. Absolute packet age and path delay cannot be proven
from these one-way observations. The receiver detects lateness relative to its
established timeline, rather than claiming a measured one-way network latency.

## The 100 ms jitter buffer

For a frame with audio timestamp `s`, its estimated local deadline is:

```text
deadline = s + b + 100,000 microseconds
```

The 100 ms delay is measured against the estimated audio timeline. Analysis and
excess network delay consume that budget. It is not a fresh 100 ms wait after
each arrival. Normal network delay remains included in `b`; perceived response
also depends on the analysis window, feature smoothing and LED frame timing.

For example, suppose two receivers normally see network delays of 10 ms and
30 ms, and the illustration expresses local time on an otherwise shared clock.
Their estimated offsets include those respective delays. With a negligible
analysis delay for this example:

| Frame audio time | Receiver | Arrival | Scheduled playback | Action |
| --- | --- | --- | --- | --- |
| 1,000 ms | A, normal path | 1,010 ms | 1,110 ms | Wait 100 ms |
| 1,000 ms | A, extra jitter | 1,060 ms | 1,110 ms | Wait 50 ms |
| 1,000 ms | B, normal path | 1,030 ms | 1,130 ms | Wait 100 ms |
| 1,000 ms | A, too late | 1,120 ms | 1,110 ms | Discard |

Jitter no longer directly moves playback, while B remains 20 ms behind A. An
isolated late arrival cannot extend its own deadline by updating the clock first.

Reserve a sorted queue of 32 frames per subscription. At the proposed rate this
holds about 0.68 s, sufficient for the initial acquisition window and normal
100 ms playback buffer. Account for receipt timestamps and tracking state as
well as the 2,304 bytes of wire payload storage. The queue is a hard bound, not
permission to increase the playback delay under load.

After acquisition, advance a sender-time playback cursor from local monotonic
time using `now - b - 100,000`, clamped so it never moves backwards. At each bounded
engine service point, publish the newest queued frame at or before that cursor
and remove all older due frames. Coalescing avoids replaying a backlog when the
engine is delayed. Publish all six values atomically.

Apply these queue rules:

- Discard duplicates and frames at or behind the current playback cursor.
- Accept a reordered frame if it is still ahead of the cursor, is consistent
  with its sequence/timestamp neighbours, and fits the queue. Receive order is
  not playback order.
- Reject frames implausibly far ahead of the established timeline. If the queue
  fills, reject new ingress and increment an overflow counter; do not allocate
  or silently add latency. A sustained fault triggers reacquisition.
- Hold the most recently published feature vector across missing frames. Do not
  extrapolate energy or invent onset pulses. Start with no receiver interpolation
  or additional smoothing; source envelopes already provide smoothing.

Service publications before the next input snapshot when due, while preserving
the existing rule that an invocation's inputs stay fixed across yields. The
buffer schedules source availability; independent animation and LED refresh
cadences can still add skew. Coordinating physical LED presentation is a
separate possible extension, not a guarantee of this proposal.

## Stream lifecycle and freshness

Latch one active session for the configured sensor. A different session cannot
replace it while that session remains live. Following a timeout, acquire a new
session from several consistent advancing packets rather than switching on a
single datagram. On adoption, flush the queue, reset the clock tracker and begin
the startup acquisition again. Keep a bounded cache of recently retired sessions
to prevent delayed packets from immediately switching playback back; specify its
retention and capacity in the implementation. This is bounded replay rejection,
not protection against arbitrarily old traffic or deliberate replay.

On receiver restart or network reconnect, begin acquisition with an empty queue.
On source loss, already queued frames may drain at their deadlines. Then retain
the last valid publication until the consumer's configured source timeout causes
its existing default or hold policy to apply. Use 1,000 ms as the default binding
timeout. Acquisition does not publish synthetic silence.

Record freshness from the original local receipt of each accepted frame, not from
its delayed publication time. Repeated engine ticks, clock corrections, duplicate
packets, invalid packets and discarded late frames must not refresh a source.
Expose receipt age and playback age separately so the 100 ms buffer is visible
in diagnostics. During acquisition or an absent stream, valid zero and missing
data remain distinguishable.

## Integration with LightVM

Implement a platform-independent decoder, clock tracker and queue, exercised on
the host. Native network ports supply group membership, bounded ingress, receipt
timestamps and reconnect notifications. ESP32 and Pico receive paths must stage
data for the engine owner; network callbacks must not mutate the registry or run
guest code. Bound packets processed per service call so traffic cannot starve
rendering or management. Enable the adapter independently of the current HTTP
discovery socket and its compile-time guards.

The adapter owns six float sources, for example:

```text
audio.room1.level
audio.room1.bass
audio.room1.mid
audio.room1.treble
audio.room1.centroid
audio.room1.onset
```

Resolve these names from a configured prefix and validate the resulting names
against the existing 32-character limit. Claim the group once when the adapter
is enabled. Add a bounded typed native publication entry point that preserves
atomic validation, publisher ownership and the original receipt timestamp. The
existing JSON API need not become the internal network decoder.

Sender identity, session and 64-bit sequence are transport metadata. Each
controller uses its own registry generation and advancing uint32 publication
sequence, renewing the local claim before exhaustion. These identities must not
be copied from one controller to another. A management claim that revokes adapter
ownership stops its publications and reports the conflict; do not automatically
fight the new owner by reclaiming on every packet.

An Eye binding can use the existing mapping format, for example:

```json
{
  "position": {
    "source": "audio.room1.centroid",
    "map": {"input_min": 0, "input_max": 1, "output_min": 0.05, "output_max": 0.95}
  },
  "width": {
    "source": "audio.room1.bass",
    "map": {"input_min": 0, "input_max": 1, "output_min": 0.04, "output_max": 0.4}
  },
  "hue": {
    "source": "audio.room1.treble",
    "map": {"input_min": 0, "input_max": 1, "output_min": 0, "output_max": 0.85}
  },
  "brightness": {"source": "audio.room1.level", "timeout_ms": 1000, "fallback": "default"}
}
```

Persist subscription configuration and input recipes, not queued samples, clock
estimates, sequence state or local publisher tokens. Configuration includes the
enabled adapter, multicast address/port, sensor ID and source prefix. Keep the
100 ms delay fixed for the first implementation; any later configurable delay
must be explicit across the installation. Reject a configuration that exceeds
source, socket or memory limits before replacing the active subscription.

## Resource and diagnostic requirements

Reserve and report DMA buffers, analysis history, FFT tables/workspace, task
stacks, networking and receiver queue memory. Keep steady capture, decoding and
publication free of application heap allocation. General SDK networking may have
its own bounded allocations. Hardware measurements must include Wi-Fi load and
normal LightVM rendering; FFT kernel benchmarks alone do not establish the full
system budget.

The analyser reports capture overruns, analysis duration, dropped work, send
failures and its current session. Each receiver reports acquisition state,
sensor/session, latest received and played sequence, queue occupancy, estimated
offset, offset residuals, late/duplicate/reordered/invalid/overflow counts, receipt
age and publication age. Label offset and jitter as estimates; do not display
them as measured absolute network latency or guaranteed inter-controller skew.

## Delivery and validation

1. Implement shared host-side codec and receiver logic with a synthetic sender
   and deterministic sender/receiver clocks. Document packet fixtures.
2. Implement ESP32-S3 capture and feature extraction. Validate 48 kHz operation,
   FFT scaling, silence, tones and transients before enabling multicast.
3. Integrate the native receiver with the registry and existing Eye bindings.
   Verify atomic snapshots, receipt-based expiry and ownership handling.
4. Run one physical analyser and at least two receivers. Compare immediate
   publication against buffered publication under normal Wi-Fi and injected
   jitter, then record timing and resource measurements.

Acceptance checks must cover:

- Exact packet encoding, truncation, malformed fields, nonfinite values and
  wrong sensor identity; invalid batches publish nothing.
- Arbitrary initial clock offsets and simulated gradual drift, including
  +/-250 ppm trials. Check stable playback and bounded estimator response.
- Jitter bursts, loss, duplicates, useful reordering, late frames and engine
  stalls. Playback never moves backwards or drains a visible backlog.
- Two paths with different constant delays. Their baseline difference remains;
  adding the buffer reduces arrival-driven variation rather than hiding it in
  an incorrect synchronization claim.
- Startup with an outlier, sender restart, receiver restart, Wi-Fi reconnect,
  retired-session traffic, prolonged outage and clock discontinuities.
- Queue limits, ingress flooding, source-slot exhaustion, registry ownership
  replacement and local publication sequence renewal.
- Synthetic audio fixtures for band separation, RMS, centroid, normalization,
  smoothing, onset warmup and silence; measured capture/processing headroom.
- A shared GPIO or photodiode observation of physical LED response, separating
  network jitter from animation cadence and output transmission time.

Record late-frame rate and physical timing distributions with and without the
100 ms buffer. Choose a numerical jitter/skew target from those measurements;
the first proposal makes no submillisecond or exact simultaneity guarantee.

This remains an experimental format. If protocol or configuration changes during
implementation, update the analyser and receivers together and reprovision the
test devices where necessary. Do not add compatibility paths for superseded
experiments.
