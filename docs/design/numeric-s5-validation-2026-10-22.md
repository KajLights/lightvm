# S5 frontend and independent-emitter integration

Historical build labels and feature floors in this record predate the
[0.1.0 milestone baseline](../versioning.md). They describe the recorded
experiments and do not define current compatibility requirements.

22 October 2026. S5 follows `69de323` on `codex/core-numeric-foundation`.
This completes the frontend/emitter integration gate in the
[core numeric plan](core-numeric-kernels-plan.md). The shared numeric design and
workloads remain grounded in LightVM's typed, bounded, allocation-free render
model and the local picolights requirements. No firmware C code, scheduler,
encoding, release floor, memory limit or stack reservation changes in S5.
The complete multi-board performance/release matrix remains S6 work.

## Implementation and contract

- JavaScript and Blocks expose explicit Q16.16 values, exact decimal tokens,
  checked conversions/raw bits, fixed square root, i32 bitwise operations and
  managed fixed arrays with indexing, length, fill and copy. Blocks also exposes
  the existing B/H/I numeric array kinds. Fixed wire settings/inputs remain
  unsupported; they receive a diagnostic instead of an implicit conversion.
- Safe JavaScript loops with explicit i32 counters and immutable bounds lower
  to the shared range/kernel path. Mutable bounds/counters and ordinary f32
  counters retain their existing loop semantics. Blocks' learning view uses the
  same i32 loop counters as direct Blocks compilation.
- Source conventions are explicit: JavaScript remainder keeps dividend sign;
  Python/Blocks modulo keeps divisor sign. The lowering handles minimum signed
  fixed/i32 values, tiny opposite-sign f32 remainders and signed zeros without a
  guest-language mode in the core. Existing core arithmetic/fault rules apply.
- Kernel advice names the rejected effect or control-flow condition and reports
  plan/scratch costs separately from resource memory. It makes no FPS prediction.
- One compiler-file manifest now drives both Vite packaging and browser-worker
  loading, including the previously omitted `numeric.py`. An isolated import
  test and a real production-worker check cover this dependency boundary.

The [wire documentation](../bytecode-emitter.md) now includes exact animation
entry bindings, resource references, declaration expressions and the base
instructions needed by the [independent emitter](../../tests/fixtures/independent_emitter.py).
That emitter imports only standard-library modules, uses explicit wire IDs and
constructs complete LBC1/4 packages without reading compiler internals or generated
packages. It produces scalar and conditional/gather bulk variants, plus eleven
invalid variants. Expected fixed state, arrays and pixel bytes are independent
of the compiler output.

## Host and browser checks

| Check | Result |
| --- | --- |
| Native CTest | 19/19 pass |
| ASan/UBSan CTest | 19/19 pass |
| Full Python/compiler/receiver discovery | 348 tests, two environment skips, no failures |
| Numeric Python suites with instrumented VM | 35/35 pass |
| Independent-emitter tests with instrumented VM | 2/2 pass |
| Full frontend suite with native execution | 166 pass, one audio-device environment skip |
| Numeric frontend suite with instrumented VM | 13/13 pass |
| Pico build-contract tests with CMake available | 20/20 pass |
| TypeScript and Vite production build | Pass |
| Production worker in Chrome 156.0.8078.47 / pinned Pyodide | Python, JavaScript and Blocks compile, execute and reject insufficient targets |

Host: arm64 macOS, Apple Clang 21, Python 3.12.13, Node 22.22.3. The Python skips
are the existing host Wi-Fi/power-recovery environment cases. ASan uses
`detect_leaks=0`; native fixtures separately assert zero retained VM allocation
after teardown and prohibit render allocations.

The frontend matrix replays representable cases from all 93 exact numeric
vectors, 15 decimal literal vectors and 43 native-math vectors. NaN/Infinity
input words are not source literals; the existing independent native invocation
tests cover those ABI cases. Assertions compare raw words and faults, including
zero signs, with optimization enabled/disabled. Array tests cover all six
numeric kinds, checked stores, fill/copy and forced yields.

Matched saved Python, JavaScript, Blocks and Blocks-learning-view sources execute
at 1, 33 and 200 pixels through scalar and bulk paths, with one worker and with
two workers plus forced time yields. They agree with the independent emitter's
state `0x00030000`, array fill `0x00018000` and exact RGBA pixels. The native driver
also checks prepared-plan selection. Existing examples and integration tests
remain in the full regression suites.

## Attached Pico

