# Compiler-generated bulk pixel maps

Write pixel colours with ordinary language expressions and functions:

```python
def rainbow(index, count, phase):
    return hsv(index / count + phase, 1.0, 1.0)

for i in range(pixels.count):
    pixels[i] = rainbow(i, pixels.count, phase)
```

The compiler types and inlines the code, then recognizes independent pixel
maps. It replaces eligible loop control with `PIXEL_MAP`, a generic bulk
instruction. The native backend executes typed expressions in weighted chunks,
captures stable scalar inputs and caches straight-line invariants after their
first ordered evaluation in each lane. It does not recognize rainbow syntax or choose an
effect's colours or timing.

Unsupported loops keep ordinary resumable bytecode. `fillfn` and first-class
callback values are not required. A compiled map must admit its prepared plan;
there is no hidden execution mode if its memory allocation fails.

## Supported loops

The optimizer accepts `for i in range(stop)` with one final assignment
to `image[i]`, where `image` is a writable output or PixelArray. Intermediate
local calculations, forward conditionals and inlined helpers are supported. The body
must lower to at most 128 operations and a worst first-pixel path of at most
1,024 work units, and cannot carry a value from one iteration
to the next. Its output is an opaque RGB replacement, matching indexed writes:
alpha becomes 255 and white becomes zero where those channels exist.

Supported calculations include integer, float and explicit Q16.16 arithmetic,
bitwise operations, conversions, comparisons, min/max, RGB/HSV construction,
`hash01`, fixed resource lengths, resident numeric-array gathers, the
[native math set](numeric-types.md#native-f32-math), RGB channel extraction and scalar `colorwheel`. These are generic
operations; for example a nonlinear HSV expression takes the same path as the
affine colourwheel expression in Rainbow.

Every branch path must reach the common final store and define every body-written
local it reads. Branches remain lazy: an invalid index or math domain in the
untaken arm does not fault. Numeric sources must keep their identity and contents
through the loop, although earlier code may update them before entering it.

Loops that mutate context/sprites/source arrays, use stateful randomness, read
pixel data, break/continue, nest another loop, scatter, skip writes or make
multiple stores retain ordinary bytecode. This preserves ordered effects and
pixel feedback. New branch-local names still follow the frontend's scope rules;
initialize a local before an `if` statement when it is used after that statement.

## Execution contract

Preparation builds a compact immutable plan charged to the VM heap and reported
as `map_plan_bytes`. The backend evaluates that plan without invoking the normal
VM separately for each pixel. It chooses a chunk from the longest weighted path,
with maxima of 128 split, 64 unsplit transient or 32 persistent pixels, and checks
time between pixels. Time and scalar inputs remain stable throughout the frame.
No rendering path allocates. Each lane has bounded private stack scratch; the
whole-path target stack audit includes the caller and native math implementations.
See [encoding and cost rules](bytecode-emitter.md#prepared-pixel-kernels).

Floating operations keep their original f32 order and rounding. Division is not
rewritten as multiplication by a reciprocal, and multiply/add is not contracted
into an FMA. Loop locals and the final index retain scalar-loop values. Empty and
negative ranges do not evaluate the body, including invariant expressions that
would fault. Faults retain the last completed contribution as usual. Persistent
images execute serially and retain the ordered writes preceding a fault.

The loader independently validates the map's destination permissions, index,
range, local writes, supported operations, native signatures and data flow. Maps
cannot write state or use an unchecked store index. Ordinary typed instruction
verification also applies to every embedded instruction.

## Compilation and target requirements

Optimization is enabled by default. Compare the same source without it using:

```sh
python -m lightvm.compiler examples/animations/rainbow.py -o build/rainbow.lbc
python -m lightvm.compiler examples/animations/rainbow.py --no-optimize -o build/rainbow-scalar.lbc
```

Python callers can pass `compile_source(source, optimize=False)`. Target-aware
compilation checks the minimum application release and rejects insufficient targets.

Simple maps are part of the LightVM 1.0.0 baseline. Gathers, fixed/bitwise map
expressions, conditional branches, bodies above 32 instructions and new math
require 1.3.0. They use the same minimum-release checks as other instructions;
the loader independently rejects falsely lowered requirements. Opcode 39 contains a destination reference, index
register, exclusive stop register and the PC after its embedded body. Header words
80–92 remain reserved zero; there is no separate drawing API version. `native_draw_us` includes map execution in addition to native
drawing bindings; `dispatch_count` counts the outer VM dispatches.

## Performance

Compare generic drawing kernels, ordinary loops and bulk maps on identical output
counts and timestamps with:

```sh
python tools/benchmark_pixel_maps.py --process build/composition-host/lightvm \
    --pixels 200 --frames 300 --rounds 7 --output build/pixel-map-results.json
```

`tools/benchmark_numeric.py` freezes and measures the broader gather, fixed,
branch and native-math fixtures with scalar/bulk replay. See the
[S2–S4 validation record](design/numeric-s234-validation-2026-10-22.md) for attached
RP2040 evidence and its limits; host timings do not establish board performance.

The plan records first-pixel and steady-pixel work separately. Chunk selection
includes the full first evaluation in each lane. A source loop over the work
limit stays ordered with a compiler advisory; an explicitly encoded over-budget
map is rejected. See the [work-accounting contract](bytecode-emitter.md#prepared-pixel-kernels)
for cache costs, watchdog reservations and speculative suffix charging.
