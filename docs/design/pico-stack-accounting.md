# Pico stack-path accounting

## Combined teaching and audio source review — 28 October

The consolidation retains both reviewed callback sets below. The merged
`control.c` reports teaching faults through the existing instruction reader and
`pbc_fault_message`; its reads resolve to the same stored-program callbacks.
The fault instruction uses an eight-byte local buffer, while message formatting
reuses the existing response buffer. The control-source fingerprint is updated
after reviewing that combined path; historical build bounds below still refer
to their original images.
The default pinned audio-core path is now retained in the CMake cache, so a
clean build's audit resolves the same dependency directory as the compiler.

## Teaching diagnostics — 27 October

The new `pbc_fault_reason` and `pbc_fault_message` entry points can inline the
instruction/table reader. Their indirect reads use the existing `vm_read` and
stored-program reader targets; neither introduces a new callback type or a
recursive edge. The explicit read-callback set now includes both entry points.
Map tasks retain the failing instruction and reason per lane and propagate it
after joining. Rebuild the audit for actual task/frame sizes; older fixed scratch
estimates in compiler advice have been removed. The 100-pixel USB Pico Release
build passes with main/worker bounds 11,516/5,108 bytes and margins 4,868/3,084
bytes in unchanged 16/8 KiB stacks. Physical stress remains a separate gate.
The 100-pixel Pico W Release profile with Wi-Fi, HTTP, WebSocket, MQTT and boot
recovery also passes: 12,152/5,120 bytes, leaving 4,232/3,072 bytes. These builds
do not change the reviewed SDK sources or the existing recursion guards.
## Subscription and gateway audit — 27 October

The subscription adapter registers `audio_received` and `audio_resolved`, with
DNS timers enabled for MQTT or audio. Audio no longer enables IGMP; HTTP discovery
still does. The receiver copies at most 700 bytes for each of eight datagrams per
poll and calls the common LVAF decoder and subscriber. It performs no allocation,
FFT or rendering in the receive callback. DNS cancellation clears the owner pointer
before a provider is freed; an outstanding resolver slot remains occupied until
its callback returns.

Transport callbacks in `audio_sources_tick`, `audio_sources_disconnect` and
provider removal resolve to the Pico open/send/close functions. Scope/status writer
callbacks resolve to the existing app/control writers. Gateway commands perform
bounded assembly independently of upload staging. Transport closure invalidates
availability immediately and defers reclamation until the next engine tick, so a
streamed status reply cannot lose storage beneath its writer. Existing command
reentrancy guards and stack margins remain unchanged.

The fingerprint manifest includes these project paths and the pinned portable
wire/subscriber core. SDK identities remain guarded; no unknown callback or frame
is treated as zero. The full Wi-Fi/HTTP/WebSocket/MQTT/audio Pico W configuration
passes the software gate with 16 KiB foreground and 8 KiB worker reservations,
16 KiB native headroom and the existing pixel driver reservation. The short
physical subscription checks below now record stack watermarks; broader stack
qualification remains separate from these functional checks.

The preset source editor adds port, expected identity and synthetic-input policy
to `audio_status`. Review confirmed that serialization reuses the existing
384-byte buffer and writer callback, with no new allocation, callback target or
recursion. Only `audio_sources.c` changed in the guarded fingerprints. The rebuilt
Pico W image retains the 12,184/5,104-byte main/worker bounds, 134,512 static bytes,
64,400-byte automatic VM heap and unchanged stack/headroom reservations.

Playback admission now uses a bounded four-provider reservation plan with borrowed
configuration pointers. It allocates no extra persistent state and changes no
transport callback target, recursion guard or interrupt path. Scope and playback
admission call the existing provider allocation/removal paths; failed acquisition
unwinds references before any gateway policy is adopted. The reviewed source set
now includes `vm.c` for direct PLAY admission. The rebuilt image retains all the
bounds and reservations above (543 guarded SDK files, 35 project files).

Manual driver, layer/input and retained-scene preparation admission uses the same
four-provider plan. The asynchronous operation owns its provider references across
persistence ticks; config pointers are consumed during admission, before any scene
can be destroyed. Cancellation and superseded queued resets release all ownership.
Presenter and progress-binding allocations precede live mutation. These changes
add no recursion, IRQ, worker job or transport callback. The extracted
`commit_prepared_scene` uses the existing pixel-driver callback targets, now listed
under that function as well. The source manifest includes preparation and transition
headers and transition implementation (543 SDK files, 38 project files). Stack,
VM heap and native-headroom reservations remain unchanged; see the current
[lifecycle validation record](../validation/audio-manual-admission-2026-10-27.json)
for the linked bounds.

