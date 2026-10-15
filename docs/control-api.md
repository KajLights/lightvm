# Portable bytecode control profile

The six current examples use the opt-in [animation/1 API](animation-profile.md):
persistent arrays/buffers, initialization, preparation settings and fixed logical
ticks. The legacy LBC1/2 API below remains supported for unmarked source.

The current firmware replaces wasm3 with a limited typed interpreter. The same
`LBC1/2` package runs on compatible host, Pico and ESP32 builds. This is a proof of
the [portable bytecode architecture](../documents/architecture-decision-portable-bytecode.md),
not the entire component system. Device configuration, drivers, USB/HTTP commands,
procedure storage, controls and snapshots retain their existing contracts.

## Authoring and compilation

```python
from lightvm import parameter, hsv, step, prepare, shade
speed = parameter(0.1, minimum=-10.0, maximum=10.0)
phase = 0.0
stride = 0.0

@step
def update():
    global phase
    phase = (phase + delta * speed) % 1.0

@prepare
def frame():
    global stride
    stride = 1.0 / count

@shade
def pixel():
    return hsv(phase + index * stride, 1.0, 1.0)
```

`@step` advances scalar simulation state. `@prepare` derives frame state.
`@shade` returns a color per pixel; alternatively, `@kernel` writes
`pixels[index]` or calls `pixels.fill(color)`. Count, index, time and delta are
injected by the runtime. Time/delta are seconds; state and parameters are scalar
values. The compiler recognizes these names statically; Python source never runs
on the device or during compilation.

```sh
python -m lightvm.compiler tests/legacy_samples/rainbow.py -o build/samples/rainbow.lbc
python tools/client.py --port PORT --store-procedure rainbow build/samples/rainbow.lbc
python tools/client.py --port PORT --activate rainbow
```

`compile_source(source, filename, target)` returns `bytecode`, `manifest`,
`diagnostics` and `success`. The CLI writes `.lbc` and a JSON manifest sidecar.
The browser uses the same compiler and no longer needs a WAT assembler.
Target checks require `bytecode_format: "LBC1/2"`, control ABI 2 and manifest
schema 3. Python syntax was deliberately adapted; old `@on_frame` sources and
WASM binaries must be migrated/recompiled. The previous simplified examples remain in `tests/legacy_samples` as LBC1/2
compatibility fixtures; the current examples use animation/1 settings.

The subset supports typed scalar literals, parameters, immutable RGB palettes,
arithmetic, `<`, `>`, `==`, conditionals and `range(bound)` loops in kernels.
Helpers are `rgb`, `hsv`, `hash01`, `int`, `float`, `min`, `max` and palette `len`.
It excludes general Python objects, dynamic allocation, arbitrary imports,
user-defined helper calls, recursion, exceptions and pixel readback.

Numbers are wrapping signed i32, IEEE binary32, bool and packed RGB8. Integer
floor division/remainder follow Python sign rules. Division by zero, signed
division overflow, invalid float-to-int conversions and invalid colors fault.
Arithmetic uses separate binary32 rounding, including fused bytecode sequences;
compiler FMA contraction is disabled. Original f64 rainbow/twinkle replay allows
one RGB channel level of difference; the matched f32 native rainbow must agree
byte for byte. Parameters remain JSON bool/int/float/RGB8 on the device API;
values outside the new numeric domain are rejected.

## Execution, storage and ownership

Each native instance owns registers and a checked view of a native RGB working
buffer. Guest numbers cannot become pointers. Admission checks instruction types,
register write permissions, branch/entry boundaries, palette sizes, parameter
mapping/defaults and the manifest before execution. A resume slice executes at
most 64 instructions; native fill writes at most 32 pixels per dispatch. A whole
invocation has a two-million-instruction limit and a 500 ms recovery deadline.
The current device scheduler completes a frame synchronously by resuming these
slices. Interleaving management during an unfinished frame is future work.

Normal stored procedure activation reads code from the existing flash store,
using a 256-byte instruction/palette cache and bounded register descriptors.
It does not expand code or copy the whole package into RAM. `STATUS` reports
`code_storage=flash`. Transient BEGIN/CHUNK/FINISH uploads use the existing RAM
slot and report `code_storage=ram`.

The unchanged snapshot store alternates banks. Before a store mutation, the core
preserves the running bytes in the existing RAM slot so the next erase cannot
invalidate them. It rebinds to flash when the resulting catalog still contains
the same package. Replacing the active name keeps its old bytes/state in RAM until
explicit activation; repeated bank rotations are tested. Configuration, Wi-Fi
settings, partitions and flash layout are unchanged. Flash operations retain the
existing store's blocking behavior; smooth animation during saves is not promised.

First PLAY/FRAME prepares and renders initial declared state at time zero without
advancing `@step`. Subsequent frames advance the pausable program clock, execute
`@step`, prepare and render. Autonomous playback uses actual elapsed time as the
existing control scheduler did. FRAME advances by its explicit 0..1000 ms delta.
Pause/resume excludes paused time; STOP clears physical output. RESET,
reconfiguration and activation reset state. Serial disconnection retains playback.
The reserved RUN command returns the existing control-program error; arbitrary
WASM exports, WASI and console imports are no longer supported.

Working pixels start black. A successful frame commits the contribution; the
native presenter calls the existing synchronous driver, which consumes/copies
RGB before returning. Wire order and brightness remain driver responsibilities.
Faults discard incomplete paint and preserve the last published output. Scalar
state is not rolled back. Reactivation/reset is required after a fault.

Displayed slots retain the existing `9N` bytes for working, committed and output
RGB; offscreen instances use `6N`. Rendering does not allocate. This PoC does not
change driver buffering or introduce DMA, multiple active slots, composition or
hot-swap transactions beyond the existing procedure lifecycle.

## Package and control metadata

The package begins with `LBC1`, version 2 and a 64-byte little-endian header.
It contains up to 128 typed register descriptors, 2048 fixed-width instructions,
1024 palette colors and an embedded manifest of at most 8192 bytes. Each descriptor
and instruction is eight bytes; palette colors are four bytes. Header byte 36
holds the manifest length; its offset follows the palette. Reserved header fields
must be zero. The actual package must also fit the board's `module_limit`.

```json
{"schema":"lightvm.program/3","abi":2,"pixels":{"format":"rgb8"},"parameters":[{"name":"speed","type":"float","default":0.1,"minimum":-10.0,"maximum":10.0}]}
```

Manifest parameter order maps to declared parameter registers; admission verifies
the count, types and defaults. Names, inclusive bounds, eight-parameter limit and
strict JSON validation are unchanged. No wiring or controller identifiers appear
in the package. The standalone experiment's version-1 packages lack this metadata
and must be recompiled for the integrated firmware.

Existing management operations remain:

- `program_parameters_get`: manifest, values, generation and parameter revision.
- `program_parameters_set`: atomic checked updates with expected generation/revision.
- `program_pause` / `program_resume`: lifecycle operations with expected generation.
- `program_snapshot`: passive RGB output plus contribution/output validity, counters,
  paused/fault state and program time.

The binary framing (including the historical `WASM` transport magic), command IDs,
CRC, HTTP routes, errors and stored-object management remain compatible. Only the
program payload changes to bytecode. `mode` now reports `bytecode` while playing;
`linear_limit` is zero. See [configuration and management](configuration.md).
