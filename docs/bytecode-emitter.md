# Independent bytecode emission

23 October 2026. Baseline: validated Pico repair `a9c8611`. This document records
the current container and the S1–S5 contract for the
[numeric foundation](numeric-types.md). The shared core adds numeric operations
under application feature floors 1.2.0 and 1.3.0. The container layout remains unchanged.

## Current container

Packages are little-endian and at most 32,768 bytes. The 96-byte header is the
four ASCII bytes `LBC1` followed by 23 unsigned 32-bit words. The layout marker
is a physical decoding check, not a separately negotiated application version.
The loader requires exact section lengths, reserved zeros, valid types and
permissions; a compatible manifest alone cannot authorize invalid bytecode.

| Byte offset | Header contents |
| --- | --- |
| 0 | `LBC1` |
| 4 | Layout marker 4 |
| 8, 12, 16 | Register, instruction, palette-entry counts |
| 20, 24, 28 | Function, resource, declaration-expression counts |
| 32, 36 | Native-call-site and setting counts |
| 40, 44 | Initialization and render function-table indices |
| 48..68 | Six retired lifecycle fields, all zero |
| 72 | Profile binding: 3 for animation, 4 for transition |
| 76 | Manifest byte count |
| 80..92 | Four reserved words, all zero |

Sections follow the header consecutively, with no implicit host alignment or
pointer fields:

| Section | Record encoding | Meaning |
| --- | --- | --- |
| Registers | `u8 type, u8 role, u16 zero, u32 initial_bits` | Fixed declared type and initialization |
| Instructions | `u8 op, u8 a, u8 b, u8 c, u32 d` | Operand meaning depends on the verified operation |
| Palette | `u32 rgb8` | Values must fit `0x00ffffff` |
| Functions | Six u32 words | First PC, exclusive end PC, permissions, result type, argument-start register, argument count |
| Resources | Six u32 words | Kind, length-expression, fill-expression, seed-expression, stream-expression, flags |
| Declaration expressions | `u8 op, u8 type, u16 zero, u32 a, u32 b, u32 value` | Acyclic initialization expressions, distinct from executable instructions |
| Native call sites | `u32 binding, u32 argc, u8 args[8]` | Typed register arguments; unused entries zero |
| Settings | Four u32 words | Register, default-expression, minimum-expression, maximum-expression; absent bounds use `0xffffffff` |
| Manifest | UTF-8 JSON | Exactly the declared byte length, at most 8,192 bytes |

Current limits are 1..128 registers, 1..2,048 instructions, up to 1,024 palette
entries, 1..16 functions, up to 16 resources, 1..128 declaration expressions,
up to 256 native call sites and up to 16 settings. JSON parser limits also apply;
the current compiler limits metadata to 256 tokens. Target descriptions may
impose additional memory/admission limits even when the package fits these caps.

Current scalar types are i32=1, f32=2, packed RGB8=3, bool=4 and Q16.16=7;
reference type is 6. Type 5 is rejected. Register roles are local=0, invocation input=1, constant=2,
persistent state=3 and setting=6. Roles 4 and 5 are rejected. F32 initializer bits
must be finite, bool is 0/1 and colour fits 24 bits. References are validated
resource identities, never numerical addresses. All 32-bit fixed initializers
are valid. Fixed arrays use resource kind 20, four-byte elements and zero flags.
Their length expression is i32 and their fill expression is fixed; seed/stream
expressions retain the existing i32 requirement. Fixed settings and live-input
wire declarations remain unsupported.

Function ranges partition the instruction section in order, with no overlap or
gaps. A void function ends in END and a valued function in RETURN. Branch targets
stay in their function. Arguments occupy the declared contiguous input-register
range, with at most eight arguments. Profile bindings impose further rules for
entry arguments and resources; see [animation](animation-profile.md) and
[transition](transition-profile.md).

