# OTA firmware updates and build discovery

Status: Proposed

Date: 30 October 2026

## Goal

Let people upgrade, downgrade and change supported firmware features from the
hosted Kajlights web interface. The interface must read the installed build and
its compiled features before choosing an update, then verify the resulting
installation after the device restarts.

The main website owns the release catalogue, installation experience and user
guidance. Firmware projects own their update implementations and produce the
artifacts and compatibility metadata consumed by that website. Shared discovery
and update contracts belong with [device-core](../../shared/device-core/README.md),
without making kaj-fi depend on the lighting engine.

**Pico W OTA is an optional build feature and remains excluded from its standard
firmware.** MQTT/Home Assistant and audio remain independent optional features.
OTA must not require either integration. Other boards also report whether OTA
is present in the installed build; their default selection should follow their
measured capacity and agreed build profiles.

This proposal extends [build profiles](feature-proposal-build-profiles.md),
[storage](../storage.md), [networking](../networking.md) and
[application versioning](../versioning.md). It specifies future behavior;
current LightVM firmware has no OTA update implementation.

## Device scope

| Target | Current declared flash | Proposed implementation |
| --- | --- | --- |
| Pico W, RP2040 | 2 MiB | Optional updater and an additional bootloader with a validated update/recovery strategy. |
| Pico 2 W, RP2350 Arm | 4 MiB | Native partition and A/B boot facilities, including trial boot and confirmation. |
| XIAO ESP32-C3 | 4 MiB | ESP-IDF application OTA slots and boot confirmation. |
| XIAO ESP32-C6 | 4 MiB | ESP-IDF application OTA slots and boot confirmation. |
| XIAO ESP32-S3 | 8 MiB | ESP-IDF application OTA slots and boot confirmation. |
| XIAO ESP32-S3 Sense running kaj-fi | Validate actual hardware and its product build | The same shared management contract, with kaj-fi artifacts and application health checks. |

The flash sizes above are the current LightVM build declarations. Validate
physical flash, the exact board variant and the installed layout before writing.
Matching the processor alone is insufficient to select a product or image.

The non-wireless Pico remains outside OTA scope and retains local installation.
Native host builds provide protocol and failure simulations, not physical
firmware updates. The sibling pattern compiler uses the shared descriptor as a
client; it is not another board firmware target. Earlier CircuitPython
experiments are outside this native firmware contract.

ESP-IDF supplies OTA machinery, but LightVM and kaj-fi currently use single
factory application partitions. Both need explicit new layouts and integration.
The 4 MiB ESP32 targets cannot fit two existing 2 MiB application reservations
plus system and content storage; choose slot sizes from measured image budgets.
RP2350 supplies boot support, rather than a complete hosted updater. RP2040
needs an additional loader; reserving a second image region alone does not
implement safe installation or rollback.

## Feature selection and flash layout

Introduce an explicit OTA build selection, provisionally `LIGHTVM_OTA`, and the
equivalent native selection in kaj-fi. Selecting it resolves the updater,
verification, boot confirmation, metadata and transport dependencies. Excluding
it removes the update service and its dedicated buffers and code. The common
build/feature descriptor remains available in both cases.

The initial hosted flow uses browser-to-device HTTP upload over the existing
management connection. That profile requires Wi-Fi and HTTP. Keep the update
engine independent of HTTP so another explicitly supported transport could be
added later; device-side GitHub downloads are not required for the first flow.

Define stable layouts per board and installation profile. Within an OTA layout,
changing MQTT or audio must not move the animation store or boot metadata.
Reserve sufficient firmware growth space, then allocate the remaining usable
flash to saved content. Emit exact regions, alignment, slot capacities and RAM
reservations with each artifact. Reject overlap or oversized images at build,
packaging and device admission.

An initial USB/UF2 installation is required to add a loader or change a partition
layout. The first OTA implementation updates application images only; it does
not rewrite bootloaders, partition tables or stored-object formats remotely.
Incompatible experimental state uses documented export, wipe and reprovision
steps, not migrations or compatibility readers.

Updating to a build without the OTA service requires an explicit choice in the
website and a valid local recovery path. Such an image must still satisfy the
installed loader's boot/confirmation contract. Removing the service does not
reclaim a second firmware slot remotely; changing to a layout with more content
storage requires local installation.

### Pico W capacity evidence

Fresh Release builds of source `115777c872c9fdcf06e7254fb3594cc92892197a`
used Pico SDK 2.3.1 and Arm GNU 13.2.Rel1. Both enabled Wi-Fi, HTTP, WebSocket and
power-cycle recovery, with the default 1024-pixel capacity and 512 KiB store.
The [measurement record](../validation/ota-picow-size-2026-10-30.json) includes
options, binary hashes, RAM reports and the arithmetic below.

