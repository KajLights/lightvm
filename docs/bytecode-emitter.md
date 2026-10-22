# Independent bytecode emission

22 October 2026. Baseline: validated Pico repair `a9c8611`. This document records
the current container and the S1 encodings for the
[numeric foundation](numeric-types.md). The shared core adds numeric operations
under application feature floor 1.2.0. The container layout remains unchanged.

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
[transition](transition-profile.md), and the native profile validators.

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

The [operation catalogue](design/numeric-operations.json) assigns S1 encodings
and their 1.2.0 application floor. Its future S4 entries remain unset: an encoder
must reject an unset mapping or feature floor. A package using a fixed type,
fixed array or new numeric operation must declare `requires` at least 1.2.0,
including generic typed callbacks. The loader rejects a falsely lowered floor
even on newer firmware. Existing programs retain their earlier feature floors.

## S1 scalar numeric encoding

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

S1 does not admit these operations or fixed-typed values to map bodies. Emit an
ordinary loop until S2 supplies verified bulk execution; a feature floor never
authorizes an unimplemented map operation.

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
5. Add an independently encoded fixture that imports no Python compiler code.
   It must pass admission and rendering on host and intended targets, then fail
   cleanly when its required application feature floor is unsupported. This
   full-emitter fixture remains an S5 gate. S1 already has independent C-encoded
   scalar callback fixtures; those do not establish all frontend/profile parity.
6. Run Python, JavaScript and Blocks emission through native execution. Recompute
   complete stack paths, admitted heap peaks and board timing after linking the
   new operations. Keep the Pico repair's archived packages and baseline results
   intact so new work has its own comparison.

If integration requires an incompatible experimental encoding, document source
recompilation and clean provisioning. Do not retain an obsolete decoder or add
automatic migration solely to preserve earlier experiments.
