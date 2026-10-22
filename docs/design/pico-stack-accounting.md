# Pico stack-path accounting

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
  `tools/pico-stack-contracts.json` (537 SDK files and ten project sources).
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

These bytes are **assumptions**, not measured stack maxima of an identified board's
ROM. The primary ROM sources are [RP2040 boot ROM](https://github.com/raspberrypi/pico-bootrom-rp2040)
and [RP2350 boot ROM](https://github.com/raspberrypi/pico-bootrom-rp2350).
Identify the actual silicon/ROM version and verify the matching ROM implementation
or stress margin before closing physical acceptance. The SDK-supplied boot2 and
ROM dispatch glue are also present in the linked evidence.

## Build evidence

The final exact paths and numbers live in the
[implementation evidence](../benchmarks/pico-performance-implementation-2026-10-21.json).
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

Physical follow-up is the plan's alternating-scene/control stress, flash/network
load and endurance matrix on both boards. Record both core watermarks and ROM
identity, keep the required margins, and investigate any disagreement with this
conditional accounting rather than enlarging reservations without evidence.
