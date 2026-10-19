# Configuration and stored programs

Firmware contains board capabilities and resource ceilings. Connected outputs,
GPIO selection, strip length, RGB wire order and output brightness are configured
as data. Each named output owns an independent animation instance. `ws2812b`
uses physical GPIO. Output count is bounded by available pins, configuration size
and runtime resources. No peripheral or startup effect is implicit. A controller
with no saved configuration starts idle and remains available over USB.

## Provisioning

Build the independent samples with `python tools/build_examples.py`, then
configure the output and store a program for manual playback:

```sh
python tools/client.py --port PORT --describe
python tools/client.py --port PORT --store-program rainbow build/examples/rainbow.lbc
python tools/client.py --port PORT --validate-config examples/config/pico-startup.json
python tools/client.py --port PORT --config examples/config/pico-startup.json --save
python tools/client.py --port PORT --activate rainbow
```

For an XIAO board use `--no-reset` and `examples/config/xiao-startup.json`; the C3 uses
`examples/config/xiao-c3-startup.json` with D1/GPIO3 to avoid its D0 boot strap pin.
`GP16` is a Pico pin name; `D0` is a XIAO board alias. Discovery lists every
permitted pin, including XIAO `GPIO` aliases. These tables describe exposed pins
and exclude USB and flash pins; they do not identify connected hardware. C3 D0,
D8 and D9 are boot strap pins, so external circuits on them can affect startup.
XIAO mappings follow the manufacturer's [C3 pin map](https://wiki.seeedstudio.com/XIAO_ESP32C3_Getting_Started/),
[S3 pin map](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
and [C6 pin map](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/).

Configuration apply takes effect in RAM. Entries with unchanged IDs, types and
hardware configuration retain their drivers and running instances, including when
reordered. Changed or removed outputs are stopped and unloaded. Startup-only
edits do not restart an animation. Saving is a separate operation and does not
start playback. After reboot, each output applies its saved power-on behavior.
An output without startup configuration stays off.
Remove `--save` for temporary wiring experiments; `--save-config` explicitly saves
the current active configuration.

Other useful commands:

```sh
python tools/client.py --port PORT --get-config
python tools/client.py --port PORT --get-config --saved
python tools/client.py --port PORT --programs
python tools/client.py --port PORT --get-program rainbow --save-to rainbow.lbc
python tools/client.py --port PORT --delete-program unused
python tools/client.py --port PORT --cancel-upload
```

For host persistence, replace `--port PORT` with
`--process build/host/lightvm --storage build/demo-store.bin`. The receiver locks
the storage file against simultaneous writers. Without `--storage`, the host
uses an unlinked temporary flash file, so uploads and named saves exercise the
same storage path but disappear when that process exits. A supplied storage file
preserves saved state across invocations.

Wi-Fi and HTTP use separate [network settings](networking.md), revisions and
snapshot objects. They can be provisioned without configuring any light output.

## JSON configuration v1

See [the Pico example](../examples/config/pico-startup.json). Root fields are
required `schema` and `outputs`, and optional `inputs`. The outputs array may be empty. Output IDs and
physical pins must be unique. Root-level `targets` and `startup` are rejected,
including empty or null forms. Previously saved configurations using those fields
report a boot error and start with empty active configuration; replace them through
the normal configuration interface. Stored programs remain available.

Each output requires `id`, `type` and `config`. Its optional `startup` object
sets **Power-on behavior**, independently of its Home Assistant default preset:

```json
"startup": {"mode": "off"}
```

```json
"startup": {"mode": "restore_last"}
```

```json
"startup": {"mode": "preset", "preset": "evening"}
```

- `off` keeps the output black. This is also the default when `startup` is omitted.
- `restore_last` restores the last selected preset and logical on/off state.
  With no remembered state, it stays off. An output remembered as off retains its
  preset selection for the next ON command.
- `preset` turns on with the named controller preset, including all layers,
  initialization settings and input bindings. Startup uses that preset's saved
  transition, exactly like preset selection and ON.

The startup transition clock starts when the output scheduler begins rendering,
after native initialization. Time spent initializing Wi-Fi or other services
does not consume the visible transition duration.

The same policy applies to power cuts and software restarts, without waiting for
HA or MQTT. Applying or saving a policy does not change current playback; save
and restart to use it. The editor offers these controls under **Configure →
Output & startup**. Presets must already be saved on the controller.

On writable controllers, successful `preset_recall` and `output_power` commands
save their desired preset and power immediately, before starting the transition.
This happens in every startup mode, so switching to `restore_last` can use the
last command. Saving restore-last while a startup preset is running also captures
that current target. An OFF accepted during a fade remains OFF after power loss.
Repeated targets and booting do not write again. State is keyed by output ID,
so reordering outputs preserves it; renaming an output gives it a new identity.
Records use the store's atomic publication, survive abrupt power loss after an
acknowledged command, and consume flash space. A persistence failure rejects
the command without changing playback. Read-only controllers can use off or a
fixed preset; `restore_last` requires writable storage.

Restore restarts the current saved definition of the selected preset. It does
not checkpoint animation time, random state, live input samples, temporary
editor previews or manual A/B mixing. Save edits as a preset and recall it to
make them part of the remembered state.

Validation, application and saving prepare isolated preset candidates against
the candidate pixel count without opening hardware or replacing running
instances. A missing or invalid preset at boot leaves that output off and reports
`outputs[ID].startup` in `boot_error`; other outputs and management remain
available. Active and saved fixed-preset startup references prevent deleting that
preset. A deleted restore-last selection leaves the output off with a boot error.
Power-cycle network recovery suppresses all startup playback and leaves the
remembered target intact; manual control remains available.

The abandoned `startup.program`, `startup.settings` and `startup.inputs` format
is rejected. Re-save startup through the current editor using off, restore last,
or a preset; put settings and input bindings in the preset's layers. For a clean
PoC install, wipe/reprovision controller storage, upload programs and presets,
and save the new configuration. There is no migration or compatibility path.

A pixel config requires a permitted string `pin` and positive integer `count`
within `pixel_capacity` for that controller. Runtime memory and native resources bound
the combined workload. `order` defaults to `GRB` and accepts the six permutations
of RGB. `brightness_limit` is an optional final output multiplier, an integer
0..255 defaulting to 255 (100%). Use rendering colour transforms for artistic
brightness changes. Logical snapshots remain unscaled RGB bytes; native drivers
apply wire order and any configured brightness reduction at submission. At 255,
drivers skip brightness arithmetic while retaining wire conversion.
WS2812B supports 800 kHz three-channel strips. Pico and host also support `apa102`:
its config requires `data_pin`, `clock_pin` and `count`, with optional active-low
`enable_pin`, `frequency` (100000..8000000 Hz; default 1000000), `order` (default
`BGR`) and `brightness_limit`. Pico uses asynchronous PIO/DMA, including the
count-dependent trailing clocks. Data, clock, enable and other output pins must
be distinct. ESP32 currently rejects APA102 and TCA9555 configurations; its
WS2812 outputs remain supported.

An output with `mode: "indicators"` reserves up to sixteen pixels for independent
RGB control through HA or the management API. It cannot have startup policies,
HA preset bindings. Animation commands and transient uploads
to it are rejected. Its normal submit/poll presenter owns one shared frame;
each indicator has on/off, brightness and RGB state, without per-pixel VMs.
Omitted mode means `animation`, including ordinary animated APA102 strips.

The optional `inputs` array accepts up to four generic `tca9555` adapters. Each
has an `id` and `config` with advertised I2C `sda` and `scl` pins, optional
`address` (32..39, default 32), `active_low` (default true), and `debounce_ms`
(5..100, default 20). All sixteen channels are configured as inputs. Pico bus
routing is checked; adapters on the same bus share its pin pair and require
distinct addresses. Bus pins cannot overlap outputs, and bus ownership is
reference counted. The driver uses 400 kHz transactions with a 1 ms deadline.
Missing hardware remains configured but unavailable, retries every second, and
does not stop other outputs. Inputs are sampled every 5 ms during normal service
and runtime checkpoints, independently of animation pause/stop. Flash operations
and other service stalls can interrupt sampling. Debounce establishes an initial
level without emitting events, including when recovering from a bus failure.

An input's optional `home_assistant` object enables sixteen press/release triggers.
Its optional `indicators` names a sixteen-pixel output in indicator mode: channel
0 pairs with pixel 0, through channel 15/pixel 15. An output may be bound once.
Optional `names` supplies exactly sixteen labels of 1..32 UTF-8 bytes for the
indicator lights; defaults are Key 1 through Key 16. IDs determine discovery
identity and trigger subtypes; labels can change without changing those IDs.
See [the keypad example](../examples/config/pico-rgb-keypad.json) and
[HA keypad setup](mqtt.md#keypad-and-rgb-indicators).

Identifiers are 1..32 ASCII letters, digits, underscores, hyphens or dots. Unknown
fields, duplicate keys, unsupported types, malformed JSON and unsuitable
pins are rejected before hardware changes. The parser is bounded to 8 KiB,
256 tokens and nesting depth 16. This reuses picolights' named-output concept;
existing picolights JSON files require conversion.

Validate does not mutate hardware. Apply requires the expected active revision,
then retains unchanged outputs and replaces changed hardware. If native initialization fails, the
active revision advances and the active configuration becomes empty. The saved
configuration is retained. Save requires matching active and saved revisions.
Clients should read configuration/discovery again after a revision conflict.

## Programs and runtime ownership

Named programs contain bytecode validated against their minimum application release. Upload streams bytes
to an immutable flash extent without replacing running instances. Finish checks length, CRC,
instruction validity and the embedded control manifest; it does not execute the
program. Activation prepares a candidate before replacing the selected instance;
failed preparation retains its prior program.

`program_activate` loads a fresh instance and plays its
[control profile](control-api.md). Its optional `output` selects a configured ID and defaults to the first
configured output; `slot` selects composition A or B on that output. The selection is explicit per request, so clients
never share a mutable current-output setting. The only activation action is `play`.
Initialization defaults live in the package. Activation/reset settings belong to
that invocation and do not alter saved presets or startup policies.
Activation replacement resets prior guest state. Storage/configuration remain
available if loading or initialization fails.

Replacing a stored name leaves an already loaded instance running its original
bytes. Discovery reports its CRC separately from catalog metadata. Delete is
blocked while a saved preset, active/saved startup configuration or resident instance refers
to that name. Saved presets also block replacing a referenced program with different
bytes; save the new version under another name. RESET unloads runtime state and cancels staging; it never erases
configuration or programs.

`VmInstance` owns interpreter state, a pinned flash extent and timing. Both
stored and transient programs read bytecode from immutable flash. Uploads share
one 256-byte page buffer; saving a replacement never copies the running package
into RAM. `PixelBuffer` and the native driver own output. Bytecode writes only its
reserved working contribution; the presenter retains each submitted frame until
completion or cancellation. Each output owns an independent VM, contribution and
output. The scheduler rotates service order; management checkpoints give each
background v3 VM at most 64 instructions per pass. Alpha composition, a separate
transition mix channel and guest output-handle imports remain proposed in the [control model](design/feature-proposal-control-api.md)
and [A/B proposal](design/feature-proposal-ab-transitions.md), with ordered
composition/layer semantics in the
[animation composition proposal](design/feature-proposal-animation-composition.md).
Compositions and layers are virtual runtime resources with their own admission limits;
they do not imply additional physical strip configuration.

## USB management v1

The current framing uses magic `LVM1`, version 1 and CRC32. Commands include STATUS (1),
transient BEGIN/CHUNK/FINISH (2–4), RESET (6), PLAY (7), STOP (8), FRAME (9), and:

| Type | Command/reply | Payload |
|---:|---|---|
| 11 | MANAGE | UTF-8 JSON operation and fields, at most 512 bytes |
| 12 | OBJECT_BEGIN | JSON: `kind` (`config` or `program`), `size`, `crc32`, and `name` for programs |
| 13 | OBJECT_CHUNK | Little-endian u32 offset followed by bytes |
| 14 | OBJECT_FINISH | Empty |
| 15 | OBJECT_CANCEL | Empty |
| 131 | DATA reply | JSON metadata/configuration or binary module bytes, determined by operation |

DATA can span frames. Concatenate all DATA for the request until its final STATUS;
discard accumulated data if STATUS indicates failure. PIXELS replies use type 130.
Errors include status 7 configuration, 8 storage and 9 upload conflict, with DATA
JSON `{error,message}`. Status 4 means execution failure. Console replies and EXIT
status are not part of this protocol.

| MANAGE operation | Additional fields / result |
|---|---|
| `describe` | Board, output types, permitted pins, capacities, active/saved/catalog revisions, active program CRC and boot/runtime diagnostics |
| `indicator_set` | `output`, zero-based `pixel`, `value` with optional `state` (`ON`/`OFF`), `brightness` (0..255) and `color: {r,g,b}` (0..255); atomically updates that light and returns its JSON state. |
| `indicator_get` | `output`, zero-based `pixel`; JSON light state, including colour while off. |
| `indicator_snapshot` | `output`; RGB bytes in PIXELS replies, after per-indicator brightness and before physical brightness limit/wire conversion. |
| `config_get`, `config_saved` | Active or persisted JSON; absent persisted config reads as empty |
| `config_validate` | Validate staged configuration, including startup preset preparation and layer argument validation |
| `config_apply` | `expected_revision` (active); apply staged config |
| `config_save` | `expected_revision` (active), `expected_saved_revision` |
| `program_list` | Optional `type` filter (`animation` or `transition`); array of `{name,type,profile,size,crc32}` |
| `program_get` | `name`, optional `type` (required for ambiguous names); raw bytecode bytes |
| `program_save` | `name`, `expected_revision` (catalog), optional `type` matching bytecode profile; persist staged module |
| `program_delete` | `name`, optional `type`, `expected_revision` (catalog) |
| `program_activate` | `name`, optional `action` (`play`, default play) |
| `output_power` | `action: "on"` or `"off"`, optional on `name`, `expected_revision` (composition); see [power transitions](transitions.md#preset-recalls-and-power). |
| `preset_list`, `preset_get`, `preset_save`, `preset_activate`, `preset_recall`, `preset_delete` | Persistent composition recipes; see [preset fields and revisions](composition.md). |

One object transfer is allowed at a time and shares the installer with transient preview uploads.
Chunks are ordered; identical already-received chunks can be retried. Conflicting
bytes and invalid offsets are rejected. Incomplete FINISH remains recoverable;
CRC/parsing failure ends the transfer. A transfer expires after 30 seconds without
an accepted chunk. Finished candidates remain available until a new object upload,
CANCEL or RESET. Revision guards prevent stale mutations; after a lost save reply,
read back revisions/content rather than blindly issuing a new save.

## Persistence and failure recovery

The board/build selects its storage backend and capacity. The default native
extent backend reserves 512 KiB unless `LIGHTVM_STORAGE_BYTES` overrides it. It
stores variable-size objects under unique `(type, name)` keys, with no program or
preset count quotas. Configuration and presets remain limited to 8192 bytes;
programs to `module_limit`.

Two alternating root sectors select an immutable catalogue that grows with the
object count. Objects use contiguous extents rounded to 4 KiB, streamed through
256-byte programming pages. Running instances and staged uploads pin their
versions. `describe` reports the actual backend, writable capability, total/free
capacity, allocation unit and largest free extent. Fragmentation and the room
needed for a replacement catalogue can prevent an upload despite free bytes.

Publication verifies the new object and catalogue before committing the inactive
root. Interrupted writes preserve the current committed generation. Unsupported
or corrupt committed stores remain unavailable until explicitly reprovisioned.
Only the first-release `LVS2` format 1 is implemented; experimental earlier stores
must be erased. See [internal storage](storage.md) for backend integration,
configuration, format and recovery details.

Saving configuration validates each fixed startup preset before committing it.
Replacing a preset leaves running instances on their original bytes; the next
boot resolves the current preset definition and its pinned program versions. Activation does not
promote or save a boot revision. Hardware health still requires controller testing.

Pico reserves the configured region at the end of its board's flash; the linker
rejects overlapping firmware. ESP32 generates the `lightvm` partition with the
requested capacity, or uses `LIGHTVM_PARTITION_TABLE` for a custom layout. The
runtime reads its actual partition size. Capacity changes require reprovisioning
and, on ESP32, installing the corresponding partition table.

Persistence services playback and native ingress between sector erases and page
writes. Individual flash operations still interrupt XIP/cache access; ESP32 drains
an RMT transfer before modifying flash. Continuous frame timing during flash
operations is not promised. Allocation reuses free extents but does not implement
wear leveling. Source projects and firmware OTA remain future work. Preserve the storage region when flashing firmware; a whole-chip
erase intentionally removes it. A bad startup reference, initialization failure,
load error or trap leaves management available for repair.

## Verification record

On 2026-10-19, the preset startup firmware was built for Pico W with Wi-Fi,
HTTP, WebSocket, MQTT, boot-sequence detection and a 1 MiB store. The UF2 passed
the storage-boundary check and was flashed with readback verification. On the
attached 200-pixel GP16 output, real reboots verified selected-preset startup,
off startup, and restore-last with both ON and OFF while retaining the Stars
selection. The controller was left configured to start its Rainbow preset.
One final software reboot lost USB enumeration; a USB unplug/reconnect recovered
it. That cold boot confirmed advancing Rainbow frames, no boot/animation errors,
and connected Wi-Fi and MQTT. Existing programs, presets and network settings
remained available. Full power-removal during a flash write was not tested.

The preset-only transition firmware rejects the abandoned output override fields.
A native regression test delays output scheduling by five seconds and verifies
that the full startup transition still renders from black. The firmware was
flashed with readback verification on the Pico W. Rainbow and Stars are configured
with a saved 1.5-second `crossfade`. Selection, OFF and ON were verified for both
presets, followed by Rainbow startup after reboot. Pixel samples confirmed the
whole strip fades in brightness, reaching black on OFF and full output on ON and
startup. Wi-Fi and MQTT reconnected after the reboot. Host recall, startup, MQTT
and recovery tests and browser acceptance checks passed.

The October 15 architecture correction adds fault-injected page/directory commits,
unsupported-directory rejection from both banks, pinned old revisions through repeated
replacements, rejected/cancelled transient uploads, per-output startup settings and
retained asynchronous output tests. The current host suite runs under address
and undefined-behavior sanitizers. Pico W and ESP32 C3/S3/C6 cross-build checks
cover the new code; the hardware record below predates these driver changes.


On 2026-10-12, 58 Python integration tests passed on each host profile, together
with the C contract test. The C test interrupts snapshot operations at 90 points
and verifies complete old/new recovery, exercises independent buffers/instances,
checks allocation-free repeated frames and simulates driver initialization failure.
Pico and both ESP32 firmware builds passed; the reserved Pico boundary rejects
an overlapping link, and both ESP32 partition tables were decoded and checked.

The connected original Pico passed save/readback, RAM apply and persistent save,
RESET retention, portable USB smoke checks and automatic startup after a firmware
reload/reboot with the same UF2. The saved 100-pixel GP16 configuration and named
rainbow survived, with no boot error. No ESP32 board was available for hardware
checks. A cold power-removal test and physical interrupted writes have not been
performed; interrupted-write recovery was verified with the simulated backend.

## Simultaneous output test

Keep the existing physical Rainbow animation on GP16, then run five additional
physical GPIO outputs (Color, Pattern, Rainbow Sprite, Sweep and Stars):

```sh
python tools/smoke_multi_output.py --port PORT --reference build/host/lightvm \
  --pixels 100 --seconds 60
```

The test checks host parity at supplied render deltas, six independent output
clocks, stable aggregate VM heap and per-output pause/resume. Saved configuration
and the program library remain unchanged. The six outputs are left running in
RAM after the test; reboot restores the saved configuration. Check the tool's GPIO
assignments against actual wiring before using it on hardware.

Use output IDs for upload, activation, status, frame, stop, pause/resume and
snapshot operations:

```sh
python tools/client.py --port PORT --output desk build/examples/sweep.lbc --animate
python tools/client.py --port PORT --output desk --program-state
python tools/client.py --port PORT --output desk --pause
```

Commands without an output select the first configured output. BEGIN accepts its
original eight-byte size/CRC payload or those bytes followed by an ASCII output
ID. The shared upload transaction keeps that destination until FINISH or
cancellation. Output-scoped binary operations use MANAGE
`{"op":"output_command","output":"desk","command":1}`. Supported command IDs
are STATUS (1), RESET (6), PLAY (7), STOP (8) and FRAME (9, with `delta_ms`). Each
output has composition slots A and B; global B–H output bindings are unsupported.

## Home Assistant bindings

Transition settings belong to each preset. The same saved invocation supplies
preset selection, ON, OFF to black and startup. Output-level `on_transition` and
`off_transition` fields are rejected. Remove those abandoned fields and edit the
preset transition in the current editor; there is no migration or compatibility
path. See [recall transitions](transitions.md#preset-recalls-and-power).

Each output may include a `home_assistant` object with `default_preset`, an
explicit `presets` list and an optional `name`. It exposes one on/off light and
one Preset selector for that output. The default must belong to its 1–16 unique
preset names. MQTT broker settings remain separate in `lightvm.network`.
See [MQTT control and Home Assistant presets](mqtt.md) for configuration, topics,
selection state and power behavior.
