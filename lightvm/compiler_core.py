#!/usr/bin/env python3
"""Restricted Python -> portable LBC1 bytecode. Parses source; never executes it."""
import ast
import math
import struct

# Keep numeric IDs in sync with bytecode_core.h. Registers have fixed declared types.
OP = {name: i for i, name in enumerate([
    'END', 'RETURN', 'MOV', 'IADD', 'ISUB', 'IMUL', 'IDIV', 'IMOD',
    'FADD', 'FSUB', 'FMUL', 'FDIV', 'FMOD', 'ITOF', 'FTOI', 'ILT',
    'FLT', 'EQ', 'JZ', 'JMP', 'RGB', 'HSV'])}
OP.update(IMIN=23, IMAX=24, FMIN=25, FMAX=26, HASH=27)
I, F, C, B = 1, 2, 3, 4
LOCAL, INPUT, CONST, STATE = range(4)
MAX_REGS = 128

class CompileError(ValueError):
    def __init__(self, message, node=None):
        super().__init__(message)
        self.node = node

class Compiler:
    def __init__(self, source):
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

    def convert(self, r, typ):
        if self.typ(r) == typ:
            return r
        if (self.typ(r), typ) not in [(I, F), (F, I)]:
            raise CompileError('incompatible types')
        out = self.reg(typ)
        self.emit('ITOF' if typ == F else 'FTOI', out, r)
        return out

    def pair(self, a, b):
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
            return self.binary(ast.Sub(), self.literal(0.0 if self.typ(r)==F else 0), r)
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
            self.emit('EQ' if isinstance(op, ast.Eq) else 'FLT' if t==F else 'ILT', out,a,b)
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
            args = [self.expr(a) for a in n.args]
            if name in ('int', 'float') and len(args)==1:
                return self.convert(args[0], I if name=='int' else F)
            if name in ('min','max') and len(args)==2:
                a,b,t=self.pair(*args);out=self.reg(t)
                self.emit(('F' if t==F else 'I')+name.upper(),out,a,b);return out
            if name in ('hsv','rgb') and len(args)==3:
                typ = F if name=='hsv' else I
                args=[self.convert(r,typ) for r in args]
                out=self.reg(C);self.emit(name.upper(),out,args[0],args[1],args[2]);return out
            if name=='hash01' and len(args)==2 and all(self.typ(r)==I for r in args):
                out=self.reg(F);self.emit('HASH',out,*args);return out
        self.fail(n, 'unsupported expression')

    def binary(self, op, a, b):
        a,b,t=self.pair(a,b)
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