For an animation, exactly two void functions bind init/render through distinct
header entry indices. Their permissions must be 9/13, argument start 0 and
argument count 3. Registers 0..2 are zero-initialized invocation inputs with
types i32/f32/f32 (count, seconds, delta); no other register has input role.
Resource 0 is output kind 12 (premultiplied RGBA), flags 1, with a length
expression of opcode 1 (pixel count). All other resources have no output flag.
Output fill is a colour expression; seed/stream are i32 expressions. Fixed-array
resources use kind 20 and flags 0. Their constant reference registers contain
the resource-table index in `initial_bits`.

Literal declaration expressions use opcode 0, `a=b=0` and raw `value` bits.
Pixel count uses opcode 1, type i32 and zero `a,b,value`. The fixed expression
encodings below refer to earlier expression indices; they cannot read executable
locals. This is sufficient to allocate an output and a fixed array initialized
by a pure fixed expression, without calling any compiler implementation.

Permission bits are persistent-state writes=1, output access=4 and stateful
RNG=8; other bits are rejected. Current compiler initialization/render functions
use 9/13 respectively. Local arithmetic is pure, but assigning its result to
persistent state still requires permission. Native resource writes are checked
against the resource identity, mutability and destination as well as the call's
signature. The [loader](../src/portable.c),
[drawing validator](../src/portable_draw.c) and
[type/opcode definitions](../src/bytecode_core.h) remain the executable authority.

## Manifest and operation selection

Use `schema: "lightvm.program"`, a valid `compiler_version`, an application
`requires` floor, the matching `profile`, `pixels: {"format":"rgba8-premul"}`
and a `settings` array consistent with the setting table. Declare live inputs
using the existing [input contract](inputs.md). No compiler implementation name
changes runtime semantics. See [versioning](versioning.md) for release checks.

Choose operations by explicit signatures, never by a host value's incidental
representation. Fold constants only when the fold preserves intermediate
rounding, wrapping and fault order. Report unsupported operations before emission.
Unknown/rejected opcodes and native bindings remain rejected; no runtime shim
or source-language compatibility mode is introduced.

The [operation catalogue](design/numeric-operations.json) assigns encodings
and their application floors. A package using a fixed type,
fixed array or instruction 40..61 must declare `requires` at least 1.2.0,
including generic typed callbacks. The loader rejects a falsely lowered floor
even on newer firmware. Existing programs retain their earlier feature floors.

## Core instruction encoding

Every unused operand is zero. Registers are 8-bit indices; branch PCs and map
end PCs are absolute instruction indices. `a` is a writable result unless the
row says otherwise. The loader checks types and effects before activation.

| ID | Operation | Operands and behaviour |
| --- | --- | --- |
| 0 | END | All fields zero; returns from a void function |
| 1 | RETURN | `a` has the function's result type; other fields zero |
| 2 | MOV | `a,b` same declared type |
| 3,4,5 | IADD, ISUB, IMUL | `a,b,c` i32; wrap modulo 2^32 |
| 6,7 | IDIV, IMOD | `a,b,c` i32; quotient floors; remainder has divisor's sign |
| 8,9,10 | FADD, FSUB, FMUL | `a,b,c` f32; each instruction rounds to binary32 |
| 11,12 | FDIV, FMOD | `a,b,c` f32; FMOD is `b-floor(b/c)*c` |
| 13 | ITOF | `a` f32, `b` i32; nearest binary32 under the runtime's normal rounding mode |
| 14 | FTOI | `a` i32, `b` f32; truncate toward zero with finite/range checks |
| 15,16 | ILT, FLT | `a` bool, `b,c` i32/f32; strict less-than |
| 17 | EQ | `a` bool, `b,c` same non-reference scalar type; f32 numeric equality, otherwise word equality |
| 18 | JZ | `a` bool; `d` target when false; `b,c=0` |
| 19 | JMP | `d` target; `a,b,c=0` |
| 20 | RGB | `a` colour; `b,c,d` i32 red/green/blue registers |
| 21 | HSV | `a` colour; `b,c,d` f32 hue turns, saturation and value registers |
| 23,24 | IMIN, IMAX | `a,b,c` i32 |
| 25,26 | FMIN, FMAX | `a,b,c` finite f32; signed-zero contract and floor 1.3.0 |
| 27 | HASH | `a` f32; `b,c` i32 index/seed; deterministic normalized polynomial hash |
| 33 | LOAD_ELEM | `a` element result, `b` constant resource reference, `c` i32 index |
| 34 | STORE_ELEM | `a` constant destination reference, `b` i32 index, `c` element value |
| 35 | LENGTH | `a` i32 result, `b` constant resource reference |
| 36 | RESOURCE_FILL | `a` constant destination reference, `b` element value, `c,d` i32 start/exclusive stop |
| 37 | RESOURCE_COPY | `a,b` constant destination/source references, same resource kind and count; `c,d=0` |
| 38 | NATIVE | `a` typed result or 255 for void; `d` call-site index, `b,c=0` |
| 39 | PIXEL_MAP | Verified body and control operands described below |

