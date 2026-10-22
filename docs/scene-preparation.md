# Scene preparation and operation lifetime

Scene changes are controller-owned asynchronous operations. The command reply
acknowledges acceptance; it does not claim that a scene is active or visible.
The controller continues rendering the old scene and returns to foreground USB,
HTTP/WebSocket and MQTT ingress between preparation units.

This applies to transient program FINISH with an output, stored activation,
composition/preset activation, preset recall and power transitions, program/layer
reset and replacement, transition selection, preset validation/save and
configuration validation/apply/save. Explicit manual FRAME and pause/stop retain
their deterministic synchronous state semantics. An upload still has a session
owner; an accepted preparation does not.

## Protocol

An accepted command returns a DATA object with an `operation` and a successful
STATUS whose value is its ID. Query or cancel it using management requests:

```json
{"op":"operation_get","boot_id":"BOOT_ID_FROM_ACCEPTANCE","id":42}
```

```json
{"op":"operation_cancel","boot_id":"BOOT_ID_FROM_ACCEPTANCE","id":42}
```

The response is `{"operation":{...}}`, with these fields:

| Field | Meaning |
| --- | --- |
| `boot_id`, `id`, `output` | Controller boot, monotonically increasing operation ID and output identity |
| `stage`, `units` | Current stage and number of completed coordinator units; not a percentage |
| `terminal`, `code`, `error`, `value` | Terminal outcome; a nonzero terminal code is failure/cancellation, even though the query itself succeeded |
| `accepted_us`, `finished_us` | Acceptance and terminal times in the controller monotonic clock |
| `activated_us` | Atomic scene installation, or zero for operations that only validate/save |
| `first_presented_us` | First completed output sampling the installed generation, or zero while unobserved |
| `fade_completed_us` | Completion of this operation's recall fade, or zero while unobserved/not applicable |
| `max_step_us` | Largest observed preparation/cleanup unit, including native and flash work |
| `stage_us` | Accumulated execution time per stage; excludes time waiting between units |

Stages include `accepted`, `load`, `decode`, `verify`, `admit`, `initialise`,
`first_render`, `compose`, `ready`, `refresh`, `persist`, and terminal `complete`,
`failed`, `cancelled` or `superseded`. Timings are charged to the stage at unit
entry; a stage boundary can therefore include the final unit of its predecessor.
Zero timestamps are sentinels, not estimates. `complete` means preparation and
installation/persistence finished; presentation/fade milestones can advance in
the retained terminal record afterwards. Contribution sampling does not prove
optical visibility through opaque layers or arbitrary transition code. A replaced
operation's milestones stop tracking when its target revision no longer matches.

Python `Client` and browser `LightVMClient` helpers wait for terminal success and
raise on terminal failure. Their waits release the command queue between polls.
Python `finish_upload()` handles FINISH; `wait_preparation()` waits for all boot
restores. Raw command callers must perform this polling themselves. A timeout or
transport disconnect does not cancel controller work. Retain the boot ID and ID,
reconnect, and query. The editor retains the operation, shows its stage, and offers
Cancel and Check preparation. A reboot or expired history returns an explicit
unknown/expired operation error.
If retained history expires, the editor marks that result unavailable and keeps
refreshing live controller state. Python upload waits have a deadline and report
the operation identity for a later query.

## Ownership and ordering

The VM heap pays for a queue of at most four operations, one allocated candidate
at a time, and eight terminal summaries. A newer accepted request supersedes
uncommitted work for its output; other outputs retain FIFO order. Admission may
return busy or out-of-memory without changing the active scene. Configuration
operations cancel conflicting preparation and exclude new scene admission until
they finish. Boot restores are fed into the bounded queue as space becomes
available, including configurations with more than four startup outputs.

Requests own copies of settings/inputs/documents and pin exact immutable program
and preset extents. No parser pointers, transport callbacks, or borrowed reset
instance survive acceptance. Generation and revision checks run before transfer.
Failed scene preparation preserves the old scene. Cancelling at a safe boundary
releases candidate buffers, package pins and incomplete writes. Destruction never
finishes a discarded guest invocation; a runaway guest cannot trap cleanup.

OFF, manual A/B takeover, reset, and configuration changes cancel conflicting
work. Slot and named-layer PLAY/STOP/RESET do so as well. Rejected stale lifecycle
requests leave preparation queued; revision invalidation aborts a candidate at
its next unit, without waiting for guest initialization to finish.
Brightness/colour edits continue to affect the active scene. Explicit
controls attached to the accepted scene apply only if no later light edit won.
Preparation recaptures current controls and allows one refresh of its first
frame before persistence. Continuous input changes cannot restart preparation
forever: later values are captured by subsequent live rendering. The controller's
light state at activation always respects newer edits. Persistence that was
already in progress is followed by a debounced save of those newer controls.

Recall state is durably published only after candidate admission, immediately
before its atomic installation. An incomplete write leaves the previous catalog
root valid. Cancellation has no yield point between publication and installation.
Configuration save writes each remembered output state and then the configuration;
these are separate durable records. Cancellation can leave updated remembered
states for the *already active* outputs, but cannot activate a failed candidate.
Native hardware configuration apply retains its existing failure contract: a
failed output-driver open leaves an empty active configuration and an error.

The flash writer excludes competing mutations while a staged write is active.
Readers and pinned running programs remain usable. Debounced light-state writes
wait for that writer; ordinary conflicting storage commands return busy/error.
HA observes applied state after installation and surfaces terminal preparation
failures in `mqtt_error`; early acceptance is not a successful preset publication.

## Bounded work and measurements

Each foreground cycle grants preparation one unit after rendering. JSON uses an
explicit cursor (depth 16, 256 tokens), and a parse/load unit processes at most
256 bytes. Package verification processes at most 32 instructions or 16 register
descriptors per unit. Resource zeroing uses 256-byte units, resource fills 32
pixels, guest init/render 256 dispatches plus the board's VM wall-time quantum,
composition 128 pixels, and transition baseline copies 256 bytes. Settings,
layer descriptors and cleanup have fixed admitted counts. There is no recursive
transport service under candidate execution.

Persistence computes/verifies CRCs in 256-byte units and writes one flash page or
erases one sector per unit. The final catalog commit marker, sync and readback
form one indivisible publication unit. Allocator calls, one native instruction,
output packing/driver operations, descriptor decoding and bounded destruction
can still exceed a VM quantum; `max_step_us`, stage totals and the foreground-gap
counter include their cost. The proposed 10 ms service gate and heavy-scene
completion improvement require physical measurements. Host correctness or a
Pico build does not establish either timing result.

See the [performance plan](design/pico-performance-plan.md) and
[stack accounting](design/pico-stack-accounting.md) for evidence and board gates.