The 27 October [physical subscription check](../validation/audio-hardware-2026-10-27.json)
ran the three-layer 200-pixel spectrum with USB/HTTP management, repeated off/on
and one Sense restart. Main/worker peaks were 5,308/740 bytes, with a 32,356-byte
VM peak under the unchanged 64,400-byte limit. It completed in 14.74 seconds and
does not replace broad stress qualification. The installed image uses the Pico's
existing 1 MiB extent-store reservation; the initial 512 KiB build correctly
refused to mount that store. A private full-flash backup verified both original
catalogues and every payload CRC before the layout was corrected. No storage was
wiped; the final reboot restored selection, power, brightness, library and network.

The previous multicast measurements below are historical evidence only; they do
not qualify the new subscription or gateway implementation.

## Audio receiver audit — 26 October

The 200-pixel Pico W receiver build adds `audio_received` to `udp_input`'s target
set and enables `igmp_tmr` when either HTTP discovery or audio multicast is built.
The callback copies at most 692 bytes, validates/stages at most eight packets per
poll, frees each pbuf and neither publishes inputs nor invokes guest code.
Publication runs at the start of `app_tick`. The reviewed project fingerprints
now include `audio_receiver.c`, `inputs.c` and `outputs.c` for these paths.

Building the full WebSocket profile also exposed two missing audit contracts for
existing `websocket_tick`/`websocket_closed` callbacks. Pico registers host busy
and disconnect handlers and no WebSocket flush hook; those targets and the shared
WebSocket source identity are now recorded. WebSocket runtime code is unchanged.
After reviewing these registrations and the existing recursion guards, the
guarded project digests were updated while preserving the audited SDK file set.

The resulting main/worker bounds are 12,152/5,104 bytes, leaving 4,232/3,088 bytes
within the unchanged 16/8 KiB stacks. RAM reservations leave 21,628 bytes with
`LIGHTVM_PIXEL_CAPACITY=200`. Short functional checks observed a main peak of
5,852 bytes during configuration, uploads and reception; this is not a new stress
qualification. The current [subscription guide](../audio-subscriptions.md)
replaces that experimental receiver configuration.

The later sixteen-band input extension retains these same linked path bounds
and RAM reservations. The decoded frame and receiver each add 64 bytes of bounded
spectrum storage; the publisher registry expands from 16 to 32 slots under the
same uint32 publication mask and VM heap ceiling. The two associated headers are
now fingerprinted as well. No callback targets, recursion guards, renderer or
stack reservations changed; all 30 host CTests and the Pico gate pass again.

## Original accounting record

22 October 2026. The configured Pico W, Pico 2 W and no-network Pico Release profiles pass a
closed **software** accounting gate. This is conditional on the explicit external
ROM allowances below. It is not certification of every physical ROM revision or
a substitute for target stress/watermarks.

## Inputs and failure policy

`tools/audit_pico_stack.py` reads GCC `-fcallgraph-info=su` and `-fstack-usage`
evidence, then joins it to the linked ELF disassembly, symbol ranges, aliases,
wrappers and source identities. This includes compiler-inserted library calls,
tail branches, link veneers and SDK/assembly routines. It conservatively sums
stack reservations within an assembly body, including alternative branches.
Zero-sized assembly symbols, unnamed weak bodies and variadic Thumb return
sequences are handled explicitly; unknown register calls never become a zero
frame. Discarded weak bodies may remain in the conservative source graph.

The graph includes foreground boot/dispatch, USB/session and HTTP/WebSocket
commands, MQTT/HA message and state paths, rendering/worker startup, preparation,
reset and storage. It includes USB, deferred USB, alarm, GPIO/CYW43 and multicore
lockout interrupt paths. Callback target sets are recorded in
`tools/pico_stack_contracts.py`; broad alternatives deliberately overestimate
rather than infer a target from a particular test run.
The build's explicit Wi-Fi, HTTP and MQTT options filter only their known
registrations and roots. Missing symbols never imply that a feature is disabled;
an unknown callback or missing enabled target still fails the gate.