22 and 28..32 are unassigned/rejected. Colour registers contain `0x00RRGGBB`;
bool registers contain 0/1. RGB faults for any channel outside 0..255, including
negative i32 values. HSV requires finite operands, saturation/value in [0,1],
and wraps hue turns; it uses the shared quantized colour conversion. HASH uses
`x=(index+seed*131) mod 65521`, then `(17*x*x+31*x+101) mod 65521`, divided by
65521 as f32; the modular intermediate is nonnegative.

I32 division/remainder fault on zero divisors and `INT32_MIN/-1`. FDIV faults
on a zero divisor; FMOD also rejects nonfinite operands. Base float arithmetic
can produce a nonfinite intermediate: it does not adopt the newer math module's
finite-result contract. FTOI, colour conversion, f32 array storage and native
math check their own domain. EQ treats signed zeros as equal and NaN as unequal.
FTOI accepts [-2147483648,2147483648) and truncates; no implicit saturation occurs.

Element operations fault on invalid indices, sealed writes and invalid stored
values. FILL requires `0 <= start <= stop <= count`. COPY requires equal counts;
self-copy is a no-op. They are resumed in bounded chunks and preserve earlier
persistent writes on a later fault. Reads of output require output permission,
including through COPY and drawing bindings. These core IDs have floor 1.0.0
except finite-f32 min/max. Fixed operands require 1.2.0; gathers/branches/fixed
operations within maps require 1.3.0.

### Resource records and initialization

`length,fill,seed,stream` in a resource record are **expression indices**, not
inline values. Length must resolve to i32 in 0..65535. Admission additionally
checks available memory and profile-specific output rules. Element kinds are:

| Kind | Storage / element register type | Initialization and restrictions |
| --- | --- | --- |
| 1 | Four-byte signed integer / i32 | Fill i32; flags 0 |
| 2 | Four-byte binary32 / f32 | Finite fill/store values; flags 0 |
| 7 | Byte / i32 | Fill/store 0..255; flags 0 |
| 8 | Two-byte unsigned integer / i32 | Fill/store 0..65535; flags 0 |
| 11 | Four-byte unsigned storage / i32 | Supported value domain 0..INT32_MAX; flags 0 |
| 12 | Four-byte premultiplied RGBA / colour | Animation/transition output resource 0, flags 1 |
| 14,15,16,17 | RGB, RGBA, RGBW, RGBWA image / colour | Strides 3,4,4,5; flags 0 for owned or 2 for sealed artwork |
| 18 | Sprite batch / no scalar element | Positive capacity; i32 fill; seed is field count 8..24; stream is a literal field mask with no higher bits |
| 19 | Drawing style / no scalar element | Length 1; i32 fill; flags 0; initialize style with its native setters |
| 20 | Four-byte signed Q16.16 / fixed | Every raw word valid; fixed fill, flags 0; floor 1.2.0 |

Other kinds are rejected. Seed and stream expressions have i32 type. For numeric
arrays they carry no RNG effect. For images, seed is a palette-word offset and
stream is either zero (owned, cleared image) or the exact image byte count. Each
palette word contributes its low three bytes in little-endian order; a populated
image must fit in the palette section. Sealed images require populated artwork
and consume no copied pixel payload. Reading a colour discards white/alpha;
writing one sets white to zero and alpha to 255. Output RGBA reads unpremultiply
RGB. Raw channel access uses the image native bindings. Batch/style internals
are native storage, never pointers or layouts exposed to guest integers.

