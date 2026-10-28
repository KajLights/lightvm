# Structured audio gateway

Implementation record, 27 October 2026. The engine and Python/browser clients
implement the ingress below. The [browser capture UI](audio-analysis-browser.md)
runs the shared WASM analyser and forwards structured snapshots through this
ingress. Host/browser checks and physical Sense/Pico UDP checks are recorded in
the [acceptance record](audio-acceptance.md).

## Ownership and declarations

A scene declares a gateway independently of the process which supplies it:

```json
{
  "sources": {
    "music": {"type": "gateway", "name": "desk", "analysis_profile": 1}
  },
  "layers": [{"id": "main", "program": "eye", "inputs": {
    "intensity": {"source": "music.level"}
  }}]
}
```

The complete enclosing document still needs its ordinary schema and, for saved
presets, program CRC references. Gateway declarations accept an optional
16-hex-digit `source_id` and `allow_synthetic` (default false). Profile defaults to
1. Different pins, profiles or synthetic policies occupy separate canonical
provider slots. Those slots observe one named gateway stream without changing
each other's policies. Identical declarations share a slot. There are four
active/preparing/claimed structured providers in total, including UDP sources.

A user-started publisher can claim a name before any scene uses it. An unobserved
claim retains one slot until release, session closure or expiry. Removing the last
scene reference does not stop browser capture. Generic scalar claims are separate
and cannot overwrite a scoped audio provider. Unknown or incompatible structured
values do not fall through to the scalar registry.

`level`, `bass`, `mid`, `treble`, `centroid`, `onset`, `band00` through `band63`
and `onsets` use the same decoded records as UDP. Only the transmitted bands are
available. `onsets` is a per-binding positive int32 counter: attachment starts at
zero, pause/resume advances the baseline, and a temporary source gap does not
lower the visible count and replay it on recovery. Other unavailable metrics
follow the declared default/hold policy.

## JSON operations

All operations use the existing `CMD_MANAGE` envelope. IDs and timestamps below
are nonzero unsigned 64-bit values encoded as exactly 16 hexadecimal digits.
Generation is a nonzero uint32; exhaustion requires reboot. Ownership also binds
to the transport session that issued the claim.

- `audio_claim`: `name`, `source_id`, `session`, `analysis_profile` (1 or 2),
  optional `synthetic` (false by default). It atomically replaces the previous
  owner after admission succeeds, returning `boot_id`, `generation`, `challenge`
  and `controller_us` (the challenge's controller send time).
- `audio_clock`: `boot_id`, `generation`, `challenge`, `publisher_us`. Stamp
  `publisher_us` when echoing the received challenge. Each challenge can be
  accepted once. The controller records its receipt time and the offset interval
  `[controller_send - publisher_echo, controller_receive - publisher_echo]`.
- `audio_keep`: `boot_id`, `generation`. Returns a fresh challenge in the same
  shape as the claim. Renew/echo within four seconds. Keep alone extends ownership,
  never feature freshness or the clock interval.
- `audio_release`: `boot_id`, `generation`. Invalidates the stream immediately.
- `audio_status`: no fields. Returns `providers`, including state, source identity,
  actual/expected analysis profiles, references, accepted packet and age-rejection
  counts, RTT, stream/session identities, exact band edges and charged bytes. Shared gateway ingress is charged once.

Clock bounds expire after five seconds. A backward/stepped publisher clock
invalidates the stream and requires a new session/claim. Ownership expires after
15 seconds without a valid frame or keepalive. Wrong boot, generation, owner,
challenge, source or session never refreshes ownership or values. A new claim
clears partial frames and stream history while retaining the total used by active
consumers' independent occurrence baselines.

## Binary frame ingress

LightVM allocates command **16** (`CMD_AUDIO_FRAME`) for a fragment. The ordinary
command CRC and 512-byte payload limit are unchanged.

| Payload offset | Encoding | Meaning |
| --- | --- | --- |
| 0 | uint32 little endian | Claim generation |
| 4 | uint64 little endian | Monotonically increasing frame identity, starting above zero |
| 12 | uint16 little endian | Exact total record length, 92..700 bytes |
| 14 | uint16 little endian | Exact fragment offset |
| 16 | remaining bytes | Contiguous record bytes, at most 496 |

The inner record is [LVAF v5](https://github.com/KajLights/kaj-fi/blob/a8e536a96cd5722a71564d02c0bd9202799aa25b/docs/protocol.md),
with its network-byte-order fields and **neutral lease value 1**. Gateway ownership
stays in the outer command. The receiver substitutes its locally admitted lease
when using the common record/event validator. Source/session/synthetic metadata
must match the claim; the profile is fixed by that claim.

Send a full record as ordered fragments, waiting for each command response. A
700-byte record needs payloads of 512 and 220 bytes. Exactly one staging record is
allocated per named gateway stream. Incomplete assembly expires after 250 ms.
Overlaps and reordered continuations discard assembly; replayed frame identities,
wrong bounds, trailing bytes, invalid floats/layout/events and incomplete records
cannot reach INPUTs. Program/config upload staging is independent.

Metrics commit atomically only after the complete record passes validation. A
reordered valid record may contribute a previously lost eligible event, but cannot
replace newer metrics or refresh their receipt time. Event repeats are deduplicated
by source/session/event identity. The same conservative 350 ms age ceiling as UDP
applies to captured metrics and occurrences, including clock uncertainty and drift
margin. Reliable transport does not make queued old audio fresh.

Clients retain one request in flight and the newest pending snapshot, preserving
only unexpired occurrence records. Publication cadence comes from analysis hops;
transport retries and UI timers must not create analysis time or restamp capture.
No raw PCM is sent through this interface.

## Verification

`tests/audio_gateway_contract.inc` covers 64 bands/four events, partial-frame
isolation, duplicate/reordered records, fragment bounds/expiry, clock expiry and
discontinuity, session replacement, policy isolation and shared capacity rollback.
`tests/test_audio_gateway.py` exercises the real native command engine, persisted
scopes, paused counters, generic-registry isolation and independent upload staging.
`web/src/audio-wire.test.ts` checks exact binary encoding and bounded fragmentation;
`web/src/audio-source-native.test.ts` sends the browser encoding through the real
native engine and observes the resulting INPUTs and pixels.

The ARM compiler measures 928 bytes per provider plus 16 adapter bytes and a
40-byte lwIP PCB: **984 charged bytes per UDP provider**, below the 2 KiB target.
Shared gateway state is 1,512 bytes including its 700-byte staging buffer, in
addition to its canonical provider slots and the existing HTTP/WebSocket budget.
The Pico W build passes the existing RAM/stack gates. Browser AudioWorklet
conformance is recorded separately in the [browser analysis checks](audio-analysis-browser.md).
The [physical check](validation/audio-hardware-2026-10-27.json) covers Sense
microphone features over Wi-Fi to the Pico and host. The mixed UDP/gateway
equivalence check uses the native engine and simulated publishers; it is not a
claim of acoustic parity between browser and Sense microphones.
