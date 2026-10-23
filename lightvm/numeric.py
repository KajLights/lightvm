"""Exact compile-time evaluation of the core's explicit numeric operations.

Values cross this interface as unsigned 32-bit words. Faulting expressions are
left in bytecode by the caller, so folding cannot move a fault out of a branch.
"""
import ast
from fractions import Fraction
import math
import struct

MASK = 0xffffffff
SCALE = 65536
RELEASE = '1.2.0'
MATH_RELEASE = '1.3.0'
I, F, B, Q = 1, 2, 4, 7
from .numeric_metadata import OPERATIONS, NATIVE_OPERATIONS, BASE_WORK, MAP_WORK_BUDGET
SIGNATURES = {name:tuple(row['signature']) for name,row in OPERATIONS.items() if row['id']>=40}
MATH_SIGNATURES = {name:sig for name,sig in SIGNATURES.items() if OPERATIONS[name]['id']>=62}
OPCODES = {name:OPERATIONS[name]['id'] for name in SIGNATURES}
WORK = BASE_WORK | {row['id']:row['work'] for row in OPERATIONS.values()}


def instruction_work(op, binding=None):
    if binding in NATIVE_OPERATIONS: return OPERATIONS[NATIVE_OPERATIONS[binding]]['work']
    return WORK.get(op, 1)


def signed(bits):
    bits &= MASK
    return bits - (1 << 32) if bits & (1 << 31) else bits


def checked(value):
    if not -(1 << 31) <= value < (1 << 31):
        raise ArithmeticError('fixed value is outside Q16.16 range')
    return value & MASK


def literal(token):
    """Use the numeric token, retaining digits beyond host-float precision."""
    token = token.replace('_', '').strip()
    value = ast.literal_eval(token)
    if type(value) not in (int, float):
        raise ValueError('fixed requires a numeric literal')
    exact = Fraction(value) if type(value) is int else Fraction(token)
    return checked(round(exact * SCALE))


def evaluate(name, left, right=0):
    a, b = signed(left), signed(right)
    if name == 'QADD': value = a + b
    elif name == 'QSUB': value = a - b
    elif name == 'QMUL': value = math.trunc(Fraction(a * b, SCALE))
    elif name == 'QDIV': return checked(math.trunc(Fraction(a * SCALE, b)))
    elif name == 'QMOD': value = a % b
    elif name == 'QNEG': value = -a
    elif name == 'QMIN': value = min(a, b)
    elif name == 'QMAX': value = max(a, b)
    elif name == 'QLT': value = int(a < b)
    elif name == 'ITOQ': return checked(a * SCALE)
    elif name == 'QTOI': value = math.trunc(Fraction(a, SCALE))
    elif name == 'FTOQ':
        f = struct.unpack('<f', struct.pack('<I', left & MASK))[0]
        if not math.isfinite(f): raise ArithmeticError('fixed conversion requires finite f32')
        return checked(round(Fraction.from_float(f) * SCALE))
    elif name == 'QTOF':
        return struct.unpack('<I', struct.pack('<f', a / SCALE))[0]
    elif name in ('QFROMBITS', 'QBITS'): value = left
    elif name == 'QSQRT':
        if a < 0: raise ArithmeticError('fixed square root requires a nonnegative value')
        value = math.isqrt(a * SCALE)
    elif name == 'IAND': value = left & right
    elif name == 'IOR': value = left | right
    elif name == 'IXOR': value = left ^ right
    elif name == 'INOT': value = ~left
    elif name in ('ISHL', 'ISHR'):
        if not 0 <= b <= 31: raise ArithmeticError('shift count must be 0..31')
        value = left << b if name == 'ISHL' else a >> b
    else: raise ValueError('unknown numeric operation: ' + name)
    return value & MASK
