# Raspberry Pi Pico 2 W

Pico 2 W uses the existing Pico port with `PICO_BOARD=pico2_w` and reports board
identity `pico2_w`. Its target is RP2350 Arm Cortex-M33 (`rp2350-arm-s`), with
520 KiB SRAM and 4 MiB flash. USB, Wi-Fi, HTTP, WebSocket, MQTT, boot-sequence
recovery and the current PIO/DMA drivers are available as build options.
Installation, flash readback and USB startup were verified on an RP2350 A2 Pico
2 W on 2026-10-20. LED timing, station networking and external transport
acceptance remain pending.

## Build

Use Arm GNU Toolchain 13.2.Rel1, CMake and Pico SDK 2.3.1. Put the compiler's `bin`
directory on `PATH`. The SDK pin is
`079c6f39023649b154152db30f1d781e884879bc`; its selected dependencies are:

| Dependency | Commit |
|---|---|
| TinyUSB | `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` |
| CYW43 driver | `055d64274b014dd7b1c2fc94d26e8a18face7124` |
| lwIP | `77dcd25a72509eb83f72b033d219b1d40cd8eb95` |
| picotool 2.3.1 (build host only) | `2041936441b48a3cc53ae3da9e805229fe8f4e18` |

From the repository root:

```sh
git clone --branch 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git build/deps/pico-sdk
git -C build/deps/pico-sdk submodule update --init lib/tinyusb lib/cyw43-driver lib/lwip

cmake -S . -B build/pico2-w -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$PWD/build/deps/pico-sdk" -DCMAKE_BUILD_TYPE=Release \
  -DLIGHTVM_WIFI=ON -DLIGHTVM_HTTP=ON -DLIGHTVM_WEBSOCKET=ON \
  -DLIGHTVM_MQTT=ON -DLIGHTVM_BOOT_SEQUENCE=ON
cmake --build build/pico2-w --parallel
```

The SDK finds picotool 2.3.1 or downloads and builds that version to generate UF2
files. An installed copy can be selected with `-Dpicotool_DIR=PATH_TO_CMAKE_CONFIG`.
Bluetooth and TLS are not used, so their submodules are not required.

For a USB-only image, use another fresh directory:

```sh
cmake -S . -B build/pico2-w-usb -DLIGHTVM_PLATFORM=pico -DPICO_BOARD=pico2_w \
  -DPICO_SDK_PATH="$PWD/build/deps/pico-sdk" -DCMAKE_BUILD_TYPE=Release
cmake --build build/pico2-w-usb --parallel
```

All feature switches default OFF. HTTP and MQTT require Wi-Fi; WebSocket requires
HTTP. Use a different build directory for each board/profile and start fresh
after an SDK upgrade. Changing a cached board or choosing RISC-V fails at
configuration. `LIGHTVM_BOARD_CONFIG` can still select an explicit custom engine
resource header.

The build produces `lightvm.uf2`, `lightvm.elf`, `storage-layout.json`,
`ram-report.json` and the licence bundle. Use the same `pico2_w` identity for
`tools/smoke_controller.py --board pico2_w` when testing that firmware on a device.

## Resources and storage

Pico 2 W automatically budgets its VM heap from the linked usable RAM after
stack, pixel-driver and native operating reserves. Its per-instance instruction
cache is 1024 bytes. The module and
physical-pixel ceilings remain 32 KiB and 1024. These budgets, connection limits,
worker stack and optional refresh cap are tunable through the
[multicore resource options](multicore-rendering.md).

Both CPU cores render. Core 0 owns communication, storage and hardware; core 1
runs bounded VM or pixel jobs. Both output drivers use PIO0/PIO1. The SDK places
the radio on PIO2, leaving eight output state machines. Output drivers do not
use PIO2. Rendering can overlap PIO/DMA transmission, with no default FPS cap.

The linker reserves a 16 KiB core-0 stack and an 8 KiB core-1 stack, excluding
both from the contiguous native heap. Explicitly time-critical SDK code,
including flash programming, stays in SRAM. Flash operations coordinate both
cores through the SDK lockout protocol. RP2350 uses its DCP for basic double
arithmetic while retaining compiler conversions and libm; the M33 hardware FPU
already handles single precision. Compare timing on hardware, not host clocks.
The [multicore validation report](multicore-validation.md) records measured
core-count and DCP comparisons, including the firmware installed on the test board.

The RAM gate reads the ELF's memory geometry, static allocation boundaries,
stack reservation and compiled VM/pixel limits. It includes RAM code and retains
the configured operating headroom (16 KiB by default) after VM and output-driver
reservations. Every
Pico build emits the report and fails if over budget. It is a reservation check,
not a measurement of peak native allocation or stack use.

With the default 512 KiB store, flash offsets are:

| Region | Offsets |
|---|---|
| Application | below `0x380000`, or below `0x37e000` with boot sequence enabled |
| Optional boot journal | `0x37e000..0x37ffff` |
| Configuration, programs and presets | `0x380000..0x3fffff` |

