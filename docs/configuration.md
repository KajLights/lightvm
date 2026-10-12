# Configuration and stored procedures

Firmware contains board capabilities and resource ceilings. Connected devices,
GPIO selection, strip length, RGB wire order and output brightness are configured
as data. The initial registry supports one named `ws2812b` device and one render
target, `A`. No peripheral or startup effect is implicit. A controller with no
saved configuration starts idle and remains available over USB.

## Provisioning

Build the independent samples with `python tools/build_samples.py`, then store a
procedure before referring to it in saved configuration:

```sh
python tools/client.py --port PORT --describe
python tools/client.py --port PORT --store-procedure rainbow build/samples/rainbow.wasm
python tools/client.py --port PORT --validate-config examples/pico-startup.json
python tools/client.py --port PORT --config examples/pico-startup.json --save
python tools/client.py --port PORT --activate rainbow
```

For either XIAO board use `--no-reset` and `examples/xiao-startup.json`.
`GP16` is a Pico pin name; `D0` is a XIAO board alias. Discovery lists every
permitted pin, including XIAO `GPIO` aliases. These tables describe exposed pins
and exclude USB, flash and board-reserved pins; they do not identify connected
hardware. XIAO mappings follow the manufacturer's [S3 pin map](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)
and [C6 pin map](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/).

Configuration apply takes effect in RAM and stops/unloads the current instance.
Saving is a separate operation and does not start playback. After reboot, the
stored startup reference is loaded into a fresh instance and played on A. A
configuration with no `startup` stays idle. Remove `--save` for temporary wiring
experiments; `--save-config` explicitly saves the current active configuration.

Other useful commands:

```sh
python tools/client.py --port PORT --get-config
python tools/client.py --port PORT --get-config --saved
python tools/client.py --port PORT --procedures
python tools/client.py --port PORT --get-procedure rainbow --output rainbow.wasm
python tools/client.py --port PORT --activate counter --action run --function run
python tools/client.py --port PORT --delete-procedure unused
python tools/client.py --port PORT --cancel-upload
```

For host persistence, replace `--port PORT` with
`--process build/host/lightvm --storage build/demo-store.bin`. The receiver locks
the storage file against simultaneous writers. Without `--storage`, the host
supports RAM configuration and transient WASM uploads, and reports persistence
unavailable. Each invocation creates a new host process; persisted state survives
that restart, while unsaved state does not.

## JSON configuration v1

See [the Pico example](../examples/pico-startup.json). `schema` and `devices` are
required. `devices` can be empty. `targets` is optional; its only supported key is
`A`, whose `device` must name the configured pixel instance. `startup` is optional
or null; when provided, it contains a stored `procedure` name and target `A`.

Each device requires `id`, `type` and `config`. A pixel config requires a permitted
string `pin` and positive integer `count` within `pixel_capacity`. `order` defaults
to `GRB` and accepts the six permutations of RGB. `brightness_limit` is an integer
0..255, default 255. Logical snapshots remain unscaled RGB bytes; native drivers
apply wire order and brightness at submission. Only 800 kHz, three-channel
WS2812B is supported. RGBW and pixel subsets are not implemented.

Identifiers are 1..32 ASCII letters, digits, underscores, hyphens or dots. Unknown
fields, duplicate keys, unsupported types/targets, malformed JSON and unsuitable
pins are rejected before hardware changes. The parser is bounded to 8 KiB,
256 tokens and nesting depth 16. This reuses picolights' named-device concept;
existing picolights JSON files require conversion.

Validate does not mutate hardware. Apply requires the expected active revision,
then stops the instance and tears down old drivers. If initialization fails, the
active revision advances and the active configuration becomes empty. The saved
configuration is retained. Save requires matching active and saved revisions.
Clients should read configuration/discovery again after a revision conflict.

## Procedures and runtime ownership

Named library procedures are complete WASM modules. Upload stages bytes without
replacing the running instance or writing flash. Finish checks length, CRC and
wasm3 parsing; it does not execute guest initialization or guarantee activation.
Linking, compilation, supported imports, resources and export signatures are
checked when selecting a procedure. A stored module can therefore fail to load
or play; the error does not remove it from the library.

`procedure_activate` loads a fresh instance and either plays its existing API v1
callbacks or calls a zero-argument export returning void or i32. The default
export for `run` is `run`. Further legacy RUN calls retain that instance's state.
Activation replacement is destructive and resets the prior guest state. Once
replacement begins, a load or initialization failure leaves the runtime idle; storage and configuration remain available.

Replacing a stored name leaves an already loaded instance running its original
bytes. Discovery reports its CRC separately from catalog metadata. Delete is
blocked while active/saved startup configuration or the resident instance refers
to that name. RESET unloads runtime state and cancels staging; it never erases
configuration or procedures. Native-demo is an explicit diagnostic on configured A.

