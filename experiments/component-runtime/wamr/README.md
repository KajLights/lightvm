# WAMR AOT component experiment

This is an executable host experiment using **WAMR 2.4.5 AOT**, compiled from
revision `25bd7eb63e828e4bd242cc9b38d260b4b31c6605`. The runtime is built with the
interpreter and both JITs disabled. Guest C is compiled to WASM, then `wamrc`
produces a target-specific AOT package. The host checks the package type before
loading it. This is not a WASM interpreter benchmark or production firmware.

From the repository root:

```sh
python3 experiments/component-runtime/wamr/run.py --bootstrap --output build/component-runtime/wamr-result.json
python3 -m unittest discover -s experiments/component-runtime/wamr -p 'test_*.py' -v
python3 experiments/component-runtime/wamr/run.py --sanitize --output build/component-runtime/wamr-sanitized.json
```

Omit `--bootstrap` after the first run. It downloads checksummed WAMR source and
official `wamrc` release archives beneath `build/component-runtime/deps/`, and
installs CMake there if unavailable. No global packages are installed. Python
3.12+, a C compiler, a Clang with a WASM backend, and `wasm-ld` are needed.
`CMAKE`, `WASM_CLANG`, `WASM_LD`, `WAMRC`, and `CC` can select existing tools.
The runner discovers Homebrew LLVM and Rust's bundled `wasm-ld` on this machine.

Bootstrap supports macOS and Linux x86-64. On Apple Silicon, the official x86-64
`wamrc` runs through existing Rosetta and cross-compiles **AArch64 AOT**. Its zstd
dependency is built locally for x86-64. The test host and executing AOT code are
native ARM64; only the off-device compiler is translated. Rosetta is not installed
by this script. Other hosts can supply a matching `wamrc 2.4.5` via `WAMRC`.
Missing tools produce `status: unavailable` and a nonzero exit; build/test failures
produce `status: failed`. Full command output stays in `build/component-runtime/wamr-commands.log`.

## What executes

`guest.c` is a model of compiler output, not a Python/JS/blocks/Pixelblaze frontend.
It contains the common 300-pixel unsigned arithmetic workload and explicit
continuation state (`cursor` and PRNG state). Each call processes at most 64
pixels; the host requests 32. Ten scheduling turns finish a frame without
restarting it. A second instance has independent state and storage. A separate
replacement artifact changes the seed-initialization algorithm using a
compile-time XOR, so activation verifies changed executable behavior.

The native host obtains a direct pointer to the guest RGB array using
`wasm_runtime_addr_app_to_native`. A bulk XOR transform touches that same memory
without guest/native crossings per pixel. The simulated output consumer retains
the pointer and its owning instance for 12 scheduler turns. The other effect,
replacement and bounded socket service progress while output is outstanding.
The host rejects stale/generation-mismatched native views, and the generated
guest API rejects writes to sealed storage. Cancellation also preserves a pending
output until completion. **There are zero pixel-data copies in this scenario.**
Each instance has one RGB buffer, so that instance waits while its own output is
retained; other instances continue. Buffer rotation within one instance is not
implemented in this prototype.

Preparation is staged: synchronous AOT loading, synchronous instance construction,
then 64-byte initialization quanta interleaved with the old effects and one byte
of actual nonblocking POSIX `socketpair` traffic per turn. Activation is a pointer
swap and retirement of the old instance. The old instance is deallocated only
after the simulated output completion. This is not DMA, Wi-Fi, USB or MQTT testing.

The wrapper generates and validates an exact versioned manifest profile, target,
capabilities/resource constants and artifact SHA-256 before native loading.
This prevents accidental ABI/resource mismatches and stale artifact selection.
It is a local test package, not a signing or hostile-upload system. Host policy
enforces a three-instance cap and an 8 MiB tracked runtime-payload cap.

## Adversarial tests and scope

