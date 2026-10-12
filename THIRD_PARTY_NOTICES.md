# Third-party notices

LightVM's original code and documentation are licensed under the MIT License in
[LICENSE](LICENSE), copyright 2026 Kaj Wik Siebert. Dependencies retain the
licenses and copyright notices listed below. The review covers the current
native receiver, RP2040 Pico build and pinned Python tools.

| Component | Version | License | Use | Preserved notice |
|---|---|---|---|---|
| wasm3 | v0.5.0 | MIT | Embedded interpreter | [License](licenses/wasm3-v0.5.0.txt) |
| Pico SDK | 1.5.1 | BSD-3-Clause; selected files MIT | Pico firmware | [BSD license](licenses/pico-sdk-1.5.1.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| TinyUSB | SDK submodule `86c416d4c0fb38432460b3e11b08b9de76941bf5` | MIT | USB device driver | [License](licenses/tinyusb.txt), [per-file notices](licenses/pico-sdk-additional-notices.txt) |
| pySerial | 3.5 | BSD-3-Clause | Desktop serial client | [License](licenses/pyserial-3.5.txt) |
| Wasmtime Python | 25.0.0 | Apache-2.0 WITH LLVM-exception | Desktop WAT compilation and tests | [License and exception](licenses/wasmtime-py-25.0.0.txt) |
| Newlib | 4.3.0, Arm GNU Toolchain 13.2.Rel1 | Per-source permissive licenses for the linked ARM C/math routines | Firmware C and math libraries | [Upstream license collection](licenses/newlib-4.3.0.txt) |
| GCC runtime | Arm GNU Toolchain 13.2.Rel1 | GPL-3.0-or-later WITH GCC-exception-3.1 | Firmware compiler support routines | [GPL](licenses/gcc-gpl-3.0.txt), [runtime exception](licenses/gcc-runtime-exception.txt) |

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

## Dependency modifications

CMake adds a loop-yield check to a build-local copy of wasm3's `m3_exec.h` and
redirects its allocator calls through LightVM's tracked allocator. The upstream
source checkout is unchanged; its MIT notice remains in the copied source and
in the interpreter notice above.

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
