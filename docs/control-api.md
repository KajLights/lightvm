# Bytecode control API

Python programs use the [init/render animation API](animation-profile.md) and
compile to portable bytecode with an unversioned `lightvm.program` document kind.
Named [Python transitions](transition-profile.md) use that same document kind with
`profile: "transition"` and are selected between A/B compositions by stored program name.
The manifest records `compiler_version` for provenance and `requires` for its
minimum application release. Firmware accepts the same major at or above that
minimum, independently of the compiler's release. Settings are frozen at
initialization; contributions use premultiplied RGBA.
See [the release contract](versioning.md). Retired experimental runtimes remain removed.

```sh
python -m lightvm.compiler examples/animations/rainbow.py -o build/examples/rainbow.lbc
python tools/client.py --port PORT --store-program rainbow build/examples/rainbow.lbc
python tools/client.py --port PORT --activate rainbow
```

`compile_source(source, filename, target)` returns bytecode, manifest, diagnostics,
advisories and success. Advisories are source-located performance notes and do not
make a compilation fail. The CLI writes `.lbc` plus a JSON manifest. The browser uses the same
compiler. Target checks use the controller’s `version`, the program’s `requires`,
and the controller’s module limit.
Compilation never executes source.

## Execution, storage and ownership

Each native instance owns registers and a checked view of a native RGBA working
buffer. Guest numbers cannot become pointers. Admission checks instruction types,
register write permissions, branch/entry boundaries, palette sizes, setting declarations and the manifest before execution. A normal resume slice executes at
most 4096 dispatches and also yields at the configured wall-time quantum;
cooperative servicing can request smaller slices. The per-invocation weighted work
limit faults runaway initialization/rendering; scalar operations, map attempts
and resource chunks are charged as described in the [emitter contract](bytecode-emitter.md#prepared-pixel-kernels). Native operations use bounded
chunks, with their size depending on worker availability. Output transfer failures do not fault the animation; playback
continues and retries output on subsequent frames.
The renderer retains its execution state across slices. Scene preparation is a
controller-owned operation that returns to foreground ingress between bounded
units; explicit manual execution and ordinary storage commands retain cooperative
checkpoints. See [scene preparation](scene-preparation.md) for acceptance,
query/cancel, progress, persistence and completion semantics.

Both named and transient packages execute from immutable flash extents through
a bounded reader/cache. Uploads use one shared 256-byte page buffer, and
`STATUS` reports `code_storage=flash`. Running instances pin the exact extent;
replacing a catalogue name cannot overwrite their code.

FINISH prepares a transient candidate when a target is configured. Named save
validates without executing; activation prepares a candidate before replacing the
old instance and queues its first presentation. Failed upload, validation or preparation retains the
old instance and generation. Each output’s `startup` selects off, restore last
state or a saved preset. Presets supply their layers’ settings and input bindings.
Boot resolves the current preset definition; activation does not update configuration or select a separate boot revision.

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
Compositions add `9N` bytes for shared RGB output buffers; see [composition](composition.md).
Native driver conversion storage adds `4N` bytes on Pico and `3N` on ESP32, plus SDK state.
Rendering does not allocate. Each configured output runs independently and can retain [A/B compositions and a transition](transitions.md).
Virtual RGB outputs use the same frame storage without physical driver conversion buffers.

## Management

Operations accept an optional `output` and composition `slot` A or B. Each output owns its
generation, settings and playback state.

- `program_state`: manifest, generation, settings_values, playback state,
  render count and elapsed program time. Settings have no live parameter writes.
- `program_reset`: expected_generation plus optional settings overrides; prepares a
  fresh instance and replaces the old instance only after preparation succeeds.
- `program_pause` / `program_resume`: expected_generation; retain initialized state.
- `program_snapshot`: passive RGB output and contribution/output validity/counters.

`program_state` also reports `render_reusable`, `displayed_frame`,
`distinct_presentations`, `displayed_sample_age_us` and
`completion_to_display_us`. The latter is the last displayed contribution's delay
from completion to its first successful physical presentation. Repeated output
refreshes do not increment the distinct count or replace that timestamp.
`dependency_revision`, `dirty_age_us` and `dirty_to_display_us` distinguish
observed input changes from the first output carrying them. Repeated frames with
the same dependency revision do not inflate the last dirty delay. Cumulative
`runnable_wait_us`, `composite_wait_us` and `output_wait_us` report coordinator
wait categories (closed at the next category observation).
`composition_get` adds `sampled_at_us` and these metrics per layer, plus
`dependency_current` (null when reuse is unsupported). Use differences between
server sample times and matching layer generations to calculate rates. See
[multicore measurements](multicore-rendering.md) for provenance and visibility limits.

Management uses `LVM1` magic, framing version 1, a 16-byte header and CRC32.
Client and firmware compatibility follows the application release contract.
The fixed frame marker identifies the binary encoding; it is not an independently
released API version.
Removed command 5 and operations such as live parameter writes return an unknown
command/operation error. `mode` reports bytecode while playing.
See [configuration and management](configuration.md).

[A/B transition controls](transitions.md#management-api) use the same native owner and management transport.
