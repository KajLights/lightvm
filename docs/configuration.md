# Configuration and stored procedures

Firmware contains board capabilities and resource ceilings. Connected devices,
GPIO selection, strip length, RGB wire order and output brightness are configured
as data. Each named device owns an independent animation instance. `ws2812b`
uses physical GPIO. Device count is bounded by available pins, configuration size
and runtime resources. No peripheral or startup effect is implicit. A controller
with no saved configuration starts idle and remains available over USB.

## Provisioning

Build the independent samples with `python tools/build_examples.py`, then store a
procedure before referring to it in saved configuration:

```sh
python tools/client.py --port PORT --describe
python tools/client.py --port PORT --store-procedure rainbow build/examples/rainbow.lbc
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
reordered. Changed or removed devices are stopped and unloaded. Startup-only
edits do not restart an animation. Saving is a separate operation and does not
start playback. After reboot, each device's optional startup invocation loads a
fresh instance with its own arguments. A device without startup stays idle.
Remove `--save` for temporary wiring experiments; `--save-config` explicitly saves
the current active configuration.

Other useful commands:

```sh
python tools/client.py --port PORT --get-config
python tools/client.py --port PORT --get-config --saved
python tools/client.py --port PORT --procedures
python tools/client.py --port PORT --get-procedure rainbow --output rainbow.lbc
python tools/client.py --port PORT --delete-procedure unused
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

See [the Pico example](../examples/config/pico-startup.json). The only root fields are
required `schema` and `devices`. The devices array may be empty. Device IDs and
physical pins must be unique. Root-level `targets` and `startup` are rejected,
including empty or null forms. Previously saved configurations using those fields
report a boot error and start with empty active configuration; replace them through
the normal configuration interface. Stored procedures remain available.

Each device requires `id`, `type` and `config`, and may provide one startup object:

```json
"startup": {
  "procedure": "rainbow",
  "args": {"duration": 3.3}
}
```

`procedure` names a stored program. Optional `args` is an object of named
initialization arguments: supplied values are passed before initialization and
omitted values use program defaults. Omitted `args` and `{}` are equivalent.
Arguments are checked against the program's names, types and bounds using that
device's configured pixel count. There is no separate argument store or remembered
last-used value. The current compiler syntax for declaring defaults is unchanged.
Omit `startup` to leave the device idle; `startup: null` is invalid.

Validation, application and saving prepare isolated startup candidates without
opening hardware or replacing running instances. Errors identify the device and,
for invalid arguments, the argument name. Startup resolves each name through the
current procedure catalogue. A failed or missing program at boot is reported
without clearing configuration or preventing other startups. Referenced programs
cannot be deleted. Power-cycle network recovery keeps startup playback idle;
manual activation remains available.

A pixel config requires a permitted string `pin` and positive integer `count`
within `pixel_capacity` for that device. Runtime memory and native resources bound
the combined workload. `order` defaults to `GRB` and accepts the six permutations
of RGB. `brightness_limit` is an optional final output multiplier, an integer
0..255 defaulting to 255 (100%). Use rendering colour transforms for artistic
brightness changes. Logical snapshots remain unscaled RGB bytes; native drivers
apply wire order and any configured brightness reduction at submission. At 255,
drivers skip brightness arithmetic while retaining wire conversion.
Only 800 kHz, three-channel WS2812B is supported. Virtual outputs,
RGBW and pixel subsets are not implemented.

Identifiers are 1..32 ASCII letters, digits, underscores, hyphens or dots. Unknown
fields, duplicate keys, unsupported types, malformed JSON and unsuitable
pins are rejected before hardware changes. The parser is bounded to 8 KiB,
256 tokens and nesting depth 16. This reuses picolights' named-device concept;
existing picolights JSON files require conversion.

Validate does not mutate hardware. Apply requires the expected active revision,
then retains unchanged devices and replaces changed hardware. If native initialization fails, the
active revision advances and the active configuration becomes empty. The saved
configuration is retained. Save requires matching active and saved revisions.
Clients should read configuration/discovery again after a revision conflict.

## Procedures and runtime ownership

Named procedures contain bytecode validated against their minimum application release. Upload streams bytes
to an immutable flash extent without replacing running instances. Finish checks length, CRC,
instruction validity and the embedded control manifest; it does not execute the
program. Activation prepares a candidate before replacing the selected instance;
failed preparation retains its prior program.

`procedure_activate` loads a fresh instance and plays its
[control profile](control-api.md). Its optional `device` selects a configured ID and defaults to the first
configured device; `slot` is local to that device and supports only A. The selection is explicit per request, so clients
never share a mutable current-device setting. The only activation action is `play`.
Initialization defaults live in the package. Activation/reset arguments belong to
that invocation and do not alter `devices[].startup.args`.
Activation replacement resets prior guest state. Storage/configuration remain
available if loading or initialization fails.

