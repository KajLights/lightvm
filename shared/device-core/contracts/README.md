# Shared device contracts

LightVM and kaj-fi expose the common members in `device.schema.json` through
the existing `MANAGE {"op":"describe"}` operation. Application-specific fields
remain in that same response. JSON Schemas use draft 2020-12; document kinds do
not introduce application release counters or feature-version floors.

`features.json` registers compiled feature IDs and their dependencies. Unknown
optional features may be ignored by a client; build generation rejects unknown
selections and missing declared dependencies. Runtime status remains separate
from compiled support. In particular `audio_gateway` describes the currently
linked structured command gateway, while `audio_subscriptions` selects native
leased UDP reception. Removing every audio implementation from standard builds
is a subsequent build-profile task; descriptions must report the current code.

The shared CMake build generator writes `device-build/build.json` alongside its
generated C definitions. `build.schema.json` describes that record. Its build ID
hashes canonical source bytes/revisions, feature selection, compiler, relevant
CMake settings, SDK configuration and partition inputs. Its feature digest hashes
the sorted compact JSON feature list. Rebuilding after source/settings changes
refreshes these definitions; unchanged inputs do not rewrite the generated files.
Development builds report dirty source and have no release tag. Application
versions remain authoritative in their owning projects.
Build metadata requires Git checkouts for the project, shared core and selected
SDK, matching the pinned checkout sources used by current CI. An SDK directory
inside another repository is not evidence of that SDK's revision.

Hardware `declared_flash_bytes` is a build declaration. `flash_bytes` remains
null and `flash_verified` is false until a platform explicitly measures physical
flash. Firmware/installer consumers must not equate those facts. The current
descriptions report OTA unsupported, with only their existing local installation
methods. Layout IDs identify current local storage/partition inputs and are not
a claim of a validated OTA boot contract.

## Device metadata

`metadata-request.schema.json` describes `device_metadata_get` and
`device_metadata_set`. Successful responses use `metadata.schema.json`.
Set requires the current `expected_revision` and `expected_boot_id`; clients
must reconcile uncertain results by reading state, without blind mutation retry.

A name is null or valid UTF-8. Trim ASCII spaces at the ends, canonicalise an
empty value to null, reject ASCII controls and limit the canonical value to
63 UTF-8 bytes. JSON Schema string lengths count characters, so the byte limit
and canonicalisation are additionally enforced by the C service and browser
client. Unicode names, quotes and backslashes round-trip without HTML parsing.

The shared store reserves type 8 and object name `$device`. Its record is small
and independent of application/network settings. Missing metadata means an unset
name and does not write at boot. Writes use existing atomic storage, and report
storage failure/read-only state without claiming the name changed. The direct
description reports metadata errors and revision. The bridge's discovery list
can report its own current name; peer names are null until obtained directly.
Names are never routing keys and do not change device or Home Assistant IDs.

Both Python clients expose `set_device_nickname`; the browser client exposes
`deviceMetadata` and `setDeviceNickname`. kaj-fi's `tools/configure.py name NAME`
saves a name; omit NAME to clear it. The existing LightVM workspace exposes the
same action under Configuration, Output and startup. A name survives reboot and
compatible firmware replacement. A storage wipe removes it; no migration from
incompatible experimental state is provided.

`firmware.schema.json` describes a complete target/profile bundle and
`release.schema.json` describes a deliberate product release containing the
entire approved matrix. Both use `manifest_version: 1` for the document format;
this does not advance application versions. They embed the existing build
contract and retain its exact identity, rather than inferring capabilities from
a release label. See [release production and discovery](../../../docs/releases.md).
The shared `tools/release.py` packages linked outputs; `discover_releases.py`
verifies public published releases in website CI. Website recommendation,
withdrawal and mirroring policy, tool bundles and OTA transactions remain their
respective implementation stages.
