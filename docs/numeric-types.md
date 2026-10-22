# Numeric foundation contract

22 October 2026. **S1 scalar runtime contract, application feature floor 1.2.0.**
The compiler, loader and shared firmware core support explicit fixed values and
arrays alongside the existing types. This implements the first runtime stage of the
[core SDK plan](design/core-numeric-kernels-plan.md).

## Scope and provenance

LightVM needs explicit fractional arithmetic, predictable four-byte storage and
shared scalar/kernel semantics. Picolights' Pattern, colour helpers, Flame and
Candle supply workload requirements for indexing, interpolation, wrapping and
math; they use ordinary floats/integers and do not supply this fixed-point
contract. The [workload specification](design/numeric-workloads.md) records those
references. The numeric implementation and fixtures are independently written.

The [operation catalogue](design/numeric-operations.json) records signatures,
units, fault conditions, accuracy and intended kernel eligibility. Its names are
semantic identifiers, not opcode numbers or source-language execution modes.
S1 encodings and their 1.2.0 feature floor are assigned. `wire_id: null` and an
unreleased feature floor still mean **do not emit this operation as a new bytecode
feature**: the expanded f32 math contract awaits S4. Existing operations such as
sine retain their current bindings.

## Values and conversions

Q16.16 is a distinct signed value type with `value = raw / 65536`. Raw storage is
32 bits; the representable interval is `[-32768, 32768 - 1/65536]`. All bit patterns
are valid. Zero has one representation. The bytecode type tag is 7 and the
four-byte array resource kind is 20. A raw integer never becomes a resource handle.

Python construction is explicit: `fixed(...)`, `FixedArray(count, fill=...)`,
`fixed_bits(...)` and `fixed_from_bits(...)`. `fixed_sqrt(...)` returns fixed;
`int(...)` and `float(...)` explicitly convert back to existing numeric types.
These SDK declarations describe compiled operations, not a separate Python
numeric runtime. Full JavaScript/Blocks authoring parity remains S5 work.
Existing literals keep their types; mixed arithmetic requires a conversion.
Counts and indices remain i32.
Settings and input wire types remain unchanged: convert captured values once at
the appropriate initialization/frame boundary. Fixed-valued wire declarations
must be rejected until separately implemented.

| Conversion | Exact rule |
| --- | --- |
| Literal to fixed | Parse the numeric source token exactly, multiply by 65536, round nearest with ties to even, then check signed raw range |
| i32 to fixed | Multiply by 65536 exactly, then check range |
| f32 to fixed | Interpret the finite binary32 value exactly; multiply by 65536, round nearest with ties to even, then check range |
| Fixed to i32 | Truncate the value toward zero |
| Fixed to f32 | Round to nearest binary32 with ties to even; raw zero becomes positive zero |
| Raw bits | Preserve all 32 bits, with no scaling or rounding |

Literal conversion must retain the token spelling rather than use an already
rounded host float AST value. For example, `0.00000762939453125` is exactly half
a raw unit and rounds to zero; appending a nonzero decimal digit beyond host
float precision can make it round to one. This applies to numeric literal
construction, not arbitrary constant expressions: expressions must retain each
operation's declared intermediate rounding and overflow.

Constant folding applies the same rules. A faulting arithmetic expression stays
in bytecode: an untaken branch does not fault, and a taken fault cannot move ahead
of earlier observable writes. An out-of-range fixed literal is a source error.

Both f32 zero signs and all f32 subnormals convert to raw zero. Nonfinite f32
inputs fault. Check range **after** rounding: a value just below the lower bound
can round to the minimum, while the upper boundary tie rounds out of range.
Converting maximum fixed to f32 yields `32768.0`; converting that f32 back faults.
A fixed/f32 round trip is not generally an identity.

## Authoring and arrays

```python
from lightvm import profile, init, render, fixed, FixedArray, rgb
profile("animation")

@init
def setup(ctx, runtime):
    ctx.levels = FixedArray(runtime.pixel_count, fill=fixed(0.25))
    ctx.step = fixed(0.125)

@render
def draw(ctx, runtime, pixels):
    for i in range(pixels.count):
        ctx.levels[i] = (ctx.levels[i] + ctx.step) % fixed(1)
        pixels[i] = rgb(int(ctx.levels[i] * fixed(255)), 0, 0)
```

Declare arrays directly on `ctx` in unconditional initialization. Count and fill
are admission expressions: literals, `runtime.pixel_count`, earlier settings and
supported pure arithmetic; they cannot depend on arbitrary mutable init state.
Counts must resolve to 0..65535, subject to admitted memory. The default fill is
fixed zero. Each element costs four payload bytes; preparation charges allocation
and initializes resources incrementally before activation.

Indexed reads/writes check bounds. `array.fill(value, start=0, stop=len(array))`
and `array.copy_from(other)` require matching fixed types; copy also requires
equal lengths. Fill/copy use the existing resumable chunks of at most 32 elements.
Array identity, permissions and generation checks apply across yields. Rendering
does not allocate.

S1 fixed expressions and new bitwise operations execute as ordinary scalar
bytecode. Compiler recognition and the runtime verifier keep them out of map
kernels until S2 supplies the corresponding verifier and conformance coverage.

## Arithmetic and bit operations

| Operation | Exact rule |
| --- | --- |
| Add/subtract/negate | Wrap raw results modulo 2^32; negating minimum yields minimum |
| Multiply | Calculate the signed wide product, divide by 65536 toward zero, then wrap to 32 bits |
| Divide | Calculate `(a_raw * 65536) / b_raw` toward zero; fault for zero divisor or a quotient outside signed 32-bit range |
| Modulo | Floored remainder with the divisor's sign; zero divisor faults |
| Comparison/min/max | Compare signed raw values; comparisons produce bool |
| Fixed square root | `floor(sqrt(raw * 65536))`; negative input faults |
| i32 AND/OR/XOR/NOT | Operate on all 32 bits and interpret the result as signed i32 |
| i32 left shift | Discard bits beyond 32 bits; count must be 0..31 |
| i32 right shift | Sign-extend; count must be 0..31 |