Both the normal post-link build and `pico_stack_audit` reject:

- Missing roots, missing frames, unresolved indirect calls, unbounded frames or
  unaccounted recursive edges.
- A changed audited SDK dependency or project recursion guard, checked against
  `tools/pico-stack-contracts.json` (the audited SDK and guarded project sources).
- A main path plus interrupt allowance that leaves less than 2 KiB, or a worker
  path that leaves less than 1 KiB, within the actual linked stack reservations.
  With the standard 16 KiB / 8 KiB reservations, these are 14 KiB / 7 KiB limits;
  changing `LIGHTVM_WORKER_STACK_BYTES` changes the checked worker limit.

`stack-paths.json` records the ELF/disassembly/callgraph hashes, callback targets,
longest paths, interrupt paths, external allowances, unresolved items and source
identity result. `software_accounting_complete` and `software_gate_passed` are
separate from `hardware_validated` and `certified`, which remain false.

## Context and assembly contracts

These are reviewed source properties, protected by fingerprints:

| Contract | Why the recursive-looking graph is bounded |
| --- | --- |
| Runtime checkpoint | Returns on a worker, inside deferred rendering/preparation/boot, or while already servicing. Reentrant command dispatch sends busy without executing another command. |
| Render jobs | A root job may run a VM slice; a nested native job cannot start another guest invocation. Further recursive native dispatch remains an error. |
| CYW43 send | A link-event callback can send while the SDK is processing flow control. The project wrapper rejects a nested `cyw43_send_ethernet` call; the caller receives an error/retry path. |
| Fatal panic | `lightvm_panic` disables interrupts and halts, preserving the format pointer for a debugger. It does not log recursively through a failing USB transport. |
| lwIP DHCP/ACD | Address-change handling invokes the conflict callback for DECLINE; it cannot choose the bind branch. The resulting ANY address/state-OFF path terminates another address-change callback. |
| Pico printf | `_etoa` normalizes before nested `_ftoa`; that nested call cannot select exponential formatting again. |
| RP2350 DCP | The unnamed saving entry pushes LR (4), saves state/scratch (32), and resumes after the guard with at most 8 arithmetic bytes. Each linked add/subtract/multiply/divide wrapper is conservatively charged 64 bytes for the complete operation. The restore helper only pops state. |
| RP2350 VFP sine/cosine | Heavy range reduction pushes 20 bytes once. Repeated `frr_core` calls use the same frame; that core is a leaf. The whole sine/cosine operation is charged 32 bytes. Re-entry after reduction bypasses the reduction guard. |

The DCP and trigonometric contracts replace misleading ordinary-call edges across
unnamed shared assembly entries. They are source-audited bounds, not guessed
recursion depths. Other linked library/assembly routines use their actual
reservations and call edges. New unknown assembly still fails closed.

The interrupt allowance is **two complete worst interrupt paths plus 256 bytes
per level** for hardware exception state, alignment, FPU/DCP and dispatch. The
configured ordinary IRQs share a priority; deferred USB is lower priority. Two
levels conservatively cover their nesting. An IRQ-priority, new handler, RTOS,
USB class, networking mode or callback-registration change requires re-audit.

External boundaries have explicit conservative engineering allowances:

| Boundary | Bytes |
| --- | ---: |
| ROM table lookup | 64 |
| ROM flash operation | 512 |
| Boot2 XIP restart | 256 |
| RP2040 ROM float helper | 256 |
| ROM memory/bit helper | 128 |
| ROM information/reset helper | 512 |