| Current build | Flash payload | Automatic VM heap limit |
| --- | --- | --- |
| Stock, MQTT and audio subscriptions off | 712,748 bytes / 696.04 KiB | 78,496 bytes |
| MQTT and audio subscriptions on | 735,244 bytes / 718.01 KiB | 64,304 bytes |

These are binary flash payloads, not UF2 transfer file sizes. They do not include
an OTA implementation. The two current feature selections differ by 22,496 flash
bytes and 14,192 bytes of effective VM heap.

A candidate layout for the 2 MiB board is:

| Region | Allocation |
| --- | --- |
| Additional loader and OTA metadata, assumed allowance | 64 KiB |
| First firmware image | 768 KiB |
| Second or staged firmware image | 768 KiB |
| Existing power-cycle journal | 8 KiB |
| Animations, presets and configuration | 440 KiB |
| Total | 2,048 KiB |

Each image region leaves 73,684 bytes for stock firmware or 51,188 bytes for the
MQTT/audio build before adding updater code. The 64 KiB loader/metadata allowance
is an assumption requiring measurement. Preserve room for image headers,
slot-specific linking, verification and any scratch area needed by the chosen
loader; a design requiring additional space must revise this budget.

Keeping 512 KiB for content would reduce each image region to 732 KiB, leaving
36,820 or 14,324 bytes respectively. A 1 MiB store cannot coexist with two current
images. Moving from an existing store to the candidate layout requires clean
provisioning. Total content capacity also includes storage metadata and staging;
440 KiB is not a promise of 440 KiB of animation payload.

For RP2040, settle how the loader selects correctly linked images or performs
a resumable swap. Copying a staged image over the running image does not by
itself preserve the previous firmware for rollback. Demonstrate the complete
power-loss and trial-boot behavior before advertising OTA support.

## Reading the installed build and features

Extend the existing read-only `MANAGE {"op":"describe"}` operation over USB,
HTTP and WebSocket with a common firmware descriptor. Reuse LVM1 framing and
current management routes. This works without OTA and without a GitHub account
or internet connection on the device.

| Field | Meaning |
| --- | --- |
| `controller_id`, `boot_id` | Stable device identity and identity of this boot; retain their current meaning. |
| `product`, `board`, hardware variant, chip and flash capacity | Identify the application and exact installation target. |
| `version` | Product application version from its single authoritative declaration. |
| `firmware.build_id` | Exact build identity, including the resolved profile and build inputs; source SHA alone is insufficient. |
| `firmware.source_revision` | Full source commit used for the application. |
| `firmware.release_tag` | Published tag when applicable; `null` for an untagged development build. |
| `firmware.profile`, `firmware.features_hash` | Profile identity and digest of the canonical compiled feature/driver selection. |
| `firmware.layout_id`, `firmware.boot_contract` | Exact layout and compatible boot/update encoding identifiers. |
| `features` | Complete set of feature IDs actually compiled into the installed image. |
| `limits` | Effective application resource limits, separate from current free space. |
| `updates` | Available methods, strategy, image/chunk limits, active slot, boot state and recovery/rollback capabilities. |

Retain current application description fields used by clients. Generate shared
identity, feature lists and artifact metadata from the same resolved build
inputs. The existing root `version` remains authoritative; do not introduce a
second independently maintained version string.

Feature IDs come from a shared catalogue. Initial relevant IDs include
`usb_management`, `wifi`, `http_api`, `websocket_management`, `mqtt`,
`home_assistant`, `audio_subscriptions`, `light_output`, `audio_analysis`,
`audio_publisher`, `program_storage` and `ota`. Resolve dependencies explicitly;
for example Home Assistant requires MQTT. Product-specific drivers can extend
the catalogue without implying support on every product.

Compiled features are distinct from configuration and current availability:
`wifi` can be present while disconnected, and MQTT can be compiled in while
disabled in settings. Report those runtime states through normal status APIs.
Discovery's coarse capability mask is a routing hint; read the full descriptor
directly before displaying controls or admitting an update.

The following common fields illustrate a future OTA-enabled Pico W build;
placeholders and the candidate image capacity are not an existing release:

