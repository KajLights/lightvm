# Bytecode control API

Python programs use the [init/render animation API](animation-profile.md) and
compile to portable bytecode with an unversioned `lightvm.program` document kind.
The manifest records `compiler_version` for provenance and `requires` for its
minimum application release. Firmware accepts the same major at or above that
minimum, independently of the compiler's release. Settings are frozen at
initialization; contributions use premultiplied RGBA.
See [the release contract](versioning.md). Retired experimental runtimes remain removed.

```sh
python -m lightvm.compiler examples/animations/rainbow.py -o build/examples/rainbow.lbc
python tools/client.py --port PORT --store-procedure rainbow build/examples/rainbow.lbc
python tools/client.py --port PORT --activate rainbow
```

`compile_source(source, filename, target)` returns bytecode, manifest, diagnostics
and success. The CLI writes `.lbc` plus a JSON manifest. The browser uses the same
compiler. Target checks use the device’s `version`, the program’s `requires`,
and the device’s module limit.
Compilation never executes source.

## Execution, storage and ownership

Each native instance owns registers and a checked view of a native RGBA working
buffer. Guest numbers cannot become pointers. Admission checks instruction types,
register write permissions, branch/entry boundaries, palette sizes, setting declarations and the manifest before execution. A resume slice executes at
most 64 instructions; native fill writes at most 32 pixels per dispatch. Slices yield and resume without a cumulative instruction cutoff or timed
execution watchdog. Output transfer failures do not fault the animation; playback
continues and retries output on subsequent frames.
The renderer retains its execution state and returns to the event loop after at
most 4096 dispatches. Validation, preparation,
manual execution and persistence also service native ingress and the existing
animation between bounded pieces of work. Commands remain serialized.

Both named and transient packages execute from immutable flash extents through
a bounded reader/cache. Uploads use one shared 256-byte page buffer, and
`STATUS` reports `code_storage=flash`. Running instances pin the exact extent;
replacing a catalogue name cannot overwrite their code.

FINISH prepares a transient candidate when a target is configured. Named save
validates without executing; activation prepares and presents a candidate before
replacing the old instance. Failed upload, validation or preparation retains the
old instance and generation. Each device’s `startup` names its boot procedure and
optional initialization `args`. Boot resolves the current catalogue entry for each
name; activation does not update configuration or select a separate boot revision.

Firmware advertising `transient_settings: true` also accepts a FINISH payload
`{"settings": {...}}` (within the 512-byte frame limit). Overrides are validated
and applied while preparing the candidate, before replacing the old instance.
An empty FINISH payload retains the existing default-settings behavior. BEGIN's
optional output ID routes the upload; PLAY and subsequent management calls must
address that same output explicitly.

Initialization and render callbacks follow the [current source contract](animation-profile.md).
FRAME advances by its explicit 0..1000 ms delta. Pause/resume excludes paused time;
STOP clears physical output. RESET unloads state. Serial disconnection cancels the
upload and retains playback.
Working pixels start transparent. A successful frame commits the contribution. The
native presenter borrows an immutable complete RGB frame until completion or
cancellation; output counters advance after successful
physical presentation. Pico uses PIO/DMA, ESP32 uses RMT. Manual command replies
wait for output while servicing native ingress. Cancellation releases retained
references before reset, replacement or destruction. Order and brightness remain
native driver responsibilities. Faults discard incomplete paint and preserve the
last published output; scalar state is not rolled back.

New direct instances use `14N` frame bytes: two RGBA frames, one RGB presentation frame,
and the last physical RGB output. Offscreen instances use `8N` bytes.
Scenes add `9N` bytes for shared RGB output buffers; see [composition](composition.md).
Native driver conversion storage adds `4N` bytes on Pico and `3N` on ESP32, plus SDK state.
Rendering does not allocate. Each configured device has its own slot A and runs independently.
Virtual RGB devices use the same frame storage without physical driver conversion buffers.

## Management

Operations accept an optional `device` and its slot A. Each device owns its
generation, settings and playback state.

- `program_state`: manifest, generation, settings_values, playback state,
  render count and elapsed program time. Settings have no live parameter writes.
- `program_reset`: expected_generation plus optional settings overrides; prepares a
  fresh instance and replaces the old instance only after preparation succeeds.
- `program_pause` / `program_resume`: expected_generation; retain initialized state.
- `program_snapshot`: passive RGB output and contribution/output validity/counters.

Management uses `LVM1` magic, framing version 1, a 16-byte header and CRC32.
Client and firmware compatibility follows the application release contract.
The fixed frame marker identifies the binary encoding; it is not an independently
released API version.
Removed command 5 and operations such as live parameter writes return an unknown
command/operation error. `mode` reports bytecode while playing.
See [configuration and management](configuration.md).
