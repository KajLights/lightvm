# Third-party notices

LightVM's original code and documentation are licensed under the MIT License in
[LICENSE](LICENSE), copyright 2026 Kaj Wik Siebert. Dependencies retain the
licenses and copyright notices listed below. The review covers the current
native receiver, RP2040 Pico and XIAO ESP32 builds, and pinned Python tools.

| Component | Version | License | Use | Preserved notice |
|---|---|---|---|---|
| wasm3 | v0.5.0 | MIT | Embedded interpreter | [License](licenses/wasm3-v0.5.0.txt) |
| Pico SDK | 1.5.1 | BSD-3-Clause; selected files MIT | Pico firmware | [BSD license](licenses/pico-sdk-1.5.1.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| TinyUSB | SDK submodule `86c416d4c0fb38432460b3e11b08b9de76941bf5` | MIT | USB device driver | [License](licenses/tinyusb.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| Pico examples WS2812 PIO | 1545dfd9a186 | BSD-3-Clause | Adapted PIO program | [License](licenses/pico-examples.txt), copyright notice in [source](platforms/pico/ws2812.pio) |
| pySerial | 3.5 | BSD-3-Clause | Desktop serial client | [License](licenses/pyserial-3.5.txt) |
| Wasmtime Python | 25.0.0 | Apache-2.0 WITH LLVM-exception | Desktop WAT compilation and tests | [License and exception](licenses/wasmtime-py-25.0.0.txt) |
| Newlib | 4.3.0, Arm GNU Toolchain 13.2.Rel1 | Per-source permissive licenses for the linked ARM C/math routines | Firmware C and math libraries | [Upstream license collection](licenses/newlib-4.3.0.txt) |
| GCC runtime | Arm GNU Toolchain 13.2.Rel1 | GPL-3.0-or-later WITH GCC-exception-3.1 | Firmware compiler support routines | [GPL](licenses/gcc-gpl-3.0.txt), [runtime exception](licenses/gcc-runtime-exception.txt) |
| ESP-IDF | v5.5.3 (`2c211b236707889e8400c4dc5644dd5c4ee071e0`) | Apache-2.0; selected MIT/BSD components below | S3/C6 SDK, USB Serial/JTAG and RMT | [Apache license](licenses/esp-idf-5.5.3.txt), [per-file copyright/attribution](licenses/esp-idf-additional-notices.txt) |
| FreeRTOS / Xtensa support | IDF-selected kernel 10.5.1 and SDK ports | MIT | ESP32 scheduling and native context support | [Preserved copyright/license headers](licenses/esp-idf-additional-notices.txt) |
| TLSF | IDF submodule `2867f6883a12920b1969ff9624c0ab0e4185c2ce` | BSD-3-Clause | Native allocator and ROM allocator patches | [BSD conditions](licenses/tlsf-esp-idf-5.5.3.txt), [copyright headers](licenses/esp-idf-additional-notices.txt) |
| Mbed TLS / micro-ecc | IDF-selected submodules | Apache-2.0 option / BSD-2-Clause | SDK/bootloader security components (unused features are discarded) | [Mbed TLS license](licenses/mbedtls-esp-idf-5.5.3.txt), [selected headers and micro-ecc license](licenses/esp-idf-additional-notices.txt) |
| Espressif Newlib | 4.3.0, toolchain `esp-14.2.0_20251107` | Per-source permissive licenses | S3/C6 C/math routines | [License collection](licenses/newlib-esp-14.2.0_20251107.txt) |
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

## Independent Python animation frontend provenance

The `lightvm/` SDK/compiler/easing and `samples/python/` effects are original
LightVM implementations under the repository MIT license. They reuse no
picolights or Adafruit source. Picolights supplied behavioural motivation only,
as recorded in the control and composition feature proposals. Its
`picolights/easing.py` explicitly declares GPLv3 and is excluded, along with
mechanical translations of its easing/helpers. Linear, polynomial and smoothstep
curves are implemented directly from their equations; the deterministic star
mixer is original modular arithmetic. Twinkle intentionally uses procedural
per-pixel lifetimes rather than picolights' stateful star slots.

Python AST/symbol-table parsing uses the desktop standard library. The existing
pinned Wasmtime package assembles WAT on the desktop under its retained license;
neither Python nor Wasmtime code is embedded in generated guest modules. Native
RGB/HSV conversion reuses LightVM's own existing MIT-licensed implementation. This
addition does not change the licenses of the firmware SDKs and build dependencies
above.

## Existing dependency modifications

The WS2812 PIO program is adapted from Pico examples commit 1545dfd9a1862961904856d46a4e05f35454d4be.
Only the single-strip program is used; the newer PIO version directive is omitted
for SDK 1.5.1 compatibility. The Raspberry Pi copyright and BSD license identifier
are retained. Source: https://github.com/raspberrypi/pico-examples/blob/1545dfd9a1862961904856d46a4e05f35454d4be/pio/ws2812/ws2812.pio


CMake adds a loop-yield check to a build-local copy of wasm3's `m3_exec.h` and
redirects its allocator calls through LightVM's tracked allocator. The upstream
source checkout is unchanged; its MIT notice remains in the copied source and
in the interpreter notice above.

The shared native build disables strict-aliasing optimization for wasm3's
intentional type-punned code/stack representation. This changes compile options,
not the dependency source.

`platforms/esp32/main/ws2812_encoder.c` adapts the composite byte/copy encoder
from ESP-IDF v5.5.3's `examples/peripherals/rmt/led_strip/main/led_strip_encoder.c`.
Its Espressif copyright and Apache-2.0 identifier are retained. LightVM changes
the timings to integer 800 kHz symbols and a 300 us reset at 20 MHz, and uses
the encoder through the shared synchronous native output contract.

The ESP32 project compiles only its required SDK component closure. Radio stacks,
vendor radio libraries and PSRAM are not part of this first port. Preserve the
selected SDK copyright headers and permissive license texts supplied here with
the application and bootloader. Additional SDK features/dependencies require
their own notice review when introduced; these notices do not cover every
optional component in an ESP-IDF source checkout.

The native Pico SDK, USB and PIO integration lives in `platforms/pico/`. The
shared engine has no Pico SDK dependency. Future platform ports must preserve
the notices for their selected native SDKs and linked services.

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
