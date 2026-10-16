# Third-party notices

LightVM's original code and documentation are licensed under the MIT License in
[LICENSE](LICENSE), copyright 2026 Kaj Wik Siebert. Dependencies retain the
licenses and copyright notices listed below. The review covers the current
native receiver, RP2040 Pico and XIAO ESP32 builds, and pinned Python tools.

| Component | Version | License | Use | Preserved notice |
|---|---|---|---|---|
| wasm3 | v0.5.0 | MIT | Historical interpreter; no longer linked | [License](licenses/wasm3-v0.5.0.txt) |
| Pico SDK | 1.5.1 | BSD-3-Clause; selected files MIT | Pico firmware | [BSD license](licenses/pico-sdk-1.5.1.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| TinyUSB | SDK submodule `86c416d4c0fb38432460b3e11b08b9de76941bf5` | MIT | USB device driver | [License](licenses/tinyusb.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| pico⚡flash PICOBOOT package | commit `230759cf32ab245decf1194f5b37b7d3c0088aed` | MIT | Browser WebUSB Pico flashing | [License](licenses/picoflash.txt), copyright 2025–2026 Piers Finlayson |
| Pico examples WS2812 PIO | 1545dfd9a186 | BSD-3-Clause | Adapted PIO program | [License](licenses/pico-examples.txt), copyright notice in [source](platforms/pico/ws2812.pio) |
| pySerial | 3.5 | BSD-3-Clause | Desktop serial client | [License](licenses/pyserial-3.5.txt) |
| Wasmtime Python | 25.0.0 | Apache-2.0 WITH LLVM-exception | Historical WAT tools; no longer required | [License and exception](licenses/wasmtime-py-25.0.0.txt) |
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

Pico SDK 1.5.1's `src/rp2_common/pico_double/double_v1_rom_shim.S` grants use solely
on a Raspberry Pi Pico device and alternatively offers GPLv2 licensing. LightVM
selects `pico_set_double_implementation(lightvm compiler)` so that source is
neither compiled nor linked into the firmware. Double-precision operations use
the compiler/Newlib implementations; floating-point performance can differ from
the SDK's ROM-assisted implementation.

## Python animation frontend and example provenance

The compiler and native interpreter are LightVM implementations under the
repository MIT license. The six current `examples/animations/` effects are faithful
ports of Kaj's supplied picolights algorithms, with the accepted lifecycle,
timing and bounded Twinkle corrections documented in
[the animation API](docs/animation-profile.md). The source reference is Kaj's
picolights repository, commit `f6480b4845b2ba3af4fbf0ffb1f398d362dda772`.
The six files in `examples/animations/` are the only maintained sample implementations;
the editor, tools and tests all use them directly.

The GPL-marked `picolights/easing.py` is not included. Twinkle's sine-out curve
is implemented directly from its mathematical equation; no easing implementation
is copied. Rainbow's colourwheel compatibility follows the published
[CircuitPython rainbowio definition](https://github.com/adafruit/circuitpython/blob/main/shared-module/rainbowio/__init__.c);
LightVM implements the arithmetic locally rather than linking that module.

Python AST parsing uses the standard library. The compiler emits custom bytecode;
Wasmtime and wasm3 are no longer dependencies of the normal build. Their notices
are retained for historical artifacts. Native RGB/HSV conversion retains
LightVM's existing MIT implementation. This does not change the licenses of the
firmware SDKs and build dependencies above.

## Existing dependency modifications

The WS2812 PIO program is adapted from Pico examples commit 1545dfd9a1862961904856d46a4e05f35454d4be.
Only the single-strip program is used; the newer PIO version directive is omitted
for SDK 1.5.1 compatibility. The Raspberry Pi copyright and BSD license identifier
are retained. Source: https://github.com/raspberrypi/pico-examples/blob/1545dfd9a1862961904856d46a4e05f35454d4be/pio/ws2812/ws2812.pio


Historical WASM builds added a loop-yield check to a build-local copy of wasm3's `m3_exec.h` and
redirected its allocator calls through LightVM's tracked allocator. The upstream
source checkout is unchanged; its MIT notice remains in the copied source and
in the interpreter notice above.

Historical WASM builds disabled strict-aliasing optimization for wasm3's
intentional type-punned code/stack representation. This changes compile options,
not the dependency source.

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

Pico W networking uses the Pico SDK 1.5.1 pins: CYW43 driver
`8ef38a6d32c54f850bff8f189bdca19ded33792a` and lwIP
`239918ccc173cb2c2a62f41a40fd893f57faf1d6`. The driver and its bundled CYW43439
firmware patches are used under the RP2040-specific alternative grant in
[LICENSE.RP](licenses/cyw43-driver-rp2040.txt), copyright 2019–2022 George Robotics
Pty Ltd, together with SDK BSD headers. This alternative permits this Pico W
build; it is restricted to Raspberry Pi semiconductor devices. No CYW43 driver
source or blob is linked into the host or ESP32 firmware. lwIP's BSD terms and
Swedish Institute of Computer Science copyright are preserved in
[its license](licenses/lwip-pico-sdk-1.5.1.txt).

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
license bundle. Upstream sources: [CYW43 driver](https://github.com/georgerobotics/cyw43-driver/tree/8ef38a6d32c54f850bff8f189bdca19ded33792a),
[lwIP for Pico SDK](https://github.com/lwip-tcpip/lwip/tree/239918ccc173cb2c2a62f41a40fd893f57faf1d6),
[ESP-IDF Wi-Fi libraries](https://github.com/espressif/esp-idf/tree/v5.5.3/components/esp_wifi/lib).

## Redistribution

Distribute this file and the `licenses/` directory with firmware or desktop
executables. CMake copies them and LightVM's LICENSE beside each build's output.
If distributing dependency sources or Python packages, retain their original
per-file copyright/license notices and any additional notices supplied by their
own distributions.

Upstream sources: [wasm3](https://github.com/wasm3/wasm3/tree/v0.5.0),
[Pico SDK](https://github.com/raspberrypi/pico-sdk/tree/1.5.1),
[TinyUSB](https://github.com/hathach/tinyusb/tree/86c416d4c0fb38432460b3e11b08b9de76941bf5),
[pySerial](https://github.com/pyserial/pyserial/tree/v3.5),
[Wasmtime Python](https://github.com/bytecodealliance/wasmtime-py),
[Newlib 4.3.0](https://github.com/mirror/newlib-cygwin/blob/newlib-4.3.0/COPYING.NEWLIB),
and [GCC runtime exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/COPYING.RUNTIME).

ESP32 upstream sources: [ESP-IDF v5.5.3](https://github.com/espressif/esp-idf/tree/v5.5.3),
[RMT encoder example](https://github.com/espressif/esp-idf/blob/v5.5.3/examples/peripherals/rmt/led_strip/main/led_strip_encoder.c),
[TLSF](https://github.com/espressif/tlsf/tree/2867f6883a12920b1969ff9624c0ab0e4185c2ce),
and [Espressif toolchain release](https://github.com/espressif/crosstool-NG/releases/tag/esp-14.2.0_20251107).