The linker rejects overlap. Changing `LIGHTVM_STORAGE_BYTES` moves the store and
journal; the authoritative addresses are in `storage-layout.json`, using XIP
addresses with base `0x10000000`. Do not copy persisted flash from a Pico W: its
physical storage region is at different addresses.

## Installation and clean provisioning

1. Confirm the physical board is **Pico 2 W**. Disconnect USB, hold **BOOTSEL**,
   and reconnect. Its mass-storage drive is **RP2350**.
2. Copy the board-matched `lightvm.uf2` to that drive. The board restarts into
   LightVM. A Pico/Pico W UF2 uses a different chip family.
3. Connect using the editor's Controllers page or the serial CLI. Check discovery
   reports `pico2_w` before configuring outputs. No LEDs are assigned by default.
4. Apply the actual wiring/output configuration and save programs and presets.
   Network firmware without a valid saved network configuration starts its
   [setup AP](networking.md#initial-setup-and-standalone-ap-mode). Join it and open
   `http://192.168.4.1` to save Wi-Fi credentials directly, then rejoin your normal
   network to use the editor. The form requires no JavaScript or internet access.

The editor's Firmware page includes Pico 2 W as a **UF2 download**. Direct browser
flashing currently supports only RP2040 Pico/Pico W. The packager validates the
RP2350 Arm Secure family and picotool's ignored E10 workaround block separately
from application blocks, and rejects writes into the linked storage boundary.

If the new board has previously held another application, or experimental state
is incompatible, perform a clean install and provision configuration, programs,
presets and network settings again. With a USB-enabled picotool 2.3.1 installation
and the intended board in BOOTSEL, `picotool erase -a` erases its flash, including
saved data. Then copy the UF2 to RP2350 again. With multiple boards attached, use
picotool's `--ser SERIAL` selector to identify the intended board. No migration
from earlier experiments is provided.

## Hardware acceptance checklist

Record the source revision, SDK/compiler pins, feature flags, board USB serial,
actual wiring and results. Installation acceptance on 2026-10-20 used board serial
`992D5E9CA99B00E3`, the uncommitted changes on base `dde7e46`, the pins above and
Wi-Fi/HTTP/WebSocket/MQTT/boot-sequence enabled. The validated UF2 SHA256 is
`606cc5d1ebbb97ed4d67a4c963ca6ccd65f35c52c992aa68be167fa0e5d5137d`.

The complete 4 MiB flash was backed up before installation. `picotool load -v`
verified the written image; a separate readback confirmed the 512 KiB store plus
8 KiB journal region was unchanged before first boot. LightVM 1.1.0 then appeared
at `/dev/cu.usbmodem11401`, reported `pico2_w`, an available 512 KiB store and no
boot error. Its initial network status was `ap_active`, SSID `lightvm-4cd58a`,
IP `192.168.4.1`, with no radio error. No outputs were configured. This confirms
radio/AP initialization; association, DHCP and HTTP over that AP were not tested.
Evidence and the pre-install backup are in
`build/pico2-w-support/hardware-992D5E9CA99B00E3/`.

The board was subsequently updated with the plain HTML Wi-Fi setup form. Its UF2
SHA256 is `d830b3a34db2117449876d86ea49dc5f7b27bdbafbf34b0a93d3a713d7033af3`.
Flash verification passed and the store/journal readback was unchanged. The board
returned to `ap_active` at `192.168.4.1`, and the setup form was confirmed in a
browser connected to its AP. The form's native POST, credential persistence,
validation and recovery behavior were tested against the host engine, including
a Chrome submission with JavaScript disabled. Actual station provisioning remains
to be tested with the intended Wi-Fi credentials. Update evidence is in
`build/wifi-setup/`.

Run the remaining checks when a broader hardware test is requested:

- [x] Install with flash readback verification and boot successfully over USB.
- [x] USB discovery reports `pico2_w`, the 32 KiB module / 64 KiB heap limits,
  1024-pixel ceiling and available 512 KiB store, with no boot error.
- [ ] Run the serial animation smoke check against the host reference.
- [ ] Configure a WS2812 output, upload a canonical animation and verify order,
  brightness and steady output under command traffic.
- [ ] Provision Wi-Fi; verify station connection, DHCP hostname, mDNS discovery,
  reconnect and setup/AP recovery using the actual network.
- [ ] Exercise HTTP and WebSocket concurrently with USB, including discovery
  and configuration/program operations.
- [ ] Verify MQTT hostname resolution, Home Assistant state, commands and broker
  reconnect while outputs keep rendering.
- [ ] Save configuration and a preset, reboot and verify startup/persistence;
  repeat the boot-sequence/power-recovery checks with that option enabled.
- [ ] If wired, exercise multiple output streams, I2C TCA9555 inputs and APA102
  keypad indicators alongside networking.
- [ ] With explicit probe-test authorisation, build with
  `-DLIGHTVM_OUTPUT_PROBE=ON`, run `output_probe.uf2`, capture the
  [production-driver probe](../tests/pico_output/README.md) results and restore
  the normal image. Check external strip timing/electrical behaviour separately.

The [implementation plan](design/feature-proposal-pico2-w.md) records scope and
the software verification results. Historical Pico W measurements in other guides
remain Pico W evidence.