`VmInstance` owns module bytes, wasm3 state, bindings and timing. `PixelBuffer`
owns packed RGB and its native driver; imports resolve through the instance's
target pointer. Output submission is separate from rendering. The aggregate VM
heap stays bounded, and one engine owner serializes all work. Tests instantiate
two independent memory-backed targets/VMs to verify ownership; production exposes
only A. B, a separate U8 mix channel, composition and guest device-handle imports
remain proposed in the [control model](../documents/feature-proposal-control-api.md)
and [A/B proposal](../documents/feature-proposal-ab-transitions.md).

## USB management v1

Existing commands 1..10 and protocol v1 framing are retained. New messages are:

| Type | Command/reply | Payload |
|---:|---|---|
| 11 | MANAGE | UTF-8 JSON operation and fields, at most 512 bytes |
| 12 | OBJECT_BEGIN | JSON: `kind` (`config` or `procedure`), `size`, `crc32`, and `name` for procedures |
| 13 | OBJECT_CHUNK | Little-endian u32 offset followed by bytes |
| 14 | OBJECT_FINISH | Empty |
| 15 | OBJECT_CANCEL | Empty |
| 131 | DATA reply | JSON metadata/configuration or binary module bytes, determined by operation |

DATA can span frames. Concatenate all DATA for the request until its final STATUS;
discard accumulated data if STATUS indicates failure. Console OUTPUT and RGB
PIXELS retain their distinct reply types. New errors include status 7 configuration,
8 storage and 9 upload conflict, with DATA JSON `{error,message}`. Legacy statuses
retain their meanings.

| MANAGE operation | Additional fields / result |
|---|---|
| `describe` | Board, device types, permitted pins, capacities, active/saved/catalog revisions, active procedure CRC and boot/runtime diagnostics |
| `config_get`, `config_saved` | Active or persisted JSON; absent persisted config reads as empty |
| `config_validate` | Validate staged configuration, including startup reference existence |
| `config_apply` | `expected_revision` (active); apply staged config |
| `config_save` | `expected_revision` (active), `expected_saved_revision` |
| `procedure_list` | Array of `{name,size,crc32}` |
| `procedure_get` | `name`; raw WASM bytes |
| `procedure_save` | `name`, `expected_revision` (catalog); persist staged module |
| `procedure_delete` | `name`, `expected_revision` (catalog) |
| `procedure_activate` | `name`, optional `action` (`play`/`run`, default play), optional `export` (default run) |

One object transfer is allowed at a time and conflicts with legacy uploading.
Chunks are ordered; identical already-received chunks can be retried. Conflicting
bytes and invalid offsets are rejected. Incomplete FINISH remains recoverable;
CRC/parsing failure ends the transfer. A transfer expires after 30 seconds without
an accepted chunk. Finished candidates remain available until a new object upload,
CANCEL or RESET. Revision guards prevent stale mutations; after a lost save reply,
read back revisions/content rather than blindly issuing a new save.

## Persistence and failure recovery

The 512 KiB region contains two 256 KiB snapshot banks. Each has a versioned,
CRC-protected directory, configuration revision, generation and up to nine entries
(one configuration plus eight procedures). Payloads start at offset 4096 and use
256-byte alignment. Configuration is limited to 8192 bytes; incoming procedures
are limited by `module_limit`. Aggregate capacity includes alignment: eight maximum
32 KiB procedures cannot all fit in one bank. `free_bytes` reports usable remaining
payload space before catalog-count limits.

A save erases the inactive bank, copies retained objects and the replacement,
verifies data CRCs and directory bytes, synchronizes, then writes a final commit
record and synchronizes again. The old bank remains intact. Interrupted writes
recover the newest complete snapshot; invalid snapshots are ignored. If neither
bank is valid, boot stays idle and reports a storage error; explicit saving can
repair the store. No previous configuration is tried after a valid snapshot's
startup module fails. Validation alone cannot establish physical hardware health.

Pico reserves flash offsets `0x180000..0x1fffff` on the original 2 MiB board. The
linker rejects firmware crossing that boundary. Flash programming copies pages to
RAM and disables interrupts around the SDK operation; the firmware uses one core.
[SDK flash restrictions](https://www.raspberrypi.com/documentation/pico-sdk/hardware.html#hardware_flash).
ESP32 reserves the `lightvm` data partition at `0x110000`, size `0x80000`, after the
1 MiB application partition. Partition operations report I/O errors without
aborting the controller. [ESP-IDF partition API](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/storage/partition.html).

Explicit persistence may briefly pause rendering. The last submitted frame is
retained and guest state resumes without counting the flash-write gap. Continuous
flash wear leveling, source projects, network transport and firmware OTA are not
implemented. Preserve the storage region when flashing firmware; a whole-chip
erase intentionally removes it. A bad startup reference, initialization failure,
load error or trap leaves management available for repair.

## Verification record

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