Replacing a stored name leaves an already loaded instance running its original
bytes. Discovery reports its CRC separately from catalog metadata. Delete is
blocked while a saved preset, active/saved startup configuration or resident instance refers
to that name. Saved presets also block replacing a referenced program with different
bytes; save the new version under another name. RESET unloads runtime state and cancels staging; it never erases
configuration or procedures.

`VmInstance` owns interpreter state, a pinned flash extent and timing. Both
stored and transient programs read bytecode from immutable flash. Uploads share
one 256-byte page buffer; saving a replacement never copies the running package
into RAM. `PixelBuffer` and the native driver own output. Bytecode writes only its
reserved working contribution; the presenter retains each submitted frame until
completion or cancellation. Each device owns an independent VM, contribution and
output. The scheduler rotates service order; management checkpoints give each
background v3 VM at most 64 instructions per pass. Alpha composition, a separate
transition mix channel and guest device-handle imports remain proposed in the [control model](design/feature-proposal-control-api.md)
and [A/B proposal](design/feature-proposal-ab-transitions.md), with ordered
scene/layer semantics in the
[animation composition proposal](design/feature-proposal-animation-composition.md).
Scenes and layers are virtual runtime resources with their own admission limits;
they do not imply additional physical strip configuration.

## USB management v1

The current framing uses magic `LVM1`, version 1 and CRC32. Commands include STATUS (1),
transient BEGIN/CHUNK/FINISH (2–4), RESET (6), PLAY (7), STOP (8), FRAME (9), and:

| Type | Command/reply | Payload |
|---:|---|---|
| 11 | MANAGE | UTF-8 JSON operation and fields, at most 512 bytes |
| 12 | OBJECT_BEGIN | JSON: `kind` (`config` or `procedure`), `size`, `crc32`, and `name` for procedures |
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
| `describe` | Board, device types, permitted pins, capacities, active/saved/catalog revisions, active procedure CRC and boot/runtime diagnostics |
| `config_get`, `config_saved` | Active or persisted JSON; absent persisted config reads as empty |
| `config_validate` | Validate staged configuration, including startup procedure preparation and argument validation |
| `config_apply` | `expected_revision` (active); apply staged config |
| `config_save` | `expected_revision` (active), `expected_saved_revision` |
| `procedure_list` | Optional `type` filter (`animation` or `transition`); array of `{name,type,profile,size,crc32}` |
| `procedure_get` | `name`, optional `type` (required for ambiguous names); raw bytecode bytes |
| `procedure_save` | `name`, `expected_revision` (catalog), optional `type` matching bytecode profile; persist staged module |
| `procedure_delete` | `name`, optional `type`, `expected_revision` (catalog) |
| `procedure_activate` | `name`, optional `action` (`play`, default play) |
| `preset_list`, `preset_get`, `preset_save`, `preset_activate`, `preset_delete` | Persistent scene recipes; see [preset fields and revisions](composition.md). |

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

Saving configuration validates every startup invocation using its supplied
arguments before committing the configuration. Replacing a stored procedure leaves
running instances on their original bytes, while the next boot uses the current
catalogue entry with each device's configured arguments. Activation does not
promote or save a boot revision. Hardware health still requires device testing.

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

The October 15 architecture correction adds fault-injected page/directory commits,
unsupported-directory rejection from both banks, pinned old revisions through repeated
replacements, rejected/cancelled transient uploads, per-device startup arguments and
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

## Simultaneous device test

Keep the existing physical Rainbow animation on GP16, then run five additional
physical GPIO devices (Color, Pattern, Rainbow Sprite, Sweep and Stars):

```sh
python tools/smoke_multi_device.py --port PORT --reference build/host/lightvm \
  --pixels 100 --seconds 60
```

The test checks host parity at supplied render deltas, six independent output
clocks, stable aggregate VM heap and per-device pause/resume. Saved configuration
and the procedure library remain unchanged. The six outputs are left running in
RAM after the test; reboot restores the saved configuration. Check the tool's GPIO
assignments against actual wiring before using it on hardware.

Use device IDs for upload, activation, status, frame, stop, pause/resume and
snapshot operations:

```sh
python tools/client.py --port PORT --device desk build/examples/sweep.lbc --animate
python tools/client.py --port PORT --device desk --program-state
python tools/client.py --port PORT --device desk --pause
```

Commands without a device select the first configured device. BEGIN accepts its
original eight-byte size/CRC payload or those bytes followed by an ASCII device
ID. The shared upload transaction keeps that destination until FINISH or
cancellation. Device-scoped binary operations use MANAGE
`{"op":"device_command","device":"desk","command":1}`. Supported command IDs
are STATUS (1), RESET (6), PLAY (7), STOP (8) and FRAME (9, with `delta_ms`). Each
device currently has local slot A; global B–H bindings are unsupported.

## Home Assistant bindings

Each output may include `home_assistant:{"on_preset":"evening","name":"Desk lamp"}`.
Enable the optional MQTT build/profile and broker settings to expose one on/off
light per selected output. See [MQTT and Home Assistant lights](mqtt.md) for
configuration, power behavior and topic contracts.