```json
{
  "product": "lightvm",
  "board": "pico_w",
  "controller_id": "pico-example",
  "boot_id": "example-boot-id",
  "version": "0.1.0",
  "firmware": {
    "build_id": "<build-inputs-and-profile-identity>",
    "source_revision": "<full-git-sha>",
    "release_tag": null,
    "profile": "standard-ota",
    "features_hash": "sha256:<resolved-features-digest>",
    "layout_id": "pico_w-ota-768k",
    "boot_contract": "<validated-rp2040-loader-contract>"
  },
  "features": [
    "usb_management", "wifi", "http_api", "websocket_management",
    "light_output", "program_storage", "ota"
  ],
  "updates": {
    "supported": true,
    "methods": ["http-push", "usb"],
    "strategy": "<validated-rp2040-update-strategy>",
    "max_image_bytes": 786432,
    "max_chunk_bytes": 4096,
    "active_slot": "A",
    "boot_state": "confirmed",
    "trial_rollback_supported": true
  }
}
```

The chunk limit is illustrative and must follow measured RAM and transport
limits. The feature list illustrates the common subset; a real response includes
all compiled features. A build without OTA reports `updates.supported: false`,
omits `ota` from its features and still reports its local installation methods.
The website must distinguish this from temporary network unavailability.

Unknown optional fields/features may be ignored. Missing required identity,
unknown required boot/update contracts, malformed metadata and unsupported
required features fail admission. Bound all strings, lists and response sizes.
An update can add features absent from the current build: validate candidate
dependencies against the hardware and boot contract, then validate retained
configuration/program requirements against the candidate's feature set.
Refresh the descriptor after reconnect or boot change; do not reuse capabilities
cached from another boot or from the board model alone.

LightVM stays at its current `0.1.0` application baseline until an explicitly
agreed milestone. Other products retain their own agreed release policy; equal
version numbers across products do not establish compatibility. No per-feature
release floors are introduced. Build IDs and encoding/layout identifiers supply
provenance and actual compatibility checks.
Updates between different builds of the same application version are valid
candidates and must not be hidden as "already installed".

## Release artifacts and hosted workflow

Each firmware project publishes immutable, tagged release artifacts containing
the exact build identity, resolved features, target hardware, boot requirements,
layout/storage contracts, image size and digest. Publish full local-installation
bundles separately from application-only OTA payloads. Include a signed manifest
binding the payload to those properties; account for verification in the build
budget. The site's approved catalogue imports complete published releases and
serves verified assets from the hosted origin. Never put private GitHub tokens
in the browser or device.

The website must perform this sequence:

1. Connect to the selected device and read its identity, build, features and
   update capabilities. A device without OTA gets a local installation route.
2. List compatible published releases and profiles for that product and board.
   Exclude drafts; expose prereleases only through an explicit selection.
   Show the installed build and recommend compatible releases independently of
   GitHub's repository-wide "latest" label.
3. Compare image/slot capacity, flash layout, boot contract, storage formats,
   configured peripherals and saved-program requirements. Show features gained
   or lost, content implications and the remaining management/recovery path.
4. Let the user choose an upgrade, downgrade or profile change and start it with
   one explicit installation action. Layout or loader changes route to local
   installation; do not attempt a remote repartition.
5. Download and verify the exact candidate, acquire update ownership and stream
   it into the permitted inactive/staging region. Display acknowledged progress.
6. Ask the device to verify the complete image and mark it pending only after
   successful admission and verification. Commit the update and restart.
7. Reconnect to the same `controller_id`, expect a new `boot_id`, and compare the
   reported build/features/layout with the chosen artifact. Show success only
   after the trial image has been confirmed; report fallback or recovery clearly.

Treat deliberate downgrades as explicit selections. Validate storage and program
compatibility in both directions, including whether the fallback firmware can
still read state. Trial firmware must not irreversibly rewrite shared persisted
state before confirmation. Preserve saved content only when compatible; otherwise
use local clean installation and recompile/reprovision from external source.
Boot selection must support an explicit older compatible release. Map native
boot version fields to build/selection needs without bumping application versions
for development commits. Report hardware anti-rollback restrictions when present;
do not enable irreversible OTP/eFuse version floors as a side effect of this PoC.

## Update transaction and device behavior

Define a bounded firmware transaction separate from animation/program uploads.
Provisional operations are `firmware_begin`, `firmware_write`,
`firmware_finish`, `firmware_commit`, `firmware_cancel` and `firmware_status`.
Their final encodings must fit existing management transports.

- Begin validates the manifest, authorization, device/build/boot expectations
  and available target region before erasing it. It returns an update ID, accepted
  chunk limit and initial offset. Permit only one firmware writer per device.
- Write carries the update ID, offset and a bounded binary payload. Identical
  chunk retries are safe; conflicting retransmissions and writes outside the
  admitted image/region are rejected. Do not accept arbitrary flash addresses.
