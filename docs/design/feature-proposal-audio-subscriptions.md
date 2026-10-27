# Preset-driven Sense subscriptions

Status: implementation design, 27 October 2026. Subscription behavior below is
**planned**, not enabled by the current firmware. The independent cleanup that
makes every installed preset selectable in Home Assistant is implemented alongside
this design. Audio visualizations remain Python compiled to platform bytecode.

The wire specification is maintained with the publisher in kaj-fi's
[`docs/subscriptions.md`](https://github.com/KajLights/kaj-fi/blob/main/docs/subscriptions.md).
In the shared checkout it is `../kaj-fi/docs/subscriptions.md`.

## User-facing model

A preset declares named external sources and its ordinary INPUT bindings use those
names. The address belongs to the preset, so changing presets can change the Sense.
The address is configuration, never a destination compiled into either firmware.

Illustrative new preset schema:

```json
{
  "schema": "lightvm.preset",
  "sources": {
    "audio": {
      "type": "kaj-fi",
      "address": "10.77.10.245",
      "port": 49152,
      "sensor_id": "00001cdbd45cce94",
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

Only `type` and `address` are required. Port defaults to 49152; synthetic input
is rejected by default. An optional sensor ID pins the intended physical Sense.
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
  If its Sense is offline, use the binding's fallback; do **not** fall through to
  an unrelated browser publisher with a matching name.
- Undeclared sources retain current generic-registry behavior, including browser
  `music.*`, board measurements and light controls. Reserve `board` and `light`
  aliases for the built-in domains. Alias names and full binding names must fit
  the existing 32-byte source-name contract.
- Lookup and capture continue through the shared INPUT implementation. Each
  invocation snapshots its inputs together before rendering, on the engine owner.
  No network callback calls a renderer or mutates a worker's snapshot.

Canonical subscription identity includes normalized address/port, optional pinned
sensor ID, accepted feature version and synthetic policy. Identical declarations
share a connection across layers, slots and outputs. Different pins/policies do
not accidentally share. Initial implementation need not merge differently spelled
DNS aliases solely because they resolve to the same sensor.

Use at most four simultaneous active or preparing source connections, each
allocated and charged to the VM budget when acquired. Keep declarations for
inactive retained scenes, but no live socket state for them. Avoid preallocating
four full receivers in static RAM. Measure exact structure/native PCB costs in
Pico builds; target no more than 2 KiB of charged state per connection, excluding
existing shared network buffers, and fail admission before replacing a live scene
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

Deduplicate each onset once at the shared connection using sensor/session/event
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
The future subscription implementation will similarly replace root hardware
`audio` configuration with per-preset declarations; document that provisioning
step rather than implementing an obsolete multicast fallback.

## Implementation map and acceptance

- `src/inputs.[ch]`: scoped provider resolution, per-consumer event baseline and
  unchanged ordinary INPUT mappings/fallbacks.
- `src/audio_receiver.[ch]`: LVAF v5 decoding, lease/session validation, latest
  values, per-connection statistics and control state machine.
- `src/preparation*`, `composition*`, `app.c`: transactional scope lifetime,
  serialization, source ownership for retained scenes and transitions.
- `platforms/pico/network_port.c`: bounded shared UDP socket, unicast controls,
  async DNS and reconnect ownership. Replace the multicast build flag with
  `LIGHTVM_AUDIO_NETWORK`; host adapter supports deterministic multi-peer tests.
- `web/src/Presets.tsx`, clients and schema tests: source editing/preservation and
  status, with no networking logic in Python effect code.
- kaj-fi publisher: receiver lease table, fan-out, independent repetition budgets
  and removal of the hardcoded destination, per its wire specification.

Prove two receivers on one Sense; two different Sense sources across A/B; two
outputs sharing one lease; cancellation and failed preparation without leaks;
hidden retained scenes releasing demand; reconnect/expiry without stale flashes;
startup restoration; no subscription from merely listing presets; and no growth
in charged state after repeated acquire/release. Unit tests should exhaust the
four-source limit and test the complete rollback path. On hardware, use short
functional checks and record actual memory and management latency. A simulated
second Sense is explicitly a simulation, not a two-Sense physical validation.

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