Declaration opcodes are separate from instruction opcodes: 0 literal, 1 pixel
count, 2 setting-register reference, 3 add, 4 subtract, 5 multiply, 6 integer
floor-divide, 7 modulo, 8 float divide, 9 min and 10 max. Binary `a,b` refer to
earlier expressions and `value=0`. Opcode 2 uses `a=setting register`, `b=value=0`;
setting dependencies must be acyclic. Arithmetic accepts i32/f32 with a float
result when either input is f32 or opcode 8 is used; integer floor-divide requires
two i32 operands. Mixed i32/f32 operands convert to f32 before arithmetic.
Unlike executable wrapping integer arithmetic, declaration i32 overflow faults;
every declaration f32 result must be finite. F32 declaration min/max requires
1.3.0 and shares scalar signed-zero behaviour. Numeric opcodes 40..71 use their
exact signatures, without implicit mixed types. Settings remain i32/f32/colour/
bool; a missing minimum/maximum expression is the setting-table sentinel
`0xffffffff`. Evaluation happens during preparation, before activation.

### Core native bindings

Call-site arguments are register indices and must match these types exactly.
Unused argument slots are zero; the instruction carries the result register.
`I,F,C,B,R` mean i32, f32, colour, bool and constant resource reference; `void`
requires result register 255. Bindings 0..2 and 18..26 are rejected.

| ID | Signature | Behaviour |
| --- | --- | --- |
| 3 | `() -> F` | Stateful uniform random in [0,1); RNG permission |
| 4 | `(I start,I stop,I step) -> I` | Stateful range choice, exclusive stop; nonzero step and nonempty range; RNG permission |
| 5,6 | `(I seed) -> void`, `() -> void` | Deterministic reseed / instance-default reseed; RNG permission |
| 7 | `(F radians) -> F` | Shared finite sine; floor 1.3.0 |
| 8,9,10 | `(C) -> I` | Red, green, blue byte extraction |
| 11 | `(F position) -> C` | Nonnegative finite position, period 256, three linear colour sectors |
| 12 | `(I degrees) -> C` | Integer degrees 0..360, cyclic three-sector sine/cosine wheel |
| 13 | `(C) -> F` | Hue in degrees rounded to nearest integer, ties even |
| 14 | `(B condition) -> void` | Fault if false |
| 15,16 | `(C) -> I`, `(I) -> C` | Packed colour word; conversion to colour requires 0..0xffffff |
| 17 | `(I low,I high) -> I` | Stateful inclusive integer range; RNG permission |

Drawing/sprite bindings 27..54 additionally validate resource kind, identity,
permissions and ranges in [portable_draw.c](../src/portable_draw.c). Their source
lowering names/signatures are in [sprite_compiler.py](../lightvm/sprite_compiler.py),
with public semantics in [PixelArray and sprites](pixelarray-sprites.md). Live-input snapshot bindings 55..58 return I/F/C/B from
one i32 input-table index; they require output permission and a matching declared
input. See [inputs](inputs.md). These effects are distinct from pure arithmetic:
an emitter cannot mark them pure by setting manifest metadata.

## Fixed and native numeric encoding

For binary instructions, `a` is the result register, `b` and `c` are operand
registers, and `d` is zero. Unary instructions use `a` and `b`; `c` and `d` are
zero. Types are exact: no implicit mixed fixed/i32/f32 arithmetic is permitted.
Writing a persistent result still requires the function's state-write permission.

| ID | Operation | Result and operands |
| --- | --- | --- |
| 40..44 | QADD, QSUB, QMUL, QDIV, QMOD | fixed ← fixed, fixed |
| 45 | QNEG | fixed ← fixed |
| 46..47 | QMIN, QMAX | fixed ← fixed, fixed |
| 48 | QLT | bool ← fixed, fixed |
| 49 | ITOQ | fixed ← i32 |
| 50 | QTOI | i32 ← fixed |
| 51 | FTOQ | fixed ← f32 |
| 52 | QTOF | f32 ← fixed |
| 53 | QFROMBITS | fixed ← i32 |
| 54 | QBITS | i32 ← fixed |
| 55 | QSQRT | fixed ← fixed |
| 56..58 | IAND, IOR, IXOR | i32 ← i32, i32 |
| 59 | INOT | i32 ← i32 |
| 60..61 | ISHL, ISHR | i32 ← i32, i32 |

