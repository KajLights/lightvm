#!/usr/bin/env python3
"""Restricted Python -> portable LBC1 bytecode. Parses source; never executes it."""
import ast
import math
import struct
from . import numeric

# Keep numeric IDs in sync with bytecode_core.h. Registers have fixed declared types.
OP = {name: i for i, name in enumerate([
    'END', 'RETURN', 'MOV', 'IADD', 'ISUB', 'IMUL', 'IDIV', 'IMOD',
    'FADD', 'FSUB', 'FMUL', 'FDIV', 'FMOD', 'ITOF', 'FTOI', 'ILT',
    'FLT', 'EQ', 'JZ', 'JMP', 'RGB', 'HSV'])}
OP.update(IMIN=23, IMAX=24, FMIN=25, FMAX=26, HASH=27)
OP.update(numeric.OPCODES)
I, F, C, B, Q = 1, 2, 3, 4, 7
LOCAL, INPUT, CONST, STATE = range(4)
MAX_REGS = 128

class CompileError(ValueError):
    def __init__(self, message, node=None):
        super().__init__(message)
        self.node = node

class Compiler:
    def __init__(self, source):
        self.source = source
        self.numeric_used = False
        self.tree = ast.parse(source)
        self.regs, self.code, self.constants, self.names = [], [], {}, {}
        self.funcs, self.table = {}, []
    def fail(self, node, message):
        raise CompileError(message, node)

    def reg(self, typ, role=LOCAL, value=0):
        if len(self.regs) >= MAX_REGS:
            raise CompileError('register limit exceeded')
        self.regs.append((typ, role, value))
        return len(self.regs)-1

    def literal(self, value):
        typ = B if type(value) is bool else I if type(value) is int else F if type(value) is float else None
        if typ is None or (typ == F and not math.isfinite(value)) or (typ == I and not -(1<<31) <= value < (1<<31)):
            raise CompileError('expected finite f32, i32 or bool literal')
        bits = struct.unpack('<I', struct.pack('<f', value))[0] if typ == F else int(value) & 0xffffffff
        key = typ, bits
        if key not in self.constants:
            self.constants[key] = self.reg(typ, CONST, bits)
        return self.constants[key]

    def emit(self, op, a=0, b=0, c=0, imm=0):
        self.code.append([OP[op], a, b, c, imm])
        return len(self.code)-1

    def typ(self, r):
        return self.regs[r][0]

    def raw_constant(self, typ, bits):
        key = typ, bits & 0xffffffff
        if key not in self.constants:
            self.constants[key] = self.reg(typ, CONST, key[1])
        return self.constants[key]

    def numeric_op(self, name, *args):
        signature = numeric.SIGNATURES[name]
        if tuple(self.typ(r) for r in args) != signature[1:]:
            raise CompileError('numeric operands require explicit matching types')
        self.numeric_used = True
        if getattr(self, 'optimize', True) and all(self.regs[r][1] == CONST for r in args):
            try:
                bits = numeric.evaluate(name, *(self.regs[r][2] for r in args))
            except ArithmeticError:
                pass  # Preserve runtime fault ordering and lazy control flow.
            else:
                return self.raw_constant(signature[0], bits)
        out = self.reg(signature[0])
        self.emit(name, out, *args)
        return out

    def fixed_literal(self, node):
        operand = node.operand if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)) else node
        if not isinstance(operand, ast.Constant) or type(operand.value) not in (int, float):
            return None
        token = ast.get_source_segment(self.source, operand)
        if token is None:
            # Compiler-created integer nodes are exact; floats must retain their
            # source token instead of silently using a rounded AST value.
            if type(operand.value) is not int:
                self.fail(node, 'fixed literal requires its original source token')
            token = str(operand.value)
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub): token = '-' + token
        try: bits = numeric.literal(token)
        except (ValueError, ArithmeticError) as error: self.fail(node, str(error))
        self.numeric_used = True
        return self.raw_constant(Q, bits)

    def convert(self, r, typ, explicit=False):
        if self.typ(r) == typ:
            return r
        if Q in (self.typ(r), typ):
            if not explicit:
                raise CompileError('fixed values require an explicit numeric conversion')
            name = {(I, Q): 'ITOQ', (F, Q): 'FTOQ', (Q, I): 'QTOI', (Q, F): 'QTOF'}.get((self.typ(r), typ))
            if name is None: raise CompileError('fixed conversion requires i32, f32 or fixed')
            return self.numeric_op(name, r)
        if (self.typ(r), typ) not in [(I, F), (F, I)]:
            raise CompileError('incompatible types')
        out = self.reg(typ)
        self.emit('ITOF' if typ == F else 'FTOI', out, r)
        return out

    def pair(self, a, b):
        if Q in (self.typ(a), self.typ(b)):
            if self.typ(a) != Q or self.typ(b) != Q:
                raise CompileError('mixed fixed arithmetic requires an explicit conversion')
            return a, b, Q
        if self.typ(a) not in (I, F) or self.typ(b) not in (I, F):
            raise CompileError('arithmetic requires numbers')
        typ = F if F in (self.typ(a), self.typ(b)) else I
        return self.convert(a, typ), self.convert(b, typ), typ

    def expr(self, n):
        if isinstance(n, ast.Constant):
            return self.literal(n.value)
        if isinstance(n, ast.Name) and n.id in self.env:
            return self.env[n.id]
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.USub):
            if isinstance(n.operand, ast.Constant):
                return self.literal(-n.operand.value)
            r = self.expr(n.operand)
            if self.typ(r) == Q: return self.numeric_op('QNEG', r)
            return self.binary(ast.Sub(), self.literal(0.0 if self.typ(r)==F else 0), r)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.Invert):
            return self.numeric_op('INOT', self.expr(n.operand))
        if isinstance(n, ast.BinOp):
            return self.binary(n.op, self.expr(n.left), self.expr(n.right))
        if isinstance(n, ast.Compare) and len(n.ops)==1:
            a,b,t = self.pair(self.expr(n.left), self.expr(n.comparators[0]))
            op = n.ops[0]
            if isinstance(op, ast.Gt):
                a,b = b,a
            elif not isinstance(op, (ast.Lt, ast.Eq)):
                self.fail(n, 'only <, > and == comparisons are supported')
            out = self.reg(B)
            self.emit('EQ' if isinstance(op, ast.Eq) else 'QLT' if t==Q else 'FLT' if t==F else 'ILT', out,a,b)
            return out
        if isinstance(n, ast.IfExp):
            cond = self.expr(n.test)
            if self.typ(cond) != B: self.fail(n, 'condition must be bool')
            branch = self.emit('JZ', cond)
            a = self.expr(n.body); out = self.reg(self.typ(a)); self.emit('MOV',out,a)
            jump = self.emit('JMP'); self.code[branch][4] = len(self.code)
            b = self.expr(n.orelse)
            if self.typ(a) != self.typ(b): self.fail(n, 'conditional branches must have the same type')
            self.emit('MOV',out,b); self.code[jump][4] = len(self.code)
            return out
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and not n.keywords:
            name = n.func.id
            if name == 'fixed' and len(n.args) == 1:
                literal = self.fixed_literal(n.args[0])
                return literal if literal is not None else self.convert(self.expr(n.args[0]), Q, explicit=True)
            if name in ('fixed_bits', 'fixed_from_bits', 'fixed_sqrt') and len(n.args) == 1:
                return self.numeric_op({'fixed_bits':'QBITS', 'fixed_from_bits':'QFROMBITS', 'fixed_sqrt':'QSQRT'}[name], self.expr(n.args[0]))
            args = [self.expr(a) for a in n.args]
            if name in ('int', 'float') and len(args)==1:
                return self.convert(args[0], I if name=='int' else F, explicit=True)
            if name in ('min','max') and len(args)==2:
                a,b,t=self.pair(*args)
                if t == Q: return self.numeric_op('Q'+name.upper(), a, b)
                out=self.reg(t)
                self.emit(('F' if t==F else 'I')+name.upper(),out,a,b);return out
            if name in ('hsv','rgb') and len(args)==3:
                typ = F if name=='hsv' else I
                args=[self.convert(r,typ) for r in args]
                out=self.reg(C);self.emit(name.upper(),out,args[0],args[1],args[2]);return out
            if name=='hash01' and len(args)==2 and all(self.typ(r)==I for r in args):
                out=self.reg(F);self.emit('HASH',out,*args);return out
        self.fail(n, 'unsupported expression')

    def binary(self, op, a, b):
        bitops = {ast.BitAnd:'IAND', ast.BitOr:'IOR', ast.BitXor:'IXOR', ast.LShift:'ISHL', ast.RShift:'ISHR'}
        if type(op) in bitops: return self.numeric_op(bitops[type(op)], a, b)
        a,b,t=self.pair(a,b)
        if t == Q:
            names = {ast.Add:'QADD', ast.Sub:'QSUB', ast.Mult:'QMUL', ast.Div:'QDIV', ast.Mod:'QMOD'}
            if type(op) not in names: raise CompileError('fixed arithmetic supports +, -, *, / and %')
            return self.numeric_op(names[type(op)], a, b)
        if isinstance(op,ast.Div):
            a,b,t=self.convert(a,F),self.convert(b,F),F
        names={ast.Add:'ADD',ast.Sub:'SUB',ast.Mult:'MUL',ast.Div:'DIV',ast.FloorDiv:'DIV',ast.Mod:'MOD'}
        if type(op) not in names or (isinstance(op, ast.FloorDiv) and t==F):
            raise CompileError('unsupported arithmetic operation')
        out=self.reg(t)
        self.emit(('F' if t==F else 'I')+names[type(op)],out,a,b);return out

    def colour_literal(self,n):
        if not isinstance(n,ast.Call) or getattr(n.func,'id','')!='rgb' or len(n.args)!=3 or n.keywords:self.fail(n,'palette colours require rgb literals')
        try:parts=[ast.literal_eval(a) for a in n.args]
        except (ValueError,TypeError):self.fail(n,'colour channels must be literals')
        if any(type(v)!=int or not 0<=v<=255 for v in parts):self.fail(n,'RGB channels must be 0..255')
        return parts[0]<<16|parts[1]<<8|parts[2]

    def colour_constant(self,v):
        return self.reg(C,CONST,v)
