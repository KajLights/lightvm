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
instruction. The native backend executes the existing typed expressions in
chunks of at most 32 pixels, caches stable scalar inputs and evaluates invariant
operations once per chunk. It does not recognize rainbow syntax or choose an
effect's colours or timing.

The normal loop remains the fallback. `fillfn` and first-class callback values
are not required by this implementation.

## Supported loops

The initial optimizer accepts `for i in range(stop)` with one final assignment
to `image[i]`, where `image` is a writable output or PixelArray. Intermediate
local calculations and straight-line inlined helpers are supported. The body
must lower to at most 32 operations and cannot carry a value from one iteration
to the next. Its output is an opaque RGB replacement, matching indexed writes:
alpha becomes 255 and white becomes zero where those channels exist.

Supported calculations include integer and float arithmetic, conversions,
comparisons, min/max, RGB/HSV construction, `hash01`, fixed resource lengths,
`math.sin`, RGB channel extraction and scalar `colorwheel`. These are generic
operations; for example a nonlinear HSV expression takes the same path as the
affine colourwheel expression in Rainbow.

Loops that mutate context or sprites, use randomness, read pixel data, branch,
break/continue, nest another loop, or use unsupported operations retain ordinary
bytecode. This preserves ordered effects and pixel feedback. Further compiler
work can broaden the proven subset without changing the source API.

## Execution contract

The backend caches and evaluates the typed body; it neither JIT-compiles machine
code nor invokes the normal VM separately for each pixel. Each dispatch executes
at most 32 pixels times 32 body operations. The scheduler can yield between
chunks, with time and scalar inputs stable throughout the frame. No rendering
path allocates memory. Fixed C-stack scratch is approximately 1 KiB; it does not
add per-instance heap storage.

Floating operations keep their original f32 order and rounding. Division is not
rewritten as multiplication by a reciprocal, and multiply/add is not contracted
into an FMA. Loop locals and the final index retain scalar-loop values. Empty and
negative ranges do not evaluate the body, including invariant expressions that
would fault. Faults retain the last completed contribution as usual.

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
compilation checks the actual drawing requirement and rejects insufficient targets.

New programs use schema 11. Optimized maps require drawing API 4, retaining LBC1/4 and execution
ABI 4. Opcode 39 contains a destination reference, index register, exclusive stop
register and the PC after its embedded body. Header drawing requirements 0, 2, 3 and 4 describe feature use
within the single current package contract; they are not older package versions. `native_draw_us` includes map execution in addition to native
drawing bindings; `dispatch_count` counts the outer VM dispatches.

## Performance

Compare generic drawing kernels, ordinary loops and bulk maps on identical output
counts and timestamps with:

```sh
python tools/benchmark_pixel_maps.py --process build/composition-host/lightvm \
    --pixels 200 --frames 300 --rounds 7 --output build/pixel-map-results.json
```