Fixed equality uses existing EQ=17 with two fixed operands and a bool result.
MOV and existing array LOAD/STORE/LEN/FILL/COPY use their verified type rules;
raw numeric values cannot be substituted for resource references.

Declaration expressions use the same numeric IDs 40..61 and signatures. Their
`a`/`b` fields refer only to earlier expressions, `type` is the declared result,
and `value` is zero. Unary `b` must be zero. A literal expression has opcode 0
and stores its raw bits in `value`, with type 7 for fixed. Admission evaluates
these expressions with the same shared numeric implementation as scalar code.

## Native f32 encoding

Instructions 62..70 are unary `FABS, FFLOOR, FCEIL, FTRUNC, FSIN, FCOS, FSQRT,
FLOG, FEXP` in that order. Instruction 71 is binary FPOW. Their operands/results
are f32, with the same unary/binary field layout above and feature floor 1.3.0.
Declaration expressions accept the same IDs, signatures and earlier-expression
references. Existing FMIN=25, FMAX=26, f32 declaration min/max and native sine
binding 7 also require 1.3.0 and call the shared finite-f32 evaluator. Reserved
operands must be zero. The compiler and loader enforce this semantic floor,
including generic typed callbacks. Recompile earlier experimental packages.

## Prepared pixel kernels

PIXEL_MAP=39 uses `a=destination reference`, `b=i32 index`, `c=i32 bound`,
`d=exclusive body end PC`. The body starts at the next instruction, contains
1..128 instructions and ends in one STORE_ELEM to that destination/current index.
The map iterates from zero to the captured bound; nonpositive bounds do no work.
The index and bound must be distinct registers so that continuation updates cannot
change the bound. The loader rejects aliased control registers before plan
preparation, including empty ranges. The compiler preserves their distinction
through register allocation.
Every path reaches the final store. JMP/JZ targets must be strictly forward and
inside the body. No nested loops, early exits, skip-writes or multiple stores are
admitted. The verifier proves every read of a body-written local is defined on
every incoming path; captured inputs and read-only locals remain stable.

Pure scalar arithmetic, the numeric operations above, existing colour helpers
and resident numeric LOAD_ELEM/LENGTH are eligible. Gathers accept numeric array
kinds 1, 2, 7, 8, 11 and 20 with zero flags. References must be verified constant
resource identities; source/destination identity may not alias. Image reads,
source mutation, persistent scalar writes, stateful RNG and scatter stay scalar.
Bodies above 32 instructions, conditional/nontrivial forward branches, gathers,
fixed values and numeric instructions 40..71 require floor 1.3.0. Earlier simple
map bodies retain their old floor. A falsely lowered floor is rejected.

Preparation admits immutable decoded plans, charges their exact allocation to
the VM heap (`map_plan_bytes` telemetry), and frees them with the VM. It scans
32 code instructions or decodes 16 body instructions per step; CFG verification
is bounded by the 128-instruction limit. There is no render allocation or hidden
fallback when admission fails. Each worker has bounded private register scratch.
Straight-line invariants are cached after their first ordered evaluation;
branches are lazy and their arms are never hoisted.

Each plan records the conservative first-pixel path cost and steady-pixel path
cost separately. A lane chooses `first + (pixels-1)*steady <= 1024`, with maxima
of 128 total split, 64 unsplit transient or 32 persistent pixels. First evaluation
uses full operation costs; a later cached read costs 1. Conditional maps use the
maximum reachable path and do not hoist either arm. A first-pixel path above
1024 units is rejected, even for an empty range. The compiler leaves such a
source loop ordered and reports the work-limit reason. There is no one-pixel
exception or runtime fallback.