These are conservative allowances, not runtime stack measurements. The attached
RP2040 B2 ROM was checked against them on 22 October 2026 as described below;
the RP2350 allowances still need that board's physical follow-up.
The primary ROM sources are [RP2040 boot ROM](https://github.com/raspberrypi/pico-bootrom-rp2040)
and [RP2350 boot ROM](https://github.com/raspberrypi/pico-bootrom-rp2350).
Identify the actual silicon/ROM version and verify the matching ROM implementation
or stress margin before closing physical acceptance. The SDK-supplied boot2 and
ROM dispatch glue are also present in the linked evidence.

### Attached RP2040 B2 ROM audit

The Pico W with serial `E6614103E7236D37` reports ROM version 3. Its captured
16 KiB ROM SHA256 is
`ef07d54bdd259c7858483606118a3c704976801823c38ac5a2fcdffacd2d83fe`.
`tools/audit_rp2040_rom.py` checks every disassembled instruction's bytes against
that dump and follows direct calls and branch alternatives while tracking stack
depth. Unknown indirect calls, recursive calls and unbounded stack growth fail
closed. The B2 double high-register save/restore helpers, multiply LR scratch use
and byte-tail memory dispatch have explicit contracts guarded by the exact dump hash, checked against
Raspberry Pi's B2 source at `ef22cd8ede5bc007f81d7f2416b48db90f313434`.

| Firmware boundary | Largest audited ROM path | Existing allowance |
| --- | ---: | ---: |
| Table lookup | 0 bytes | 64 bytes |
| Flash | 88 bytes | 512 bytes |
| Float/double | 68 bytes | 256 bytes |
| Memory/bit helpers | 12 bytes | 128 bytes |
| USB boot reset | 16 bytes | 512 bytes |

80 of 83 exported entries close. Debug trampolines `DT`/`DE` and vector transfer
`WV` remain explicitly unresolved; they are not firmware helper calls covered by
these allowances. This audit excludes interrupt nesting and does not replace the
linked software graph, the separate boot2 allowance or stress watermarks. Seven
regression tests cover nested calls, alternative paths, unknown calls, stack
growth, stack-register writes, data directives and mismatched dump/disassembly.

## Build evidence

The regression-repair paths and numbers live in the
[repeat comparison evidence](../benchmarks/pico-performance-repair-2026-10-22.json).
The [initial implementation evidence](../benchmarks/pico-performance-implementation-2026-10-21.json)
and first hardware campaign remain historical records.
Both profiles retain the 16/8 KiB reservations and the 16 KiB native operating
reserve; positive linker headroom is separate from runtime stack headroom.
The HA `message`, `light_command` and `ha_tick` frames are 184, 24 and 656 bytes
respectively. Full path checking now supplements those regression sentinels.

Run against saved build directories:

```sh
cmake --build build/multicore-pico-w --target pico_stack_audit
cmake --build build/multicore-pico2-w --target pico_stack_audit
```

After an intentional SDK or guarded-source change, review callback registrations,
recursion conditions, IRQ priorities, assembly and frame reports **before**
explicitly refreshing the fingerprints:

```sh
python tools/pico_stack_fingerprints.py --record build/deps/pico-sdk-2.3.1 \
  build/multicore-pico-w build/multicore-pico2-w build/review-pico-minimal
```

Recording fingerprints does not prove the contracts. The build never does this
automatically. Modified firmware feature profiles need the same audit, and an
unknown/missing path remains a build failure. Unit tests cover unknown/dynamic
frames, recursion, source edits, Thumb returns/veneers and whole-path overflow
including interrupts, changed linker reservations and optional feature targets.
Native allocation/cancellation and integration tests cover
the corresponding project lifetime guards.

In the first hardware campaign, the attached Pico W completed 62.55 minutes on
one boot with USB, HTTP,
authenticated MQTT, program writes/deletes, scene replacements and control stress.
Observed main/worker peaks were **5,740 / 2,684 bytes**, leaving **10,644 / 5,508
bytes** in the standard reservations. Linked bounds including interrupts remain
12,152 / 5,300 bytes. The corrected output-ownership path passes all three build
gates; the relevant guarded-source fingerprint was refreshed after reviewing its
nested-service ownership. See the [hardware report](pico-performance-hardware-2026-10-22.md)
for raw evidence and the independent performance/admission failures.

Physical follow-up still includes Pico 2 W, additional output types and the full
multi-output stress matrix. Record both core watermarks and ROM
identity, keep the required margins, and investigate any disagreement with this
conditional accounting rather than enlarging reservations without evidence.

### Regression-repair audit — 22 October

Pico USB replies now assemble one protocol frame (at most 528 bytes) before
calling the SDK writer. The reviewed indirect `stdio_usb.out_chars` target moves
from `output_bytes` to `output_message`; the callback map and main-source digest
track that exact call site. The temporary reply remains owned by the synchronous
call. Bounded bulk CDC input is called by `service_commands`, whose indirect
`stdio_usb.in_chars` target is also registered. Main invokes this service before
and after engine work; it is never entered by a recursive runtime checkpoint.
Rendering and preparation use separate foreground turns. Progress replies
stream through one 512-byte buffer, and common status formatting avoids printf.
The frozen UF2 SHA-256 is
`ace68ff719e5f098812b941c0e47123142b25e72303b9ac744aa6deb25e52c4f`.
All three strict build profiles pass with no unresolved software paths:

| Profile | Main with interrupts | Main margin | Worker with interrupts | Worker margin |
| --- | ---: | ---: | ---: | ---: |
| Pico W | 12,048 B | 4,336 B | 5,384 B | 2,808 B |
| Pico 2 W | 12,168 B | 4,216 B | 5,156 B | 3,036 B |
| No-network Pico | 12,044 B | 4,340 B | 5,372 B | 2,820 B |

The 16/8 KiB stack reservations and required 2/1 KiB margins are unchanged.
The Pico W report covers 225 objects and 1,968 frames; its HA sentinel frames
remain 184/24/656 bytes. Static RAM, including SRAM drawing helpers and enlarged
CDC FIFOs, is 132,976 bytes. After the unchanged VM, stack, driver and 16 KiB
operating reservations, 400 bytes remain. This linker margin does not replace
runtime heap and stack measurements. The 61.55-minute repeat campaign records
**5,248/16,384 bytes main** and **2,676/8,192 bytes worker**, leaving 11,136/5,516
bytes, across 234 same-boot controller descriptions. VM heap peak is
61,708/65,536 bytes; both HA stress blocks retain a constant 29,000-byte live
heap. The actual B2 ROM identity and audited helper bounds above remain
applicable to the same physical device. The [repair report](pico-performance-repair-2026-10-22.md)
links the exact ELF, linked paths, firmware readback and stress evidence.


## Native board-status measurements

The board-status addition uses direct calls from `app_init`/`app_tick` on core 0.
It adds no callbacks, IRQ handlers, recursion, worker jobs or assembly. ADC
initialization/reset is the SDK's existing blocking initialization contract;
each subsequent conversion polls the READY/ERR registers with a 100 µs deadline.
Eight conversions are bounded by the fixed loop. Temperature power is disabled
after each read. VSYS is sampled only on the plain Pico. Wireless Pico VSYS
and RSSI are not exposed: both require synchronous CYW43 firmware ioctls with
up to a one-second wait, which is incompatible with the foreground latency
budget. Board telemetry never enters the radio driver or holds its lock.
Allocator status uses `mallinfo` and `sbrk(0)` without changing the break.

Reviewed the new ADC C/header and RP2040/RP2350 register/struct dependencies,
then added their identities to the existing SDK manifest union. The guarded
project set now includes the two board-status implementations. The Pico,
Pico W and Pico 2 W software RAM/stack gates pass; no physical sensor or stack
watermark validation is implied by those software gates.

### ESP32 campaign integration — 24 October

The shared `input_list(name)` filter adds a bounded 33-byte local name buffer and
direct string checks; `describe.reset_reason` calls a direct weak platform hook
(returning null on Pico). The runtime hook is a leaf. These changes add no
callbacks, recursive dispatch, IRQs or worker activity, and do not alter the
runtime checkpoint guards. The two project fingerprints were refreshed after
that source review; stack reservations, callback contracts and margins stay
unchanged. Use a fresh build directory after the device-core source move: stale
`.ci` files from deleted root sources otherwise remain in the audit inventory.
Current cross-build results are recorded in the ESP32 campaign report.

## Offline audio dependency verification, 27 October 2026

The pinned audio dependency now materializes from a checked source archive and
per-file SHA-256 manifest, without private-repository CI credentials. The reviewed
`cmake/kaj_audio.cmake` change replaces Git HEAD inspection with source-byte
verification. It changes no runtime code, callbacks, recursion guards or reserves.
Only that CMake fingerprint changed; all 543 SDK, 38 project and eight audio-core
entries remain covered. Rebuilding the complete Pico image from the snapshot
produced the identical UF2 SHA-256
`4a41c7a36168ea84010dc9a033f39685dd771a55bbf47eb7a9de0e8007956099`
as the installed physical-check image. RAM/stack software gates pass unchanged.