Pico W, RP2040 B2, serial `E6614103E7236D37`, 125 MHz, two render workers,
Pico SDK 2.3.1 / Arm GCC 13.2.rel1, 1 MiB application storage. The attached strip
has 200 WS2812 pixels on GP16. Temporary output configurations used a brightness
limit of 32. Snapshot comparisons validate buffer bytes, not optical colour.

The unchanged final 1.3.0 firmware SHA256 is
`f87a2e51ecd31f8cbaf2fa48667c32ae94faddb7b5c1f311e945abc5434bee6e`.
Rebuilding S5 produces this same UF2. The existing 1.1.0 repair firmware is
`ace68ff719e5f098812b941c0e47123142b25e72303b9ac744aa6deb25e52c4f`.
Both flashes were verified by picotool; no new firmware backup or storage wipe
was made. Final frontend package hashes match the files executed on hardware.

- **20 rendering cases pass:** scalar/bulk packages from four frontend forms
  and the independent emitter, each at 33 and 200 pixels. Six requested frames
  per case match every expected RGBA byte. Active heap remains constant within
  each case, ranging from 7,060 to 11,282 bytes across cases. Native plan sizes
  are zero for scalar, 152 bytes for the independent kernel and 184 bytes for
  the compiler kernel.
- **21 current-firmware rejection attempts pass:** eleven malformed/type/
  resource/permission/CFG/floor variants and ten oversized-resource candidates.
  Every rejection retains the active frame and generation, with constant heap.
- **Ten older-firmware rejections pass:** all new frontend/emitter packages are
  rejected by 1.1.0 before replacing a known paused Rainbow scene. Its loader
  reports `invalid portable declarations` for these new fixed-type packages.
  Current-firmware forged-floor checks and frontend target-version checks cover
  the explicit feature-floor rules separately.
- **Restoration verified:** all eight saved programs match their original
  SHA256s; both presets and current/saved configuration are unchanged. Firmware
  is back to 1.3.0 with Wi-Fi station and MQTT connected. Rainbow is selected at
  80% brightness with power off, matching the state observed at the start of S5.

The old-firmware harness explicitly creates and pauses its guard scene because
reboot applies the saved startup policy, not transient light state. Two initial
setup attempts were corrected before the successful rejection run; each restored
the final firmware. The final restoration check runs after all flashes.

The 120 sampled `describe` replies took 9.01–12.51 ms (median 9.75 ms), including
transport and serialization. These are functional responsiveness observations,
not a 10 ms service certificate. Across the complete upload/configuration/rejection
campaign, foreground gaps over 10 ms increased by 130 and ingress gaps by 119;
the lifetime maximum gaps already predated the campaign. This is not a matched
performance campaign and does not close S6's responsiveness gates.

Strict SRAM and complete-path stack audits still pass: static SRAM 132,736 B,
640 B remaining after all configured reservations; main path 12,000/16,384 B,
worker path 5,064/8,192 B. These are the unchanged S2–S4 software bounds. No
new RP2350/ESP32 physical result or broader performance improvement is claimed.

## Evidence and reproduction

The [summary](../benchmarks/numeric-s5-validation-2026-10-22.json),
[raw archive](../benchmarks/numeric-s5-validation-2026-10-22-raw.tar.gz) and
[SHA256 manifest](../benchmarks/numeric-s5-validation-2026-10-22-manifest.json)
retain source/package identities, raw device results, restoration checks,
browser results, regression logs and memory/stack audit output. Firmware images
remain in the existing local archives.

```sh
LIGHTVM_RECEIVER=build/host/lightvm \
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
LIGHTVM_FRONTEND_FIXTURES_OUT=/tmp/lightvm-s5-packages npm test
npm run build
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
  node tools/check_numeric_frontends.mjs
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
  python3 -m unittest discover -s tests -p test_independent_emitter.py -v
python3 tools/check_s5_hardware.py --port /dev/cu.usbmodem11401 \
  --packages /tmp/lightvm-s5-packages --firmware path/to/validated-1.3.0.uf2 \
  --result /tmp/s5-pico.json
```

Set `LIGHTVM_PYTHON` when the compiler environment is not `python3` and
`CHROME_EXECUTABLE` when using installed Chrome instead of Playwright Chromium.
The historical old-firmware check used `tools/check_s5_floor_hardware.py` (now
retired with the experimental per-feature floors) with the
same package directory and the 1.1.0 firmware identity. It expects an existing
`lights` output and saved `rainbow` program. Flashing/restoring firmware is a
separate explicit step; restore the user's transient light state after reboot.