The generated [operation metadata](design/numeric-operations.json) assigns work
128 to log/exp/pow, 64 to sin/cos, 32 to square roots, 8 to division/modulo, 4 to
HSV/fixed multiply, 2 to gathers, otherwise 1. These units are conservative
accounting weights, not measured microseconds. Target calibration and physical
service-time validation remain release gates; time checks occur between pixels.

`LIGHTVM_INVOCATION_LIMIT` now counts weighted scalar operations and map pixel
reservations, plus outer dispatch overhead. Each attempted map pixel reserves
its full applicable path cost before executing, including when it later faults.
Every lane's work counts, including unpublished suffixes that are later recomputed.
Cached reads use steady cost only after that lane's first evaluation. FILL adds
one unit per element and COPY three per element (load cost 2 plus store cost 1).
Other native drawing bindings retain their bounded native dispatch/time contract;
this meter is not a cycle counter or a universal cost model for native services.

Reservations never exceed remaining fuel; near exhaustion execution uses one
lane. `dispatch_count` remains a separate diagnostic counter. Work exhaustion
can differ between scalar and optimized code, worker counts and yield schedules:
invariants, dispatch reduction, worst-path reservations and speculation change
charged work. Numeric/domain-fault equivalence is tested independently. This
contract bounds charged work without claiming optimizer-neutral watchdog cutoffs.

Only transient output splits across workers. Persistent images stay serial to
retain partial writes before a fault. Across yields only a contiguous completed
prefix advances; unpublished speculative output is recomputed when necessary.
Final locals merge from the last executed write, including conditional writes.
Resource generations and the executor lifetime fence remain enforced.

## Conformance and remaining integration gates

1. S1 binds the shared numeric implementation through `cmake/lightvm_core.cmake`
   for every target, with register/resource typing, initialization, declaration
   evaluation and runtime faults updated together. See the
   [S1 validation record](design/numeric-s1-validation-2026-10-22.md).
   Fixed wire settings and live-input types remain out of scope.
2. Replay the [numeric vectors](../tests/fixtures/numeric-vectors.json) through
   constant folding, ordinary bytecode and each admitted bulk path. Include
   malformed types, bindings, permissions, alias claims and resource identities.
   Verify signed zeros and raw results, not only rendered pixels.
3. For broader kernels, prove stable sources, definite assignment and lazy
   branches; reject source mutation and aliases. An inactive branch may contain
   an invalid load or division. No speculative evaluation may move a fault ahead
   of observable writes. Kernel eligibility does not imply frame-reuse eligibility.
4. Exercise yields, cancellation and allocation failure. Charge prepared plans
   and scratch before activation, preserve generations and the active scene on
   rejection, and retain ordered partial writes for persistent destinations.
5. The [standalone emitter](../tests/fixtures/independent_emitter.py) imports only
   the Python standard library and writes full profile packages using the wire
   tables above. Scalar/bulk versions share expected raw state, fixed arrays and
   pixels. Invalid variants cover types, identities, flags, permissions, CFG,
   source mutation, reserved operands and declared feature floors. It passes
   host and attached-Pico admission/rendering; the older 1.1.0 application
   rejects both valid new packages before replacing a live scene. See the
   [S5 validation record](design/numeric-s5-validation-2026-10-22.md).
6. Run Python, JavaScript and Blocks emission through native execution. Recompute
   complete stack paths, admitted heap peaks and board timing after linking the
   new operations. Keep the Pico repair's archived packages and baseline results
   intact so new work has its own comparison.

If integration requires an incompatible experimental encoding, document source
recompilation and clean provisioning. Do not retain an obsolete decoder or add
automatic migration solely to preserve earlier experiments.

Reproduce the independent corpus and native checks with:

```sh
python3 tests/fixtures/independent_emitter.py build/independent-emitter
LIGHTVM_NUMERIC_RUNTIME=build/host/test_platform \
  python3 -m unittest discover -s tests -p test_independent_emitter.py -v
```

The [23 October validation](design/numeric-followup-validation-2026-10-23.md)
closes the implementation corrections with generated metadata, exhaustive native
signature/floor checks, malformed map and RGB-boundary independent fixtures, and
the full host parameter matrix. Physical service/performance gates remain open.