These rules deliberately distinguish division from modulo. `-3.5 mod 3` is
`2.5`; modulo minimum by negative one fixed unit succeeds with zero even though
the corresponding division overflows. A modulo implementation must not compute
its answer through a checked fixed division. Negative multiplication truncates,
so raw `-1 * 32768` produces zero, not raw negative one.

The existing i32 division floors, whereas the new fixed division truncates its
scaled quotient toward zero. The shared aspect is checked exceptional division
overflow, not identical rounding. Do not change existing i32 division while
adding fixed-point. Fixed values have no implicit saturation and no implicit
bitwise interpretation; explicit raw-bit conversion bridges to i32 operations.

## Native f32 math contract for S4

Required coverage is `abs`, `floor`, `ceil`, `trunc`, `sin`, `cos`, `sqrt`, `log`,
`exp`, `pow`, `min` and `max`. All take finite binary32 values and return finite
binary32 values. Round-to-integral functions return f32 at this layer; frontends
explicitly convert where their source API returns integers. Angles use radians;
`log` and `exp` use the natural base.

Negative square root, nonpositive logarithm, zero to a negative power and negative
bases to nonintegral exponents fault. Exponent integrality is tested on the exact
binary32 exponent. `pow(0, 0)` is one, but nonfinite operands are rejected before
any identity shortcut. A nonfinite computed result faults. Gradual underflow and
finite zero are allowed; silently flushing representable subnormals is not part
of this contract.

Signed zeros are explicit: abs clears the sign; sqrt, sin, floor, ceil and trunc
preserve it where applicable. Min of opposite zero signs is negative zero; max
is positive zero. A negative-zero base raised to a positive odd integral exponent
produces negative zero, otherwise positive zero. All zero exponents produce one.
The [native vectors](../tests/fixtures/numeric-vectors.json) pin these identities
and fault cases. They specify future behaviour; S1 does not implement the expanded
native f32 set or establish its conformance.

Scalar and bulk paths must use the same implementation and match exactly on one
build. For S4, generate independent high-precision reference results from exact
binary32 inputs, increasing precision until rounding is stable. Include every
exponent band, signed zeros, subnormals, domain boundaries and adjacent values,
range-reduction extremes and the locally motivated workload ranges. Freeze and
hash that expanded corpus before comparing implementations. A host libm call is
not the independent reference for another libm call.

Initial targets: abs/floor/ceil/trunc/min/max are exact; sqrt is correctly rounded;
sin/cos/log/exp are within four binary32 ULPs of the rounded reference and pow
within eight. Measure ULP distance using ordered finite binary32 encodings,
treating the two zeros as one numeric value; check zero signs separately.
Record maximum absolute error too. Exact domain/fault and signed-zero cases
override tolerance. These are acceptance targets for the reference corpus, not
a measured or universal accuracy claim. S4 must publish errors for each target
math implementation before enabling it; a failing target needs a documented
contract review rather than an unreported approximation.

Clamp, interpolation, periodic wrapping and easing remain SDK equations. Fixed
transcendental approximations, noise and indexed sampling remain separate scope.
For now fixed programs explicitly convert to f32 for transcendental work. Do not
accumulate a long-lived elapsed clock in Q16.16.

## Implementation and verification

[numeric.h](../src/numeric.h) exposes signed raw values as `LvmQ16`; the loader
independently verifies the distinct bytecode type. Checked functions return false and
leave the non-null output pointer unchanged on failure. No operation allocates,
uses global state or changes the floating environment. Implementations avoid
signed overflow, signed shifts and out-of-range unsigned-to-signed conversion.

Float conversion operates on IEEE binary32 bits using integer arithmetic; float
wrappers only copy those bits. Fixed square root takes exactly 24 restoring
steps. Other loops are bounded by the 32-bit representation. Wide division may
use a compiler support routine on embedded targets. Pico builds include the
resulting calls in the whole-path stack audit; physical latency remains S6 work.

[The oracle](../tests/numeric_oracle.py) uses rational arithmetic, arbitrary
integers, binary32 packing and integer square root independently of the C code.
Reviewed vectors test both implementations, then randomized and boundary suites
compare raw values and faults. Standalone tests build a temporary executable.
Runtime tests replay the same oracle through independent hand-encoded packages,
verified scalar invocation and Python emission with folding enabled/disabled.

```sh
python3 -B -m unittest discover -s tests -p test_numeric.py -v
LIGHTVM_NUMERIC_CFLAGS='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' \
  python3 -B -m unittest discover -s tests -p test_numeric.py -v
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
  python3 -B -m unittest discover -s tests -p test_numeric_runtime.py -v
```

`CC` selects a C11 compiler. Sanitizer availability depends on that host compiler.
The [S1 validation record](design/numeric-s1-validation-2026-10-22.md) records
native/compiler regressions, ASan/UBSan and Pico build, SRAM and stack results.
Coverage includes 93 reviewed raw operation vectors, 15 exact literal cases,
seeded raw operands, conversion boundaries and four host rounding modes, plus
resource admission, malformed packages, cancellation and allocation failures.
These results establish scalar correctness and build bounds, not firmware
throughput. Scalar/bulk agreement, complete frontend parity, expanded native f32
math and physical measurements belong to the later stages. The 43 future native
math vectors remain catalogue requirements awaiting S4.
