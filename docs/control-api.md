# Bytecode control API

Current Python programs use the [init/render animation API](animation-profile.md)
and compile to LBC1/4, execution ABI 4, schema `lightvm.program/5`. Settings are
frozen at initialization. The previous decorators and fixed-step scheduler are
removed. Stored LBC1/2 packages remain loadable; old source is not accepted by the
public compiler. LBC1/3 packages must be recompiled.

```sh
python -m lightvm.compiler examples/animations/rainbow.py -o build/examples/rainbow.lbc
python tools/client.py --port PORT --store-procedure rainbow build/examples/rainbow.lbc
python tools/client.py --port PORT --activate rainbow
```

`compile_source(source, filename, target)` returns bytecode, manifest, diagnostics
and success. The CLI writes `.lbc` plus a JSON manifest. The browser uses the same
compiler. Target checks require advertised LBC1/4, ABI 4, schema 5 and animation/1.
Compilation never executes source.

## Execution, storage and ownership

Each native instance owns registers and a checked view of a native RGB working
buffer. Guest numbers cannot become pointers. Admission checks instruction types,
register write permissions, branch/entry boundaries, palette sizes, parameter
mapping/defaults and the manifest before execution. A resume slice executes at
most 64 instructions; native fill writes at most 32 pixels per dispatch. Slices yield and resume without a cumulative instruction cutoff or timed
execution watchdog. Output transfer failures do not fault the animation; playback
continues and retries output on subsequent frames.
The v2 scheduler completes a frame through bounded resume slices with native
service checkpoints. The v4 `animation/1` renderer retains its continuation and
returns to the event loop after at most 4096 dispatches. Validation, preparation,
manual execution and persistence also service native ingress and the existing
animation between bounded pieces of work. Commands remain serialized.

Both named and transient packages execute from immutable flash extents through
a bounded reader/cache. Uploads use one shared 256-byte page buffer, and
`STATUS` reports `code_storage=flash`. Running instances pin the exact extent;
replacing a catalogue name cannot overwrite their code. Legacy snapshot
payloads are retained in place when their directory is migrated.

FINISH prepares a transient candidate when a target is configured. Named save
validates without executing; activation prepares and presents a candidate before
replacing the old instance. Failed upload, validation or preparation retains the
old instance and generation. Each device’s `startup` names its boot procedure and
optional initialization `args`. Boot resolves the current catalogue entry for each
name; activation does not update configuration or select a separate boot revision.

Initialization and render callbacks follow the [current source contract](animation-profile.md).
FRAME advances by its explicit 0..1000 ms delta. Pause/resume excludes paused time;
STOP clears physical output. RESET unloads state. Serial disconnection cancels the
upload and retains playback.
The reserved RUN command returns the control-program error; arbitrary WASM
exports, WASI and console imports are unsupported.

Working pixels start black. A successful frame commits the contribution. The
native presenter borrows an immutable complete RGB frame until completion or
cancellation; output counters advance after successful
physical presentation. Pico uses PIO/DMA, ESP32 uses RMT. Manual command replies
wait for output while servicing native ingress. Cancellation releases retained
references before reset, replacement or destruction. Order and brightness remain
native driver responsibilities. Faults discard incomplete paint and preserve the
last published output; scalar state is not rolled back.

Displayed slots use `9N` bytes for working, committed and output RGB; offscreen
instances use `6N`. Native conversion storage is additionally `4N` bytes on Pico
and `3N` on ESP32, plus SDK driver state. Rendering does not allocate. Each configured device has its own slot A and runs independently.
Composition of multiple slots onto one output remains future work. Virtual RGB
outputs have the same 9N RGB allocation and no physical conversion buffer.

## Management

Operations accept an optional `device` and its slot A. Each device owns its
generation, settings and playback state.

- `program_parameters_get`: manifest, generation, settings_values, playback state,
  render count and elapsed program time. ABI 4 has no live parameters.
- `program_parameters_set`: retained for stored ABI 2 packages only; ABI 4 rejects it.
- `program_reset`: expected_generation plus optional settings overrides; prepares a
  fresh instance and replaces the old instance only after preparation succeeds.
- `program_pause` / `program_resume`: expected_generation; retain initialized state.
- `program_snapshot`: passive RGB output and contribution/output validity/counters.

The historical WASM framing magic, command IDs, CRC, HTTP routes and stored-object
management remain compatible. `mode` reports bytecode while playing; linear_limit
is zero. See [configuration and management](configuration.md).
