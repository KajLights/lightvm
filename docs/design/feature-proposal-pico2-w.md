# Pico 2 W board support

Status: Software implemented and verified as of 2026-10-20.
Installation and USB startup were subsequently verified on hardware; broader
output/network acceptance remains pending.
See the [board guide](../pico2-w.md) for current build and installation instructions.

## Goal and scope

Add Raspberry Pi Pico 2 W as `PICO_BOARD=pico2_w` in the existing Pico port,
with its own `pico2_w` board identity, reproducible firmware builds and a documented
installation path. Support USB, Wi-Fi, HTTP, WebSocket, MQTT and the optional boot
sequence using the existing LightVM contracts. Keep Pico and Pico W working on
the same updated SDK.

The first target is RP2350 Arm Cortex-M33 (`rp2350-arm-s`). RISC-V, multicore
execution, Bluetooth, OTA, larger VM limits and additional output capacity are
outside this change. Keep board support separate from the proposed
[instance build profiles](feature-proposal-build-profiles.md).

Raspberry Pi documents the Pico 2 W as having 520 KiB SRAM, 4 MiB flash and a
150 MHz RP2350, with the Pico W header layout. The SDK supplies a `pico2_w` board
definition and its wireless pin assignments. Use those definitions rather than
copying a Pico W SDK header or redefining the wireless wiring.
Sources: [board documentation](https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html),
[SDK board definition](https://github.com/raspberrypi/pico-sdk/blob/2.3.1/src/boards/include/boards/pico2_w.h).

## Findings before implementation

- The root `CMakeLists.txt` chose a special resource header only for `pico_w`,
  before importing the SDK. An unrecognised board otherwise inherited the `pico`
  identity. `LIGHTVM_BOARD_CONFIG` is cached, so reusing a build directory can
  also retain the wrong board header.
- `platforms/pico/CMakeLists.txt` accepted Wi-Fi only for `pico_w`. Both firmware
  workflows pinned SDK 1.5.1, which predates Pico 2 W support.
- The Pico W header reduces the VM heap to 64 KiB for network builds. The network
  source asserts that limit and a maximum of 1024 physical pixels.
  `tools/check_pico_ram.py` hardcodes 264 KiB SRAM and reserves buffers for eight
  output state machines. It cannot report RP2350 capacity correctly.
- Flash storage and the boot journal already use SDK flash geometry through
  `storage_layout.S` and the linker assertions. `tools/write_storage_layout.py`
  exports the resulting boundaries for installers; these must be checked against
  a real Pico 2 W ELF.
- WS2812 and APA102 drivers currently allocate from `pio0` and `pio1`; networking
  prefers PIO1. Do not silently claim all twelve RP2350 state machines are
  available to outputs.
- Firmware packaging, UF2 decoding and the editor's board list accept only Pico
  and Pico W / RP2040. The vendored PICOBOOT library has an RP2350 target, but
  that alone does not validate the application's UF2 parsing or flashing flow.
- Third-party notices and retained SDK/network dependency notices describe
  1.5.1 and the RP2040-specific CYW43 grant. They need to match the new pin.

## Implementation plan

### 1. Establish one current SDK baseline

- Pin Pico SDK 2.3.1 for all Pico builds, including both GitHub workflows and the
  build instructions. It is the current published stable release at planning
  time. Record its commit and selected TinyUSB, CYW43 and lwIP submodule commits.
  [Upstream release](https://github.com/raspberrypi/pico-sdk/releases/tag/2.3.1).
- Check Arm GNU 13.2.Rel1 against both RP2040 and RP2350. Retain that compiler if
  it passes; otherwise choose one explicit compiler version for both targets and
  update its notices. Pin any SDK host tool needed to produce RP2350 UF2 files.
- Build from fresh directories. Update SDK APIs or warnings in the shared port
  directly; do not retain a parallel 1.5.1 implementation.
- Review the dependencies actually linked by USB and network builds. Update the
  retained licences, per-file notices, source links and copied artifact bundle,
  including the CYW43 grant that covers RP2350.

Acceptance: clean USB firmware builds for Pico, Pico W and Pico 2 W, with UF2
output and an accurate dependency pin in the documentation.

### 2. Add the board configuration and identity

- Add `platforms/pico/boards/pico2_w.h` and select it automatically for
  `PICO_BOARD=pico2_w`. Report `pico2_w` through platform information, controller
  discovery and the existing client tools.
- Preserve an explicit `LIGHTVM_BOARD_CONFIG` override. Make default board
  resolution unambiguous; reject an unsupported selection or a conflicting
  cached target rather than silently reporting `pico`.
- Support Pico 2 W in the Wi-Fi guard, while continuing to reject Wi-Fi for the
  non-wireless Pico. Select and validate the Arm target explicitly for this
  implementation.
- Initially retain the current networking resource ceilings: 64 KiB VM heap,
  1024 physical pixels, current connection limits and existing output drivers.
  The extra SRAM provides headroom; raising user-visible limits needs separate
  capacity measurements.
- Retain the exposed Pico header GPIO list and let the SDK own internal wireless
  pins. Check USB identity and the existing I2C/keypad path when cross-building.

Acceptance: configure/build a Pico 2 W with and without networking; verify its
board identity is distinct, custom resource headers still work, and unsupported
board/transport combinations fail with a useful error.

### 3. Validate RAM, flash and output assumptions

- Make the RAM report use the selected target's SRAM geometry and actual
  configured reservations, instead of assuming RP2040. Keep heap/stack and
  output-buffer accounting consistent with the compiled firmware; reject an
  over-budget build and retain the report beside the ELF.
- Audit the new SDK's section accounting so RAM-resident code and data are
  included. Keep the output-driver reservation aligned with the two PIO blocks
  the current drivers use and the networking driver's own resources.
- Verify 4 MiB flash, the configured store at the end of flash, and the optional
  8 KiB journal immediately before the store. With the default 512 KiB store,
  expect store offset `0x380000` and journal offset `0x37e000`.
- Exercise the linker overlap guards with and without the journal; check the
  exported `storage-layout.json` against the ELF and the packaged image.
- Audit clock-derived PIO timing, DMA setup/error flags, flash operations and
  reset handling on RP2350. Build the existing output probe for both chip
  generations; reserve physical timing claims for hardware measurements.

Acceptance: board-correct RAM/storage reports, tested rejection of excessive
reservations/overlap, and production/probe firmware cross-builds. No increased
output or VM capacity is advertised.

### 4. Include Pico 2 W in CI and firmware distribution

- Extend the firmware matrix with Pico 2 W USB, Wi-Fi-only, HTTP, WebSocket plus
  MQTT, and MQTT-only builds, with boot sequence OFF and ON as for Pico W.
- Run Pico and Pico W regressions on the same SDK pin. Publish UF2, ELF, size,
  storage layout, RAM report where applicable, and notices with clear board names.
- Add the network-capable Pico 2 W image to the Pages firmware bundle and
  packaging CLI. Preserve board/chip identity in the manifest.
- Validate RP2350 UF2 using the actual SDK output: family ID, loadable flash
  ranges, ordering, block flags and any SDK-generated metadata. Do not merely
  allow the RP2350 family in the current contiguous-RP2040 parser. Reject a
  chip/image mismatch or writes into the reserved store/journal.
- Offer the Pico 2 W UF2 download in the editor with BOOTSEL instructions for its
  `RP2350` drive. Add direct WebUSB installation only after the RP2350 command,
  erase, write, readback and reboot flow is validated; otherwise clearly limit
  that button to the existing RP2040 boards. A working board-matched UF2 download
  is required for this goal; a new unverified direct installer is not.

Acceptance: CI describes all supported transport combinations; packaging accepts
the real image, rejects wrong-chip/malformed/overlapping images, and the editor
provides a working, accurately labelled Pico 2 W installation path.

### 5. Document and run acceptance

- Update the README, networking, storage, boot sequence, MQTT and editor guides
  where board lists or build/installation instructions would otherwise be
  misleading. Keep historical Pico W measurements labelled as Pico W evidence.
- Provide exact fresh-build commands for USB and network Pico 2 W firmware,
  expected board identity and artifact locations, and clean provisioning steps.
  If experimental saved state becomes incompatible, document wiping and
  reprovisioning; do not add a migration layer for earlier experiments.
- Run relevant host contract/integration tests, firmware-packaging tests, UF2
  tests, editor tests and the production editor build. Add focused tests for the
  new board selection, capacity calculation and chip/image boundary checks.
- Detect an accessible device without changing its state. Record USB identity
  and whether it is application firmware or BOOTSEL before choosing any serial
  port or flash target.
- When hardware testing is explicitly requested and the board is accessible,
  install the board-matched image and record: `pico2_w` discovery; USB command
  smoke test; one configured WS2812 output; Wi-Fi provisioning, station and AP
  recovery; HTTP/WebSocket control; MQTT/DNS and reconnect; saved preset and
  configuration after reboot; and boot-sequence recovery when enabled. Exercise
  multiple outputs/network traffic and the keypad path if that hardware is wired.
  Run the output probe only as part of an authorised hardware test and restore
  the normal firmware afterward.
- Keep a hardware acceptance checklist with the exact firmware revision, build
  flags and observed results. Mark unavailable checks pending; cross-build
  success is not evidence of physical output timing or wireless reliability.

## Completion criteria

The software goal is complete when Pico 2 W has a distinct, automatically chosen
board profile; USB and network builds pass on the current shared SDK pin; RAM and
flash boundaries reflect RP2350/Pico 2 W; CI and editor UF2 packaging include it;
relevant tests pass; and build, installation and acceptance instructions are
current. Report hardware results separately, including checks still awaiting an
accessible board or an explicit hardware-test request.


## Software verification record (2026-10-20)

- SDK 2.3.1 and Arm GNU 13.2.Rel1 cross-build Pico USB and all five transport
  combinations for Pico W and Pico 2 W, each with boot sequence OFF/ON: 22
  configurations. Each image is checked against its compiled SRAM/stack budget,
  physical flash size, store/journal boundaries and expected UF2 family.
- The output probe builds for Pico W and Pico 2 W alongside the combined
  Wi-Fi/HTTP/WebSocket/MQTT/boot-sequence firmware. It has not been flashed.
- Real custom resource-header firmware builds with different heap/pixel limits.
  RISC-V, unknown boards, Wi-Fi on Pico and MQTT without Wi-Fi are rejected.
  A real oversized-heap build fails the post-link RAM gate; firmware/store
  overlap fails linking with the boot journal both disabled and enabled.
- Host CTest: 15 contracts pass. Python integration: 273 tests pass, including
  HTTP, WebSocket, MQTT, DNS adapter tests and the new Pico packaging/resource
  tests. The final focused Pico suite passes 8 tests, including the added
  contiguous-heap/stack-boundary check. Browser protocol: 19 tests pass. The firmware-selection UI has tests for
  the Pico 2 W download/drive instructions and switching back to the Pico W
  installer; the full editor suite passes 133 tests (8 optional tests skipped). The TypeScript/Vite production editor build passes.
- The compiled `PlatformInfo` in the Pico, Pico W and Pico 2 W flash images is
  checked directly, including the explicit custom-header identity.
- The packager successfully consumes the real Pico, Pico W and Pico 2 W UF2s
  together and emits separate board/chip entries and checksums. The exact Pages
  Wi-Fi/HTTP/WebSocket/boot-sequence profiles (MQTT OFF) also build for both
  wireless boards and package together with the Pico USB image.
- Final implementation uses the SDK's PIO allocator: the radio normally takes
  PIO1 on Pico W and PIO2 on Pico 2 W. Output drivers stay on PIO0/PIO1, so RAM
  reservations cover seven possible output buffers for RP2040 networking and
  eight for RP2350 networking. The obsolete preferred-PIO macro is removed.
- The linker gives the single core a real 16 KiB stack and the remaining SRAM
  to the heap. Compiler math code/constants stay in XIP flash; SDK flash write
  and erase routines are confirmed to reside in SRAM in every validated ELF.
- At initial software verification, no Raspberry Pi USB device, Pico serial port
  or RP2350 BOOTSEL drive was visible. Installation was subsequently verified
  as recorded below; broader hardware acceptance remains in the board guide.

Local build evidence is in `build/pico2-w-support/`, including
`matrix-results.json`, `rejection-results.json`, `identity-results.json`,
`device-check.json`, per-build logs and the packaged
`editor-firmware/` bundle. Initial local validation used base revision `dde7e46`.
The updated firmware matrix and editor packaging run on pushes through
[GitHub Actions](https://github.com/KajLights/lightvm/actions).

## Installation acceptance (2026-10-20)

Following the user's installation request, Pico 2 W serial `992D5E9CA99B00E3`
was detected as RP2350 A2 in BOOTSEL. Its full 4 MiB flash was backed up, the
combined-feature UF2 was installed with successful readback verification, and
the reserved store/journal region was independently checked unchanged before
first boot. LightVM 1.1.0 booted on `/dev/cu.usbmodem11401`, reported board
`pico2_w`, available storage, correct resource limits and no boot error. The
radio reported its initial setup AP active at `192.168.4.1` with SSID
`lightvm-4cd58a`. No outputs or saved network settings were configured.

The [board guide](../pico2-w.md) records the image hash and remaining hardware
checks. Installation evidence is in
`build/pico2-w-support/hardware-992D5E9CA99B00E3/`.

## Direct Wi-Fi provisioning (2026-10-20)

Wi-Fi/HTTP firmware now serves a plain HTML form at `/`, using a normal POST to
`/setup` with no JavaScript or external assets. The Pico 2 W was updated with
verified flash readback, retained its store/journal region, and returned to its
initial setup AP. The user confirmed that `http://192.168.4.1` displays the form.

The host suite passes 284 tests with one optional skip, including ten setup-form
tests; all 15 native contracts pass. Chrome submitted the form successfully with
JavaScript disabled. Pico W and Pico 2 W combined firmware and ESP32-S3 HTTP
firmware build successfully. Evidence is in `build/wifi-setup/`.
