# Preset-driven audio subscriptions and gateway sources

Status: implementation in progress, 27 October 2026. Scoped UDP/gateway providers,
lease/session validation, stored source declarations and the browser source editor
are implemented. Browser/native analysis parity and the Pico software memory/stack
gates pass. Direct, layer, composition and A/B lifecycle commands, manual edits
and scene preparation reserve retained-source capacity before changing playback.
Power off/on and restore-last startup preserve source declarations. Short physical
Sense/Pico subscription and restoration checks pass. The complete mixed-transport
conformance matrix remains in progress. Every installed preset is selectable
in Home Assistant. Audio visualizations remain Python compiled to platform bytecode.

The extended design also requires the webservice/browser analyser to adopt the
Sense's current algorithm, supports explicitly selected alternative algorithms
for browser experiments, and permits publishers on a Mac or Raspberry Pi. These
requirements are specified in [shared audio analysis](audio-analysis-profiles.md).
The portable analyser, leased publisher and silent browser AudioWorklet are
implemented; optimized S3 FFT parity on the physical board passes the committed
corpus for both profiles and 0/16/64 bands. Threshold-boundary cases remain pending.

The wire specification is maintained with the publisher in kaj-fi's
[`docs/subscriptions.md`](https://github.com/KajLights/kaj-fi/blob/main/docs/subscriptions.md).
In the shared checkout it is `../kaj-fi/docs/subscriptions.md`.

## User-facing model

A preset declares named external sources and its ordinary INPUT bindings use those
names. The address belongs to the preset, so changing presets can change the
publisher: a Sense, a Mac or a Raspberry Pi service.
The address is configuration, never a destination compiled into either firmware.

Illustrative new preset schema:

```json
{
  "schema": "lightvm.preset",
  "sources": {
    "audio": {
      "type": "lvaf",
      "address": "10.77.10.245",
      "port": 49152,
      "source_id": "00001cdbd45cce94",
      "analysis_profile": 1,
      "allow_synthetic": false
    }
  },
  "layers": [
    {
      "id": "bands00",
      "program": "audio_spectrum",
      "settings": {"first_band": 0},
      "inputs": {
        "band0": {"source": "audio.band00"},
        "band1": {"source": "audio.band01"},
        "band2": {"source": "audio.band02"},
        "band3": {"source": "audio.band03"},
        "band4": {"source": "audio.band04"},
        "band5": {"source": "audio.band05"},
        "level": {"source": "audio.level"},
        "onsets": {"value": 0}
      }
    }
  ]
}
```

The other two rainbow layers use this same `audio` alias. The flashing preset
binds `onsets` to `audio.onsets` instead. All 16 bands and auto-levelling remain
implemented by the existing Python example.

For `type: "lvaf"`, only `type` and `address` are required. Port defaults to 49152,
analysis profile to 1 (Adaptive percussion v1), and synthetic input is rejected
by default. An optional source ID pins the intended logical analysis source.
The editor should discover/read the ID and save that pin when possible, showing
an identity mismatch instead of silently following a different device at an old IP.
Hand-written address-only presets accept the identity returned by the handshake;
that is an explicit distinction from identity-pinned presets.

The first implementation accepts an IPv4 unicast literal or an ordinary DNS
hostname, bounded to 127 bytes, with nonblocking resolution and a port in
1024..65535. Reject unspecified, broadcast and multicast addresses. Resolve on
acquisition and again after reconnect/expiry; retain the resolved peer for the
lease. Literal addresses do not automatically follow DHCP changes. The current
Pico mDNS code advertises services but does not resolve arbitrary `.local` peers:
reject `.local` with a clear message until a resolver is implemented, rather than
claiming it works through ordinary DNS. A local router DNS name or DHCP reservation
is sufficient for the first implementation. Automatic discovery is an optional
address-picker enhancement, not a requirement for steady-state streaming.

Expose a source editor under the preset: address, optional identity pin, and
live connection/format status. Python INPUT declarations and bytecode do not gain
network operations. Saving/listing a preset does not subscribe; activating it does.

## Coexistence with the webservice gateway

The gateway is the existing HTTP/WebSocket command path used by browser music
and generic `input_claim` / `input_publish` clients. It remains usable while UDP
subscriptions are active. There is no global "web or Sense" mode and no transport
switch in an animation. A single invocation can bind some inputs to one provider
and others to another, alongside `board.*`, light controls and generic publishers.

Illustrative future preset combining both paths:

```json
{
  "schema": "lightvm.preset",
  "sources": {
    "room": {
      "type": "lvaf", "address": "10.77.10.245", "analysis_profile": 1
    },
    "desk": {
      "type": "gateway", "name": "music", "analysis_profile": 1
    }
  },
  "layers": [{
    "id": "mixed", "program": "eye",
    "inputs": {
      "width": {"source": "room.bass"},
      "intensity": {"source": "desk.level"}
    }
  }]
}
```

`type: "gateway"` names a structured audio publisher on this controller, not
a remote HTTP URL. It takes `name` (a bounded ordinary object name), optional
`source_id`, `analysis_profile` (default 1) and `allow_synthetic` (default false).
Selecting this preset does not open a microphone or seize a publisher's ownership.
It waits for that explicitly started publisher, using fallbacks until it appears.
The same provider vocabulary exposes `level`, `bass`, `mid`, `treble`, `centroid`,
`onset`, spectrum bands and per-consumer `onsets` through either transport.

The updated browser runs profile 1 from the common analysis core. Its existing
six-scalar publisher cannot provide full spectrum/event semantics by merely
renaming fields. Add a bounded structured-audio ingress to the gateway: claim a
named provider with controller boot/generation ownership and source/profile/session
metadata, publish complete feature/event frames, and release it explicitly.
Claims are separate from generic scalar-name claims; neither can revoke or write
a UDP subscription. Starting a new owner of the same gateway name invalidates the
old generation atomically. A profile or pinned identity mismatch makes the scoped
provider unavailable instead of falling through to a matching global scalar.

Use the same validated feature record and event logic as the UDP adapter. Commit
all metrics/bands from one frame atomically and inspect events before coalescing.
Keep transport-specific tokens outside that record. A maximum spectrum frame
exceeds the current 512-byte command payload: use bounded feature-frame
fragmentation/reassembly with a frame identity, generation, exact length and
completion check. Charge at most one 700-byte staging frame per gateway provider;
expire incomplete frames within 250 ms and reject overlapping/reordered fragments.
Do not enlarge every command buffer or borrow the program/config upload slot.
No partial frame reaches INPUTs. Allocate operation numbers and detailed ingress
encoding alongside implementation, with 64-band/four-event boundary tests.

Retain one request in flight and the newest pending snapshot in the web client;
retain live events only within their TTL. A late reliable HTTP/WebSocket response
does not make an old frame fresh. Establish the same bounded clock-offset interval
as UDP using gateway reply challenges: controller send time `c0`, publisher echo
timestamp `s`, controller receipt `c1` give `[c0-s,c1-s]`. Correlate each challenge
with boot/generation and a unique request, renew within four seconds, expire after
five, and use the same 350 ms age ceiling. A publisher cannot substitute receipt
time for capture time. Reset on clock discontinuity; reject stale queued events.

Gateway ownership expires after 15 seconds without a valid frame/keepalive, or
on explicit release or owning transport-session closure. This differs from the
ordinary scalar registry's retained-until-reboot names. A preset dropping its last
reference does not stop independently user-started browser capture; the browser
owns that permission and lifetime. Losing either transport leaves the other
provider usable. Rendering snapshots both on the engine owner, but promises no
cross-device sample synchronization and adds no waiting for the slower provider.

Browser comparisons can forward two explicitly named structured providers; local
comparison needs no controller slots. The old generic six-scalar audio publisher
is replaced when the shared-core browser lands. Arbitrary non-audio scalar clients
continue to use the generic registry; that is an independent feature, not an
obsolete-audio compatibility shim. Update affected experimental presets directly.

## Why source scope is necessary

Today one `AudioReceiver` in `HardwareConfig` claims all 23 global `audio.*` names.
Those claims consume 23 of the 32 generic publisher slots. Merely changing the
configured sensor when recalling a preset would redirect **every** layer and
output, and a crossfade between Senses would feed both scenes from one source.
Adding global prefixes for four full spectra would also exceed the registry.

Introduce an `InputSourceScope` owned by a composition or standalone invocation:

- `sources.audio` belongs to that scene. `audio.band00` resolves within that scope.
- The source manager owns the latest measurement and one network lease per shared
  endpoint. Scopes reference it; they do not duplicate 23 global signals.
- An explicitly declared local alias takes precedence over a global source name.
  If its provider is offline, use the binding's fallback; do **not** fall through to
  an unrelated browser publisher with a matching name.
- Undeclared sources retain generic-registry behavior, including web-published
  controls, board measurements and light controls. Reserve `board` and `light`
  aliases for the built-in domains. Alias names and full binding names must fit
  the existing 32-byte source-name contract.
- Lookup and capture continue through the shared INPUT implementation. Each
  invocation snapshots its inputs together before rendering, on the engine owner.
  No network callback calls a renderer or mutates a worker's snapshot.

Canonical subscription identity includes provider type, normalized address/port
or gateway name, optional pinned source ID, analysis profile, accepted feature
version and synthetic policy. Identical declarations share a connection across
layers, slots and outputs. Different pins/policies do
not accidentally share. Initial implementation need not merge differently spelled
DNS aliases solely because they resolve to the same sensor.

Use at most four simultaneous active/preparing or gateway-claimed structured audio
providers **in total across both transports**, each allocated and charged to the
VM budget when acquired. Ordinary scalar publishers retain their separate 32-name
limit. Keep declarations for inactive retained scenes, but no live socket state
for them. Avoid preallocating
four full receivers in static RAM. Measure exact structure/native PCB costs in
Pico builds; target no more than 2 KiB of charged state per UDP connection,
excluding existing shared network buffers. Account separately for gateway staging
and its HTTP/WebSocket connection, and fail admission before replacing a live scene
if capacity is unavailable. The existing eight INPUTs per program do not change.

## Lifecycle and transitions

| Situation | Required action |
| --- | --- |
| Load/save a preset in the library | Validate source declarations only; no network traffic |
| Prepare a scene | Reserve scoped handles; start asynchronous acquisition while preparation proceeds |
| Remote Sense unavailable | Finish preparation using declared INPUT fallbacks; report source offline and keep retrying while needed |
| Invalid declaration, no slot or insufficient memory | Fail preparation transactionally; keep old scene and its subscriptions |
| Commit/activate | Transfer candidate ownership with the scene; no global rebinding |
| Crossfade between different Senses | Both A and B retain their own leases and snapshots until transition completion |
| Several scenes use one Sense | One shared lease with reference-counted active consumers |
| Preset recall completes | Suspend the hidden retained source scene's demand; release its lease if nobody else needs it |
| Manual A/B mixing resumes a retained scene | Reacquire its declared source; use fallback until fresh values arrive |
| Output fades to off | Keep sources through the fade, then release demand at black |
| Pause | Release demand for the paused invocation; retain displayed pixels and reset event baseline on resume |
| Cancel/delete/reset/replace/configure output | Release the affected references on all success/failure/cancel paths |
| Controller reconnect/reboot | Reacquire only sources needed by restored active scenes; persisted lease tokens are forbidden |

These lease actions apply to UDP sources on any host. Gateway references observe
the independently owned claim lifecycle above. Browser/host algorithm changes
create a new session and invalidate profile-incompatible claims or leases; changing
a preset must never retune a source used by another controller. Different algorithm
outputs have distinct logical source identities/endpoints or gateway names.

Normal presets remain usable without the Sense online; remote availability must
not turn local preparation into an unbounded network transaction. A future
"require source before activation" mode would be a separate explicit feature,
not an implicit wait in the first implementation.

The same `sources` object is accepted and retained by composition apply/get,
preset save/get, direct program activation and composition snapshots. Python and
browser clients preserve it when saving an edited preset. A preset's transition
program resolves external bindings within the destination preset's scope; source
and destination animation layers continue to use their own scopes. Standalone
manual transitions carry an explicit scope. Reusing a scene or making a private
remembered off/on target must preserve declarations as well as layer INPUTs.

## Metrics, onsets and timing

Retain latest-only metric delivery and zero deliberate playback delay. The normal
INPUT timeout remains configurable (one second by default), while a source can
become unavailable sooner when its conservative metric age exceeds 350 ms. That
invokes the same fallback policy; an explicit hold policy is still honored. The
wire design adds per-lease identity and ACK-based age bounds, keeping late packets
from old subscriptions out of a newly activated preset. Receiver source status
distinguishes offline, wrong identity, unsupported format, capacity refusal and
stale data.

Deduplicate each onset once at the shared provider using source/session/event
identity. **Consumption remains per invocation.** For the current `audio.onsets`
integer interface, each binding starts its own visible counter at zero and samples
only new eligible occurrences after activation. Its visible counter never drops
just because the network session changes or it pauses. While paused, unavailable
or transitioning to a replacement session, advance the internal baseline without
replaying those occurrences when it resumes. This prevents an initial flash from
a shared receiver's pre-existing count and prevents one layer from consuming
another's beat. Capture the baseline during binding attachment, before first render.
A native event INPUT type remains a separate future feature.

Use counter rollover behavior consistent with current positive int32 counters;
coalesce multiple new onsets in one render interval into the existing example's
single flash, rather than pretending to schedule missed beats. Keep band edges
in diagnostics: changing the Sense's global spectrum layout creates a new stream
session and can change what a band index means. A receiver never retunes the
Sense's global analysis settings as a side effect of selecting a preset.

## Installed presets are the selectable presets

Remove `home_assistant.presets`, its 16-name limit and the per-output checkbox
editor. Keep `home_assistant.default_preset` and optional friendly name. These
still decide whether an output appears as a light and what ON does before a
preset has been selected. ON subsequently resumes the last installed selection;
OFF retains its existing transition behavior.

All stored presets appear automatically in each exposed output's HA effect list,
matching the editor and `preset_list`. Add/delete republishes discovery. A preset
that cannot run on an output is rejected by ordinary transactional admission,
with an error; discovery does not maintain a second eligibility catalogue.
Arbitrary live edits have an empty effect name instead of the synthetic name
`Custom`, so real presets named `Custom` or `off` remain unambiguous and selectable.

The old list also bounded discovery to one 2 KiB MQTT transmit buffer. Stream the
catalogue through the existing buffer instead of allocating a full JSON copy or
silently truncating the list. A borrowed source is valid only while catalogue and
configuration revisions match; if they change mid-publication, close/retry the
MQTT session so a partial packet cannot be combined with another packet. Keep
network operations nonblocking and test partial writes and catalogues larger than
the buffer. This does not change the incoming MQTT command size limit.

Persisted experimental configs with `home_assistant.presets` are invalid under
the new schema. Export the config, remove that property from each output, and
reapply/save over USB after upgrading. Programs/presets need no migration or wipe.
Root hardware `audio` configuration is also invalid. Remove it and save
per-preset declarations as described in the [provisioning guide](../audio-subscriptions.md).

## Implementation map and acceptance

- `src/inputs.[ch]`: scoped provider resolution, per-consumer event baseline and
  unchanged ordinary INPUT mappings/fallbacks.
- `src/audio_sources.[ch]` and the pinned kaj-fi wire/subscriber core: LVAF v5
  decoding, lease/session validation, latest values and provider control state.
- `src/preparation*`, `composition*`, `app.c`: transactional scope lifetime,
  serialization, source ownership for retained scenes and transitions.
- `platforms/pico/network_port.c`: bounded shared UDP socket, unicast controls,
  async DNS and reconnect ownership. `LIGHTVM_AUDIO_SUBSCRIPTIONS` replaces the
  multicast build flag; the host adapter supports deterministic multi-peer tests.
- `web/src/Presets.tsx`, clients and schema tests: source editing/preservation and
  status, with no networking logic in Python effect code.
- kaj-fi publisher: receiver lease table, fan-out, independent repetition budgets
  and removal of the hardcoded destination, per its wire specification.
- Browser/webservice analyser: replace `AudioFeatureExtractor` with the common
  C/WASM core, add the algorithm/comparison selector, and forward full structured
  frames over the existing gateway. See the shared-analysis conformance plan.
- Gateway adapter: claim ownership, bounded frame ingress, clock mapping and
  source-scope binding alongside UDP; keep ordinary scalar publishing independent.
- Optional Mac/Pi publisher: native analysis and UDP service adapters using the
  same wire protocol, with no embedded-device identity or SDK assumption.

Prove two receivers on one Sense; two different Sense sources across A/B; two
outputs sharing one lease; cancellation and failed preparation without leaks;
hidden retained scenes releasing demand; reconnect/expiry without stale flashes;
startup restoration; no subscription from merely listing presets; and no growth
in charged state after repeated acquire/release. Unit tests should exhaust the
four-source limit and test the complete rollback path. On hardware, use short
functional checks and record actual memory and management latency. A simulated
second Sense is explicitly a simulation, not a two-Sense physical validation.

Also prove one animation consuming UDP and gateway values simultaneously, including
both kinds during a crossfade; independent timeout/reconnect; identical alias names
in different scenes; a generic claim unable to overwrite either audio provider;
shared four-provider exhaustion with rollback; and full-spectrum gateway frames
crossing the 512-byte command boundary. Algorithm tests compare native and WASM
output on identical PCM, switching/reset without old events, mismatched profiles,
two local browser detectors and optional forwarding under two distinct names.
Host-service contract tests cover a persistent non-MAC source ID, process restart,
sleep/resume and two service ports. No Mac/Pi daemon is required for this design
task; a future test server is an optional implementation aid.

## Implemented preset cleanup validation

The selector cleanup was installed on the Pico W, with the obsolete property
removed from its saved configuration. All six installed presets were preserved;
the no-flash spectrum is running at the previous 40% brightness. Wi-Fi, MQTT and
the existing unicast audio receiver are connected. This deployment **does not**
implement LVAS or change the Sense publisher.

Validation passed: 33 native sanitizer tests; 46 MQTT/preset/startup integration
tests; 24 Pico build/accounting tests; 19 focused web tests and the web build;
Pico RAM/stack gates; and an ESP32-S3 Wi-Fi/HTTP/MQTT build. The catalogue test
advertises 65 presets through the unchanged 2 KiB transmit buffer. Tests also
cover partial writes, changed publication sources, add/delete discovery refresh,
ON restoring any installed preset, and actual presets named `Custom` and `off`.
Native regression tests used the committed compiler plus this task's changes,
isolating concurrent teaching/compiler work in the shared checkout. Rapid host
restart tests disable boot-sequence recovery; physical Pico firmware retains it.

The [validation record](../benchmarks/preset-catalog-2026-10-27.json) contains the
installed firmware hash and memory/connection results. Removing the fixed list
also lowers static RAM by 512 bytes; automatic VM capacity is now 87,672 bytes,
with the operating, stack and driver reserves unchanged.

## References

- [kaj-fi subscription wire design](https://github.com/KajLights/kaj-fi/blob/main/docs/subscriptions.md)
- [Home Assistant MQTT light JSON schema](https://www.home-assistant.io/integrations/light.mqtt/#json-schema):
  `effect_list` is a discovery list; a separate per-output preset allow-list is
  our own policy and is not required by Home Assistant.
- [RFC 8085](https://www.rfc-editor.org/rfc/rfc8085.html): bounded UDP payloads,
  backoff, controlled-LAN applicability and return-routability considerations.
