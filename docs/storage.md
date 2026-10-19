# Internal object storage

Application code uses `store.h`. Names are unique within a `StoreType`:
`animation/wipe`, `transition/wipe` and `preset/wipe` are separate objects.
Animations, transitions, presets, controller configuration and network settings
share the backend's capacity. There are no program or preset count quotas.

The default `store_extent_backend` manages native flash. `store_assets_backend`
serves immutable firmware assets through the same lookup and VM read API. A board
or use case may supply a different `StoreBackend`, including a filesystem adapter.
No filesystem library is required by the application or interpreter.

## Contract

- `store_find(store, type, name)` and `store_list(store, type, &cursor)` return
  metadata. A listing cursor starts at zero. `STORE_ANY` lists every type; a
  name-only find succeeds only if exactly one type has that name.
- Catalogue pointers and cursors last until a mutation. Copy a `StoreEntry` and
  call `store_pin` before retaining its version across mutations. Release the
  returned pin with `store_unpin`. The entry's `id` is an opaque backend token.
- `store_read` performs bounded reads at an offset. The VM retains its entry and
  uses its existing instruction cache; rendering performs no name lookups.
- `store_upload_begin`, `store_upload_chunk` and `store_upload_finish` stage a
  private object. Identical chunks may be retried. Finish checks integrity but
  does not publish. `store_publish` atomically replaces the exact `(type, name)`
  key. `store_upload_cancel` releases the staging resources, including after a
  successful publication. `store_put` combines these steps for resident data.
- `store_delete` removes a key. Pinned versions remain readable until released.
  Application policy separately protects references from presets/startup/playback.
- `store_info` reports actual capacity, free space, largest contiguous free extent,
  allocation unit and whether the backend accepts writes. Free bytes are not a
  promise that a particular upload fits: staging, metadata and fragmentation also
  matter. Read-only backends reject writes without modifying their contents.
- `store_destroy` releases a mounted store after uploads and readers have closed.
  Destroy before remounting. Mount failures never automatically erase media.

`store_backend.h` defines the implementation interface. Backends own their private
state and version tokens. Publication/deletion must be atomic; revisions change
only on success. Optional retain/release callbacks can hold filesystem handles or
immutable versions. Callbacks must not re-enter storage on the same instance.
Backend configuration and asset payloads must remain valid for the mounted store's
lifetime. Directory operations and file reads are separate, so a filesystem backend
can keep file handles open without searching directories during rendering.

## Selecting a backend

The shared CMake options apply to host, Pico and ESP32 builds:

| Option | Default | Purpose |
| --- | --- | --- |
| `LIGHTVM_STORAGE_BYTES` | `524288` | Native flash region, in bytes; at least 16 KiB and a multiple of 8 KiB. |
| `LIGHTVM_STORAGE_BACKEND` | `store_extent_backend` | `StoreBackend` symbol used by `store_mount`. |
| `LIGHTVM_STORAGE_BACKEND_SOURCES` | empty | Additional backend or board configuration C sources. |
| `LIGHTVM_STORAGE_CONFIGURATION` | empty | Optional `const void *const` symbol pointing to backend configuration. |

For example, `-DLIGHTVM_STORAGE_BYTES=1048576` reserves a 1 MiB region. This is a
build/provisioning setting, not editable controller JSON. Select a size that leaves
room for firmware and any boot, OTA or other partitions. Backends report actual
allocated capacity rather than assuming this default.

For a read-only product, a board source can define:

```c
#include "store_backend.h"
static const StoreAsset files[] = {
    { STORE_ANIMATION, "wipe", wipe_bytecode, sizeof(wipe_bytecode) },
};
static const StoreAssets assets = { files, sizeof(files) / sizeof(files[0]) };
const void *const board_storage_configuration = &assets;
```

Build with `LIGHTVM_STORAGE_BACKEND=store_assets_backend`,
`LIGHTVM_STORAGE_CONFIGURATION=board_storage_configuration`, and that source in
`LIGHTVM_STORAGE_BACKEND_SOURCES`. Include the bytecode array in the board source.
For explicit instance configuration, use `store_mount_with`. The extent backend
accepts an optional `StoreBlockDevice` with capacity, context and read/write/erase/
sync callbacks; NULL selects the native platform region. Its geometry is currently
4 KiB erase units and 256-byte programming units.

Pico places storage at the end of the SDK board's declared flash. Absolute linker
symbols use the same board geometry as the flash driver and reject overlapping
firmware. Each build exports `storage-layout.json`; firmware packaging and the
browser installer use its linked boundary when validating the UF2. The optional boot journal occupies 8 KiB immediately before storage.

