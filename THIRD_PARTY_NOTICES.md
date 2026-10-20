# Third-party notices

LightVM's original code and documentation are licensed under the MIT License in
[LICENSE](LICENSE), copyright 2026 Kaj Wik Siebert. Dependencies retain the
licenses and copyright notices listed below. The review covers the current
native receiver, RP2040/RP2350 Pico and XIAO ESP32 builds, and pinned Python tools.

| Component | Version | License | Use | Preserved notice |
|---|---|---|---|---|
| Pico SDK | 2.3.1 | BSD-3-Clause; selected files MIT | Pico firmware | [BSD license](licenses/pico-sdk-2.3.1.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| TinyUSB | SDK submodule `86ad6e56c1700e85f1c5678607a762cfe3aa2f47` | MIT | USB device driver | [License](licenses/tinyusb.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| pico⚡flash PICOBOOT package | commit `230759cf32ab245decf1194f5b37b7d3c0088aed` | MIT | Browser WebUSB Pico flashing | [License](licenses/picoflash.txt), copyright 2025–2026 Piers Finlayson |
| Pico examples WS2812 PIO | 1545dfd9a186 | BSD-3-Clause | Adapted PIO program | [License](licenses/pico-examples.txt), copyright notice in [source](platforms/pico/ws2812.pio) |
| pySerial | 3.5 | BSD-3-Clause | Desktop serial client | [License](licenses/pyserial-3.5.txt) |
| Newlib | 4.3.0, Arm GNU Toolchain 13.2.Rel1 | Per-source permissive licenses for the linked ARM C/math routines | Firmware C and math libraries | [Upstream license collection](licenses/newlib-4.3.0.txt) |
| GCC runtime | Arm GNU Toolchain 13.2.Rel1 | GPL-3.0-or-later WITH GCC-exception-3.1 | Firmware compiler support routines | [GPL](licenses/gcc-gpl-3.0.txt), [runtime exception](licenses/gcc-runtime-exception.txt) |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) | Apache-2.0; selected MIT/BSD components below | C3/S3/C6 SDK, USB Serial/JTAG and RMT | [Apache license](licenses/esp-idf-5.5.3.txt), [S3/C6 source notices](licenses/esp-idf-additional-notices.txt), [C3 source notices](licenses/esp-idf-c3-additional-notices.txt) |
| FreeRTOS / Xtensa support | IDF-selected kernel 10.5.1 and SDK ports | MIT | ESP32 scheduling and native context support | [Preserved copyright/license headers](licenses/esp-idf-additional-notices.txt) |
| TLSF | IDF submodule `2867f6883a12920b1969ff9624c0ab0e4185c2ce` | BSD-3-Clause | Native allocator and ROM allocator patches | [BSD conditions](licenses/tlsf-esp-idf-5.5.3.txt), [copyright headers](licenses/esp-idf-additional-notices.txt) |
| Mbed TLS / micro-ecc | IDF-selected submodules | Apache-2.0 option / BSD-2-Clause | SDK/bootloader security components (unused features are discarded) | [Mbed TLS license](licenses/mbedtls-esp-idf-5.5.3.txt), [selected headers and micro-ecc license](licenses/esp-idf-additional-notices.txt) |
| Espressif Newlib | 4.3.0, toolchain `esp-14.2.0_20251107` | Per-source permissive licenses | C3/S3/C6 C/math routines | [License collection](licenses/newlib-esp-14.2.0_20251107.txt) |
| Espressif GCC runtime | GCC 14.2.0, toolchain `esp-14.2.0_20251107` | GPL-3.0-or-later WITH GCC-exception-3.1 | Xtensa/RISC-V compiler support routines | [GPL](licenses/gcc-gpl-3.0.txt), [runtime exception](licenses/gcc-runtime-exception.txt) |

The linked dependencies permit LightVM's own code to remain MIT-licensed when
these upstream notices and conditions are preserved. The GCC runtime exception
permits distributing the compiled combination under the independent modules'
licenses for an eligible compilation process; this build uses GCC directly.
The Newlib license collection also covers other targets and optional components
that are not linked by this `arm-none-eabi` firmware.

## Pico double-precision implementation

LightVM selects `pico_set_double_implementation(lightvm compiler)` on both
RP2040 and RP2350. Double-precision operations use compiler/Newlib routines,
not SDK ROM double shims. Compiler math code and constant tables remain in XIP
flash to retain the Pico W network builds' RAM budget. SDK routines explicitly
marked as time-critical, including flash programming, remain in SRAM.

## Python animation frontend and example provenance

The compiler and native interpreter are LightVM implementations under the
repository MIT license. Color, Pattern, Sweep, and Rainbow in `examples/animations/`
are ports of Kaj's supplied picolights algorithms, with the accepted lifecycle and
timing changes documented in
[the animation API](docs/animation-profile.md). The source reference is Kaj's
picolights repository, commit `f6480b4845b2ba3af4fbf0ffb1f398d362dda772`.
Stars and Rainbow Sprite are LightVM examples of sprite drawing and motion under the same MIT license.
The six files in `examples/animations/` are the only maintained sample implementations;
the editor, tools and tests all use them directly.

The GPL-marked `picolights/easing.py` is not included. No easing implementation
is copied. Rainbow's colourwheel arithmetic follows the published
[CircuitPython rainbowio definition](https://github.com/adafruit/circuitpython/blob/main/shared-module/rainbowio/__init__.c);
LightVM implements the arithmetic locally rather than linking that module.

Python AST parsing uses the standard library. The compiler emits custom bytecode.
Native RGB/HSV conversion retains LightVM's MIT implementation. SDK and build
dependency licenses remain as listed above.

## Existing dependency modifications

The WS2812 PIO program is adapted from Pico examples commit 1545dfd9a1862961904856d46a4e05f35454d4be.
Only the single-strip PIO version 0 program is used, shared by RP2040 and RP2350. The Raspberry Pi copyright and BSD license identifier
are retained. Source: https://github.com/raspberrypi/pico-examples/blob/1545dfd9a1862961904856d46a4e05f35454d4be/pio/ws2812/ws2812.pio


`platforms/esp32/main/ws2812_encoder.c` adapts the composite byte/copy encoder
from ESP-IDF v5.5.3's `examples/peripherals/rmt/led_strip/main/led_strip_encoder.c`.
Its Espressif copyright and Apache-2.0 identifier are retained. LightVM changes
the timings to integer 800 kHz symbols and a 300 us reset at 20 MHz, and uses
the encoder through the shared synchronous native output contract.

The ESP32 project compiles only its required SDK component closure. Radio stacks and vendor radio libraries are included only in optional networking
profiles; PSRAM remains excluded. Preserve the
selected SDK copyright headers and permissive license texts supplied here with
the application and bootloader. Additional SDK features/dependencies require
their own notice review when introduced; these notices do not cover every
optional component in an ESP-IDF source checkout.

The native Pico SDK, USB and PIO integration lives in `platforms/pico/`. The
shared engine has no Pico SDK dependency. Future platform ports must preserve
the notices for their selected native SDKs and linked services.


## Optional Wi-Fi and HTTP dependencies

Pico W and Pico 2 W networking use the Pico SDK 2.3.1 pins: CYW43 driver
`055d64274b014dd7b1c2fc94d26e8a18face7124` and lwIP
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`. The driver and its bundled CYW43439
firmware patches are used under the Raspberry Pi semiconductor alternative grant in
[LICENSE.RP](licenses/cyw43-driver-raspberry-pi.txt), copyright 2019–2022 George Robotics
Pty Ltd, together with SDK BSD headers. This alternative covers RP2040 and RP2350 builds; it is restricted to Raspberry Pi semiconductor devices. No CYW43 driver
source or blob is linked into the host or ESP32 firmware. lwIP's BSD terms and
Swedish Institute of Computer Science copyright are preserved in
[its license](licenses/lwip-pico-sdk-2.3.1.txt).

The bounded Pico AP DHCP server adapts MicroPython DHCP lease and reply logic
from Raspberry Pi pico-examples, copyright 2018–2019 Damien P. George, under
[MIT](licenses/dhcp-server.txt). Packet validation and the portable testable core
are maintained in this repository. It is excluded from USB-only firmware.

ESP32 networking adds the pinned ESP-IDF Wi-Fi binaries, copyright 2015–2016
Espressif Systems, distributed in object form under
[Apache-2.0](licenses/esp32-wifi-libraries.txt), plus ESP-IDF's HTTP server and
native network/event/NVS services under the SDK's retained Apache headers.
ESP-IDF's lwIP license is retained [separately](licenses/lwip-esp-idf-5.5.3.txt).
The HTTP parser's MIT terms and Joyent/Node contributors' copyright are retained
[in its notice](licenses/http-parser-esp-idf.txt). Selected networking source
headers are preserved [here](licenses/networking-additional-notices.txt).

These optional dependencies are excluded from USB-only native SDK dependency
closures. Their notices are copied alongside build artifacts with the existing
license bundle. Upstream sources: [CYW43 driver](https://github.com/georgerobotics/cyw43-driver/tree/055d64274b014dd7b1c2fc94d26e8a18face7124),
[lwIP for Pico SDK](https://github.com/lwip-tcpip/lwip/tree/77dcd25a72509eb83f72b033d219b1d40cd8eb95),
[ESP-IDF Wi-Fi libraries](https://github.com/espressif/esp-idf/tree/v5.5.3/components/esp_wifi/lib).

## Redistribution

Distribute this file and the `licenses/` directory with firmware or desktop
executables. CMake copies them and LightVM's LICENSE beside each build's output.
If distributing dependency sources or Python packages, retain their original
per-file copyright/license notices and any additional notices supplied by their
own distributions.

Upstream sources: [Pico SDK 2.3.1](https://github.com/raspberrypi/pico-sdk/tree/079c6f39023649b154152db30f1d781e884879bc),
[TinyUSB](https://github.com/hathach/tinyusb/tree/86ad6e56c1700e85f1c5678607a762cfe3aa2f47),
[pySerial](https://github.com/pyserial/pyserial/tree/v3.5),
[Newlib 4.3.0](https://github.com/mirror/newlib-cygwin/blob/newlib-4.3.0/COPYING.NEWLIB),
and [GCC runtime exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/COPYING.RUNTIME).

ESP32 upstream sources: [ESP-IDF v5.5.3](https://github.com/espressif/esp-idf/tree/v5.5.3),
[RMT encoder example](https://github.com/espressif/esp-idf/blob/v5.5.3/examples/peripherals/rmt/led_strip/main/led_strip_encoder.c),
[TLSF](https://github.com/espressif/tlsf/tree/2867f6883a12920b1969ff9624c0ab0e4185c2ce),
and [Espressif toolchain release](https://github.com/espressif/crosstool-NG/releases/tag/esp-14.2.0_20251107).

The build-only UF2 utility is picotool 2.3.1, commit
`2041936441b48a3cc53ae3da9e805229fe8f4e18`, selected by SDK 2.3.1. It is not
linked into firmware or shipped in the browser bundle. Its upstream source and
notices are in [picotool](https://github.com/raspberrypi/picotool/tree/2041936441b48a3cc53ae3da9e805229fe8f4e18).