- Finish checks exact length, full image digest, authenticity, target/layout
  and native image validity. An incomplete or invalid image remains unbootable.
- Commit switches only to a verified image using the platform's durable boot
  metadata. A lost reply does not authorize another commit: query by update ID.
- Cancel releases an uncommitted update. Status reports receiving, verifying,
  ready, pending boot, confirmed, rolled back, cancelled or failed, with useful
  reasons and the last acknowledged offset. Persist enough state to reconcile
  a committed update across reboot. Transfer resume is advertised only if tested;
  otherwise safely discard incomplete staging and restart the transfer.

The application marks a trial boot successful after bounded local health checks.
Confirmation must not depend on the browser remaining open, GitHub availability,
an MQTT broker or an audio publisher. Failed trial boots must revert to a known
working image for the normal hosted-update path. USB/BOOTSEL remains the recovery
route when ordinary application management is unavailable.

Use bounded buffers and stream to flash; never hold an entire firmware image in
RAM. Serialize flash operations with storage writers and coordinate both Pico
cores and XIP access through supported SDK mechanisms. Enter an explicit
maintenance state; outputs may pause and kaj-fi capture must report any resulting
gap/new stream. Do not promise uninterrupted animation or audio during flashing.

The device must enforce update authorization and authenticity. Current management
session IDs and open CORS access are not authentication. Settle a scoped pairing
or equivalent authorization mechanism and signed-artifact verification before
enabling the hosted OTA path. A checksum detects corruption, not the authority
to install code. Keep credentials and private signing material out of release
assets, browser bundles and public descriptor responses.

Serve the website over HTTPS and exercise actual browser local-network permission,
CORS and WebSocket behavior. Browser permission alone does not authorize a
device-side firmware write. Keep firmware verification and flash admission on
the device even when the browser has already checked the candidate.

## Implementation sequence and acceptance

1. Generate the common build/feature descriptor in LightVM and kaj-fi, including
   OTA-disabled builds. Check it against release metadata and actual linked
   components, and teach the hosted workspace to select controls from it.
2. Finalize per-board OTA layouts and recovery contracts. Measure loader,
   updater, verifier and RAM costs. Prove the optional Pico W layout with current
   stock and MQTT/audio variants; reject a selection that exceeds a slot or RAM.
3. Implement ESP-IDF and RP2350 adapters using their native boot facilities.
   Implement and validate the separate RP2040 loader before offering Pico W OTA.
   Keep product health checks and the shared transaction machinery separate.
4. Add release packaging, catalogue admission, the hosted installation flow and
   local bootstrap/recovery instructions. No tags or releases are created merely
   by implementing this proposal.

Acceptance requires:

- Correct build/profile/features over USB and HTTP/WebSocket where included,
  with OTA absent/present, MQTT/audio off/on, and runtime services disconnected.
- Device rejection of wrong product, board, flash geometry, boot/layout contract,
  required features, oversized images, malformed manifests and invalid signatures.
- Matching descriptor/manifest identities for two builds sharing version `0.1.0`;
  successful identification of a changed profile at the same application version.
- Real hosted-browser upgrade and intentional downgrade on each advertised target,
  including kaj-fi Sense and optional Pico W. Verify saved content where compatible.
- Power cuts during transfer, verification, boot-metadata commit and first boot;
  trial crash/hang; transfer corruption; duplicate/reordered chunks; lost replies;
  browser closure; competing storage writes and network loss. Show recovery and
  fallback according to the advertised contract, including RP2040-specific paths.
- No writes outside admitted regions, including loader and animation storage;
  measured bounded RAM; builds with excluded services remove their dedicated cost.
- A clean local bootstrap from today's non-OTA layout, and local recovery after
  a failed application. Publish clear export/wipe/reprovision steps for layout
  changes instead of introducing experimental-state migrations.
- Explicit feature removal, including loss of OTA, with a usable recovery route;
  failed preflight/authorization leaves the current application and saved content
  unchanged.

## Platform references

- [ESP-IDF 5.5.3 application OTA and rollback](https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32s3/api-reference/system/ota.html).
- [Raspberry Pi RP2350 OTA example](https://github.com/raspberrypi/pico-examples/tree/master/pico_w/wifi/ota_update).
- [Pico SDK boot ROM APIs](https://www.raspberrypi.com/documentation/pico-sdk/runtime.html#pico_bootrom).
- [RP2040 boot process](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf), section 2.8.

Pin the exact upstream revisions used by an implementation; reference examples
on a moving branch do not become build dependencies implicitly.