ESP32 generates its partition table from the requested capacity, reserving a
2 MiB application partition at `0x10000` and placing storage at `0x210000`.
The optional boot journal follows the store. `LIGHTVM_PARTITION_TABLE` may instead
name a custom CSV with a `lightvm` data partition and, when enabled, a
`lightvm_boot` partition. The backend uses the actual `lightvm` partition size.
ESP-IDF validates overlaps and
physical flash bounds. When updating from the earlier 1 MiB application layout,
erase flash with `idf.py -C platforms/esp32 -B BUILD_DIR erase-flash`, then flash
the rebuilt firmware and partition table and provision configuration, programs
and presets again. Changing partition addresses likewise requires a clean install.
The host's backing file must have exactly the configured size.

## Native extent format

The first release uses only `LVS2`, format 1. Earlier experimental stores require
explicit erase/reprovisioning. There is no migration or compatibility reader.
Changing the region size or backend likewise requires clean provisioning.

Two 4 KiB root sectors, at zero and halfway through the region, select the latest
committed generation. Each root contains revisions, the region size and a CRC-
protected reference to an immutable catalogue extent. Its 64-byte records hold
name, type, backend version token, size and payload CRC. The catalogue grows with
the number of stored objects, and can span multiple sectors. RAM metadata grows
with the actual entry count, not the reserved flash capacity.

Payloads and catalogues share a sector allocator. Payloads are variable length,
rounded to 4 KiB, and currently require contiguous space. This backend does not
pack multiple small files into one sector and does not implement wear levelling.
Those policies can change in a backend without changing the application API.

A save streams and verifies the new payload, writes and verifies a new catalogue,
then writes the inactive root and its final commit record. The previous committed
root and its objects remain usable during an interrupted update. Running and
staged versions pin their extents. Later allocation may reclaim extents referenced
only by an older root, so old roots are fallback candidates only while their data
remains intact. Saving needs room for the new object and replacement catalogue
while the old generation is still live.

## Management and clients

`describe` advertises `typed_storage`, `storage_backend`, `storage_writable`,
`storage_bytes`, `free_bytes`, `largest_free_bytes` and `storage_allocation_unit`.
There are no `program_limit` or `preset_limit` fields.

`program_list` optionally accepts `type: "animation"` or `"transition"`; every
entry returns both `type` and `profile`. `program_get` and `program_delete`
accept the same type selector. Omitting it is allowed for an unambiguous program
name; otherwise the request fails with `ambiguous_name`. Presets have their own
namespace and do not participate in program-name ambiguity.

The uploaded bytecode profile determines the type of `program_save`. An optional
explicit type must match it. Replacement affects only that typed key. Animation
startup/layer references and transition selection already supply their type by
context. The Python client supports `programs(program_type)`,
`get_program(name, program_type)` and `delete_program(name, program_type)`;
the CLI exposes `--program-type`. The browser client and editor preserve the type
when selecting, retrieving and deleting stored programs.

## Remembered output targets

The private `STORE_OUTPUT_STATE` type (6) uses the output ID as its key and stores
one power byte followed by a zero-terminated, zero-padded 33-byte preset name.
These targets are separate from hardware/network configuration and never appear
in program or preset lists. Accepted preset/power commands atomically replace a
changed target before playback changes. Saving restore-last can capture the
current recalled target; repeated targets and boot do not write.
The next restore-last startup resolves the current named preset. Records do not
pin deleted presets or contain live VM state. They share normal store capacity
and advance the catalogue generation, without changing configuration revisions.

## Keypad press-feedback settings

The private `STORE_KEY_FEEDBACK` type (7) uses the input ID as its key and stores
a two-byte little-endian bitmask (bit 0 is Key 1, bit 15 is Key 16). Missing records
mean feedback is disabled for every key. Accepted changes persist atomically before
the live setting changes; failed saves leave the previous setting active. Repeated
values, key presses, blinking, boot and HA reconnects do not write. Records consume
normal store capacity and advance the catalogue generation, without changing
hardware/network configuration revisions or appearing in program/preset lists.
They survive input/output rebinding and removal; disable a key's feedback through HA
or the management API to save its disabled state. Light targets/effects remain transient.

The current firmware reads existing stores without a migration. Earlier firmware
that does not recognize type 7 cannot read catalogues containing these records;
wipe storage and reprovision if returning to such an experimental build.