The integrated native executable fails immediately on any assertion failure. It
checks independent oracle hashes, continuation progress, initialization ordering,
zero and oversized quanta, bulk-operation ranges, integer-overflow offsets, stale
views, WAMR's actual out-of-linear-memory trap, allocation failure, component-count
admission, corrupt and truncated AOT, rejected replacement preserving live state,
an endless logical loop broken into finite quanta, cancellation, outstanding
output through replacement, changed replacement code and zero tracked allocations
after cleanup. Python tests additionally reject malformed manifest profiles,
unknown capabilities, wrong target/ABI, stale hashes and WASM passed as AOT.
The stale-view test also reuses the exact host component slot to check that a
previous generation cannot regain access after destruction and reconstruction.

An explicit bypass test deliberately performs an **in-range raw guest store into
a published buffer**, observes that it succeeds, then restores the byte. WAMR's
linear-memory isolation does not revoke writes to individual buffer objects.
The successful ownership tests apply to the trusted generated guest and host
schedule. Arbitrary guest code could bypass `sealed`, reset state via `configure`,
or loop indefinitely without compiler-inserted yields. Loading arbitrary AOT is
not a safe sandbox boundary. The prototype does not claim otherwise.

Only the fixture's finite generated quanta are resumable. Long native calls,
synchronous load/instantiate and uninstrumented loops are not preempted. Timings
are measured host observations, not deadline guarantees. State migration,
recursive continuations, MCU execution, multicore use, flash writes, physical
buffer alignment/cache/DMA requirements and browser toolchain size remain open.

## Memory accounting

The runtime enables `WAMR_BUILD_ALLOC_WITH_USAGE`, which routes linear-memory
allocation through the instrumented allocator as well as runtime metadata and
execution environments. Without this option, the host's linear-memory `mmap`
would bypass an ordinary WAMR malloc counter and substantially understate memory.

`peak_tracked_runtime_heap_bytes` counts requested allocator payload, including
linear memory. It excludes the counter's aligned allocation headers, the system
allocator's internal overhead, mapped executable code, artifact staging, native
stack, sockets and process/SDK allocations. Those categories are separately
reported where known and are explicitly `null` when unmeasured. Header bytes are
an upper bound because their peak need not coincide with payload peak.

With this fixture, WAMR shrinks the declared 64 KiB memory to 6,064 logical bytes,
then rounds the native allocation to the host's page size. On the tested ARM64
macOS host that is 16,384 bytes. An instance initially needs about 20,032 tracked
payload bytes; retaining a 900-byte output pins that **whole instance**. Three
instances plus two loaded module metadata sets peak around 62 KiB of tracked
payload. These are host observations, not MCU memory estimates. The backing
storage is WAMR-owned native memory; arbitrary externally allocated peripheral
buffers have not been mapped into the guest.

`memory_accounting` distinguishes logical linear bytes, rounded allocations,
artifact staging, host component structs, and excluded mappings/stacks. The
report records hashes/sizes of both AOT artifacts and the host executable. AOT
file size is not executable mapping size or MCU flash footprint.

## Dependency provenance and licenses

- [WAMR revision](https://github.com/wasm-micro-runtime/wasm-micro-runtime/tree/25bd7eb63e828e4bd242cc9b38d260b4b31c6605): Apache-2.0 WITH LLVM-exception. Full `LICENSE` is preserved in the downloaded source.
- [Official wamrc 2.4.5 releases](https://github.com/wasm-micro-runtime/wasm-micro-runtime/releases/tag/WAMR-2.4.5): WAMR and LLVM Apache-2.0 WITH LLVM-exception. Release archive SHA-256 values are pinned in `run.py`.
- [zstd 1.5.7](https://github.com/facebook/zstd/tree/v1.5.7): BSD-3-Clause (also offered under GPL-2.0); the source download retains `LICENSE` and `COPYING`.
- CMake 3.31.6: BSD-3-Clause; optional local Python distribution retains its notices.
- Existing LLVM Clang and Rust-bundled LLD: Apache-2.0 WITH LLVM-exception. Compiler versions and artifact hashes are included in each report.

Generated binaries, dependencies, manifests and results stay under ignored
`build/component-runtime/`. No production source, runtime defaults or devices are
changed by this experiment.
