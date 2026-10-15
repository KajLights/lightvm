#!/usr/bin/env python3
"""Restricted Python -> portable LBC1 bytecode. Parses source; never executes it."""
import argparse
import ast
import json
import math
import struct
import re
from pathlib import Path

# Keep numeric IDs in sync with vm.h. Registers have fixed declared types.
OPS = ['END', 'RETURN', 'MOV', 'IADD', 'ISUB', 'IMUL', 'IDIV', 'IMOD',
       'FADD', 'FSUB', 'FMUL', 'FDIV', 'FMOD', 'ITOF', 'FTOI', 'ILT',
       'FLT', 'EQ', 'JZ', 'JMP', 'RGB', 'HSV', 'PALETTE', 'IMIN', 'IMAX',
       'FMIN', 'FMAX', 'HASH', 'HSV_RETURN', 'STORE', 'FILL', 'HUE_RETURN', 'IFMADD']
OP = {name: i for i, name in enumerate(OPS)}
I, F, C, B = 1, 2, 3, 4
LOCAL, INPUT, CONST, STATE, FRAME, PARAM = range(6)
MAX_REGS = 128

class CompileError(ValueError):
    def __init__(self, message, node=None):
        super().__init__(message)
        self.node = node

class Compiler:
    def __init__(self, source, fuse=True):
        self.tree = ast.parse(source)
        self.fuse = fuse
        self.regs, self.code, self.constants, self.names, self.palettes = [], [], {}, {}, {}
        self.params, self.funcs, self.table = [], {}, []
        for name, typ in [('count', I), ('index', I), ('time', F), ('delta', F)]:
            self.names[name] = self.reg(typ, INPUT)
        self.stage = 0
        self.kernel = False

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
        if isinstance(n, ast.Subscript) and isinstance(n.value, ast.Name) and n.value.id in self.palettes:
            r = self.expr(n.slice)
            if self.typ(r)!=I: self.fail(n, 'palette index must be i32')
            start, count = self.palettes[n.value.id]
            out = self.reg(C); self.emit('PALETTE',out,r,count,start)
            return out
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and not n.keywords:
            name = n.func.id
            if name=='len' and len(n.args)==1 and isinstance(n.args[0], ast.Name) and n.args[0].id in self.palettes:
                return self.literal(self.palettes[n.args[0].id][1])
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
        # Combine a typed index conversion, multiply and add without FMA rounding.
        if self.fuse and isinstance(op,ast.Add) and t==F and len(self.code)>=2:
            mul,conv=self.code[-1],self.code[-2]
            if mul[0]==OP['FMUL'] and mul[1]==b and conv[0]==OP['ITOF'] and mul[2]==conv[1] and a not in (mul[1],conv[1]):
                self.code.pop();self.code.pop();self.emit('IFMADD',out,conv[2],mul[3],a);return out
        self.emit(('F' if t==F else 'I')+names[type(op)],out,a,b);return out

    def statements(self, nodes):
        for n in nodes:
            if isinstance(n, (ast.Global, ast.Pass)) or (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value,str)):
                continue
            if isinstance(n, ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
                name=n.targets[0].id;r=self.expr(n.value)
                if name in self.names:
                    if name not in self.writes: self.fail(n,'assignment requires a global declaration')
                    target=self.names[name]
                    if self.regs[target][1] != (STATE if self.stage==0 else FRAME if self.stage==1 else -1):
                        self.fail(n,'entry point cannot mutate this global')
                else:
                    target=self.env.get(name)
                    if target is None: target=self.reg(self.typ(r));self.env[name]=target
                if self.typ(target)!=self.typ(r): self.fail(n,'assignment changes type')
                self.emit('MOV',target,r)
            elif isinstance(n, ast.Assign) and self.kernel and self.stage==2 and len(n.targets)==1 and isinstance(n.targets[0],ast.Subscript) and isinstance(n.targets[0].value,ast.Name) and n.targets[0].value.id=='pixels':
                at=self.expr(n.targets[0].slice);value=self.expr(n.value)
                if self.typ(at)!=I or self.typ(value)!=C:self.fail(n,'pixel store requires i32 index and RGB colour')
                self.emit('STORE',at,value)
            elif isinstance(n,ast.Expr) and self.kernel and self.stage==2 and isinstance(n.value,ast.Call) and isinstance(n.value.func,ast.Attribute) and isinstance(n.value.func.value,ast.Name) and n.value.func.value.id=='pixels' and n.value.func.attr=='fill' and len(n.value.args)==1 and not n.value.keywords:
                value=self.expr(n.value.args[0])
                if self.typ(value)!=C:self.fail(n,'fill requires RGB colour')
                self.emit('FILL',value)
            elif isinstance(n,ast.For) and self.kernel and self.stage==2 and isinstance(n.target,ast.Name) and isinstance(n.iter,ast.Call) and getattr(n.iter.func,'id','')=='range' and len(n.iter.args)==1 and not n.iter.keywords and not n.orelse:
                name=n.target.id
                if name in self.names:self.fail(n,'loop variable cannot shadow a global')
                stop=self.expr(n.iter.args[0])
                if self.typ(stop)!=I:self.fail(n,'range requires i32 bound')
                bound=self.reg(I);self.emit('MOV',bound,stop);stop=bound
                loop=self.reg(I);self.env[name]=loop;self.emit('MOV',loop,self.literal(0))
                head=len(self.code);condition=self.reg(B);self.emit('ILT',condition,loop,stop);end=self.emit('JZ',condition)
                self.statements(n.body);self.emit('IADD',loop,loop,self.literal(1));self.emit('JMP',imm=head);self.code[end][4]=len(self.code)
            elif isinstance(n, ast.If):
                r=self.expr(n.test)
                if self.typ(r)!=B:self.fail(n,'condition must be bool')
                at=self.emit('JZ',r);before=dict(self.env);self.statements(n.body);body=dict(self.env)
                end=self.emit('JMP');self.code[at][4]=len(self.code);self.env=dict(before)
                self.statements(n.orelse);self.code[end][4]=len(self.code)
                # Only variables established before the branch remain available.
                self.env=before
            elif isinstance(n,ast.Return) and self.stage==2 and not self.kernel:
                r=self.expr(n.value)
                if self.typ(r)!=C:self.fail(n,'shade returns RGB colour')
                if self.fuse and self.code and self.code[-1][0]==OP['HSV'] and self.code[-1][1]==r:
                    _,_,h,s,v=self.code.pop()
                    if all(self.regs[r]==(F,CONST,0x3f800000) for r in (s,v)):
                        self.emit('HUE_RETURN',h)
                    else:self.emit('HSV_RETURN',h,s,0,v)
                else:self.emit('RETURN',r)
            else:self.fail(n,'unsupported statement')

    def compile(self):
        allowed={'parameter','rgb','hsv','hash01','step','prepare','shade','kernel'}
        for n in self.tree.body:
            if isinstance(n,ast.ImportFrom):
                if n.module!='lightvm' or n.level or any(a.name not in allowed or a.asname for a in n.names):self.fail(n,'only explicit lightvm imports supported')
            elif isinstance(n,ast.FunctionDef):
                if len(n.decorator_list)!=1 or not isinstance(n.decorator_list[0],ast.Name) or n.decorator_list[0].id not in ('step','prepare','shade','kernel'):
                    self.fail(n,'expected @step, @prepare or @shade')
                role=n.decorator_list[0].id
                if role=='kernel':self.kernel=True;role='shade'
                if role in self.funcs or n.args.args or n.args.posonlyargs or n.args.kwonlyargs or n.args.vararg or n.args.kwarg:self.fail(n,'one zero-argument function per stage')
                self.funcs[role]=n
            elif isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.targets[0],ast.Name):
                name=n.targets[0].id
                if name in self.names or name in self.palettes or name in allowed or name=='pixels':self.fail(n,'duplicate/reserved name')
                value=n.value;role=STATE;bounds={}
                if isinstance(value,ast.Tuple):
                    start=len(self.table)
                    for item in value.elts:
                        self.table.append(self.colour_literal(item))
                    if not value.elts or len(value.elts)>255:self.fail(n,'palette size must be 1..255')
                    self.palettes[name]=(start,len(value.elts));continue
                if isinstance(value,ast.Call) and getattr(value.func,'id','')=='parameter':
                    if len(value.args)!=1 or any(k.arg not in ('minimum','maximum') for k in value.keywords):self.fail(n,'parameter takes one default value and optional minimum/maximum')
                    for k in value.keywords:
                        if k.arg in bounds:self.fail(n,'duplicate parameter bound')
                        try:bounds[k.arg]=ast.literal_eval(k.value)
                        except (ValueError,TypeError):self.fail(n,'parameter bounds must be literals')
                    value=value.args[0];role=PARAM
                if isinstance(value,ast.Call):typ,bits=C,self.colour_literal(value)
                else:
                    try:lit=ast.literal_eval(value)
                    except (ValueError,TypeError):self.fail(n,'declarations require literals')
                    r=self.literal(lit);typ,_,bits=self.regs[r]
                reg=self.reg(typ,role,bits);self.names[name]=reg
                if role==PARAM:
                    if len(self.params)>=8 or not re.fullmatch(r'[A-Za-z0-9_-]{1,32}',name):self.fail(n,'invalid parameter name/count')
                    default=lit if typ==F else bool(bits) if typ==B else bits if typ==C else struct.unpack('<i',struct.pack('<I',bits))[0]
                    for key,bound in bounds.items():
                        if typ not in (I,F) or type(bound) not in (int,float) or (typ==I and type(bound)!=int):self.fail(n,'bounds require matching numeric types')
                        self.literal(bound)
                        if (key=='minimum' and default<bound) or (key=='maximum' and default>bound):self.fail(n,'default is outside parameter bounds')
                    self.params.append({'name':name,'type':{I:'int',F:'float',C:'rgb8',B:'bool'}[typ],'default':default,**bounds})
            elif not (isinstance(n,ast.Expr) and isinstance(n.value,ast.Constant) and isinstance(n.value.value,str)):
                self.fail(n,'unsupported module declaration')
        if 'shade' not in self.funcs:raise CompileError('a @shade function is required')
        step_writes=set();frame_writes=set()
        for role,fn in self.funcs.items():
            writes={s for n in ast.walk(fn) if isinstance(n,ast.Global) for s in n.names}
            if role=='step':step_writes=writes
            elif role=='prepare':frame_writes=writes
            elif writes:self.fail(fn,'shade cannot mutate globals')
        if any(name not in self.names for name in step_writes|frame_writes):raise CompileError('undeclared mutable global')
        if step_writes & frame_writes:raise CompileError('simulation and frame state must be distinct')
        for name in frame_writes:
            if name not in self.names:raise CompileError('undeclared frame state')
            r=self.names[name];t,role,bits=self.regs[r]
            if role!=STATE:raise CompileError('prepare cannot mutate inputs or parameters')
            self.regs[r]=(t,FRAME,bits)
        entries=[]
        for self.stage,role in enumerate(('step','prepare','shade')):
            entries.append(len(self.code));self.env=dict(self.names)
            fn=self.funcs.get(role);self.writes={s for n in ast.walk(fn) if isinstance(n,ast.Global) for s in n.names} if fn else set()
            if fn:self.statements(fn.body)
            if role=='shade' and not self.kernel:self.emit('RETURN',self.colour_constant(0))
            else:self.emit('END')
        if len(self.code)>2048:raise CompileError('code limit exceeded')
        if len(self.table)>1024:raise CompileError('palette storage limit exceeded')
        manifest={'schema':'lightvm.program/3','abi':2,'pixels':{'format':'rgb8'},'parameters':self.params}
        metadata=json.dumps(manifest,separators=(',',':'),allow_nan=False).encode()
        header=struct.pack('<4s15I',b'LBC1',2,len(self.regs),len(self.code),len(self.table),*entries,2 if self.kernel else 1,len(metadata),0,0,0,0,0,0)
        raw=header+b''.join(struct.pack('<BBHI',t,role,0,v) for t,role,v in self.regs)
        raw+=b''.join(struct.pack('<BBBBI',*ins) for ins in self.code)
        raw+=b''.join(struct.pack('<I',c) for c in self.table)
        raw+=metadata
        if len(raw)>32768:raise CompileError('package limit exceeded')
        return raw,manifest

    def colour_literal(self,n):
        if not isinstance(n,ast.Call) or getattr(n.func,'id','')!='rgb' or len(n.args)!=3 or n.keywords:self.fail(n,'palette colours require rgb literals')
        try:parts=[ast.literal_eval(a) for a in n.args]
        except (ValueError,TypeError):self.fail(n,'colour channels must be literals')
        if any(type(v)!=int or not 0<=v<=255 for v in parts):self.fail(n,'RGB channels must be 0..255')
        return parts[0]<<16|parts[1]<<8|parts[2]

    def colour_constant(self,v):
        return self.reg(C,CONST,v)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('-o','--output',type=Path,required=True);p.add_argument('--no-fusion',action='store_true');a=p.parse_args()
    raw,manifest=Compiler(a.source.read_text(),not a.no_fusion).compile();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(raw);a.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest))
