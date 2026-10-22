"""Independent exact-value oracle, deliberately outside the SDK/compiler.

Fraction models arithmetic and decimal literals; Python's arbitrary integers,
binary32 packing and isqrt supply independent implementations of the C paths.
Inputs/outputs of evaluate are unsigned 32-bit words, including float bit patterns.
"""
from fractions import Fraction
import math
import struct

SCALE = 65536
MINIMUM, MAXIMUM = -(1 << 31), (1 << 31) - 1
MASK = (1 << 32) - 1


class NumericFault(ValueError):
    pass


def signed(word):
    return word if word < (1 << 31) else word - (1 << 32)


def checked(value):
    if not MINIMUM <= value <= MAXIMUM:
        raise NumericFault('range')
    return value & MASK


def literal(text):
    """The caller supplies a numeric source token, not a host float AST value."""
    return checked(round(Fraction(text) * SCALE))


def evaluate(operation, *words):
    a = signed(words[0])
    b = signed(words[1]) if len(words) > 1 else 0
    x, y = Fraction(a, SCALE), Fraction(b, SCALE)
    if operation == 'q16.add': result = int((x + y) * SCALE)
    elif operation == 'q16.sub': result = int((x - y) * SCALE)
    elif operation == 'q16.neg': result = int(-x * SCALE)
    elif operation == 'q16.mul': result = int(x * y * SCALE)
    elif operation == 'q16.div':
        if not y: raise NumericFault('zero')
        return checked(int(x / y * SCALE))
    elif operation == 'q16.mod':
        if not y: raise NumericFault('zero')
        result = int((x % y) * SCALE)
    elif operation == 'q16.min': result = int(min(x, y) * SCALE)
    elif operation == 'q16.max': result = int(max(x, y) * SCALE)
    elif operation == 'q16.lt': result = int(x < y)
    elif operation == 'q16.eq': result = int(x == y)
    elif operation == 'q16.sqrt':
        if x < 0: raise NumericFault('domain')
        result = math.isqrt(a * SCALE)
    elif operation == 'q16.from_i32': return checked(a * SCALE)
    elif operation == 'q16.to_i32': result = int(x)
    elif operation == 'q16.from_f32':
        value = struct.unpack('<f', struct.pack('<I', words[0]))[0]
        if not math.isfinite(value): raise NumericFault('nonfinite')
        return checked(round(Fraction.from_float(value) * SCALE))
    elif operation == 'q16.to_f32':
        # Every Q16.16 value is exactly representable as a Python binary64.
        return struct.unpack('<I', struct.pack('<f', float(x)))[0]
    elif operation in ('q16.bits', 'q16.from_bits'): result = a
    elif operation == 'i32.and': result = a & b
    elif operation == 'i32.or': result = a | b
    elif operation == 'i32.xor': result = a ^ b
    elif operation == 'i32.not': result = ~a
    elif operation in ('i32.shl', 'i32.shr'):
        if not 0 <= b < 32: raise NumericFault('shift')
        result = a * 2**b if operation == 'i32.shl' else a // 2**b
    else: raise ValueError(operation)
    return result & MASK
