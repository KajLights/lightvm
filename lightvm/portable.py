"""Opt-in animation/1 frontend for the general LBC1/3 instruction container.

The function table carries explicit permissions/result types; animation entry
selection is profile metadata. No source code runs during compilation.
"""
import ast
import json
import math
import struct
from .bytecode import Compiler, CompileError, OP, I, F, C, B, LOCAL, INPUT, CONST, STATE, FRAME, PARAM

STAMP, REF, SETTING = 5, 6, 6
P_OP = {**OP, 'LOAD_ELEM': 33, 'STORE_ELEM': 34, 'LENGTH': 35,
        'RESOURCE_FILL': 36, 'RESOURCE_COPY': 37, 'NATIVE': 38}
# name: binding ID, result type, argument types, required effect permissions
NATIVES = {
    'now': (1, STAMP, (), 0), 'elapsed': (2, F, (STAMP,), 0),
    'uniform': (3, F, (REF,), 8), 'try_index': (4, I, (REF, I), 8),
    'cycle_complete': (5, 0, (I,), 16), 'complete': (6, 0, (), 16),
    'sin': (7, F, (F,), 0), 'red': (8, I, (C,), 0),
    'green': (9, I, (C,), 0), 'blue': (10, I, (C,), 0),
    'colorwheel': (11, C, (F,), 0), 'pico_colorwheel': (12, C, (I,), 0),
    'hue_degrees': (13, F, (C,), 0), 'require': (14, 0, (B,), 0),
}
KINDS = {'i32': (1, I), 'f32': (2, F), 'rgb8': (3, C), 'bool8': (4, B),
         'stamp': (5, STAMP), 'u8': (7, I), 'u16': (8, I)}
EXPR_OPS = {ast.Add: 3, ast.Sub: 4, ast.Mult: 5, ast.FloorDiv: 6, ast.Mod: 7, ast.Div: 8}


def is_portable(source):
    tree = ast.parse(source)
    return any(isinstance(n, ast.Expr) and isinstance(n.value, ast.Call)
               and getattr(n.value.func, 'id', '') == 'profile' for n in tree.body)


class PortableCompiler(Compiler):
    def __init__(self, source):
        super().__init__(source, fuse=False)
        self.resources, self.settings, self.expressions, self.calls = [], [], [], []
        self.resource_names, self.setting_names = {}, {}
        self.helpers, self.call_stack, self.return_stack, self.loop_stack = {}, [], [], []
        self.rights = 0
        self.expressions.append((0, I, 0, 0, 0))
        count = self.declarative(ast.Name(id='count'))
        black = self.declarative(ast.Call(func=ast.Name(id='rgb'), args=[ast.Constant(value=0)]*3, keywords=[]))
        self.add_resource('pixels', 3, count, black)
        self.period, self.catch_up, self.first = 0, 4, 1

    def reg(self, typ, role=LOCAL, value=0):
        if len(self.regs) >= 4096:
            raise CompileError('virtual register limit exceeded')
        self.regs.append((typ, role, value))
        return len(self.regs) - 1

    def typ(self, register):
        if register < 0: raise CompileError('void operation cannot be used as a value')
        return super().typ(register)

    def emit(self, op, a=0, b=0, c=0, imm=0):
        self.code.append([P_OP[op], a, b, c, imm])
        return len(self.code) - 1

    def declarative(self, node):
        """Checked acyclic numeric expressions, evaluated by native admission."""
        if isinstance(node, ast.Constant):
            r = self.literal(node.value)
            typ, _, bits = self.regs[r]
            value = (0, typ, 0, 0, bits)
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub) and isinstance(node.operand, ast.Constant):
            return self.declarative(ast.Constant(value=-node.operand.value))
        elif isinstance(node, ast.Name) and node.id == 'count':
            value = (1, I, 0, 0, 0)
        elif isinstance(node, ast.Name) and node.id in self.setting_names:
            r = self.names[node.id]
            value = (2, self.typ(r), r, 0, 0)
        elif isinstance(node, ast.Call) and getattr(node.func, 'id', '') == 'rgb':
            value = (0, C, 0, 0, self.colour_literal(node))
        elif isinstance(node, ast.BinOp) and type(node.op) in EXPR_OPS:
            a, b = self.declarative(node.left), self.declarative(node.right)
            ta, tb = self.expressions[a][1], self.expressions[b][1]
            if ta not in (I, F) or tb not in (I, F): self.fail(node, 'numeric declaration required')
            typ = F if F in (ta, tb) or isinstance(node.op, ast.Div) else I
            if isinstance(node.op, ast.FloorDiv) and typ != I: self.fail(node, 'integer floor division required')
            value = (EXPR_OPS[type(node.op)], typ, a, b, 0)
        elif isinstance(node, ast.Call) and getattr(node.func, 'id', '') in ('min', 'max') and len(node.args) == 2 and not node.keywords:
            a, b = map(self.declarative, node.args)
            ta, tb = self.expressions[a][1], self.expressions[b][1]
            if ta not in (I, F) or tb not in (I, F): self.fail(node, 'numeric declaration required')
            value = (9 if node.func.id == 'min' else 10, F if F in (ta, tb) else I, a, b, 0)
        else:
            self.fail(node, 'declaration needs literals, count or earlier settings')
        if value in self.expressions: return self.expressions.index(value)
        if len(self.expressions) >= 128: self.fail(node, 'declaration expression limit exceeded')
        self.expressions.append(value)
        return len(self.expressions) - 1

    def add_resource(self, name, kind, length, fill=0, seed=0, stream=0):
        if len(self.resources) >= 16: raise CompileError('resource limit exceeded')
        r = self.reg(REF, CONST, len(self.resources))
        self.names[name] = r
        self.resource_names[name] = (len(self.resources), kind)
        self.resources.append((kind, length, fill, seed, stream, int(name == 'pixels')))
        return r

    def native(self, name, args, node):
        binding, result, types, rights = NATIVES[name]
        if len(args) != len(types) or rights & ~self.rights:
            self.fail(node, 'native call arguments or stage permissions do not match')
        args = [self.convert(r, t) for r, t in zip(args, types)]
        if name in ('uniform', 'try_index'):
            r = args[0]
            if self.regs[r][1] != CONST or self.resources[self.regs[r][2]][0] != 9:
                self.fail(node, 'owned RNG reference required')
        out = self.reg(result) if result else -1
        site = len(self.calls)
        self.calls.append((binding, args))
        self.emit('NATIVE', out, imm=site)
        return out

    def resource(self, node):
        if isinstance(node, ast.Name) and node.id in self.env and self.typ(self.env[node.id]) == REF:
            r = self.env[node.id]
            return r, self.resources[self.regs[r][2]][0]
        self.fail(node, 'statically typed resource reference required')

    def check_write(self, ref, node):
        identity = self.regs[ref][2]
        if self.resources[identity][0] in (9, 10) or not self.rights & (4 if identity == 0 else 1):
            self.fail(node, 'stage cannot mutate this resource')

    def expr(self, n):
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.Not):
            r = self.expr(n.operand)
            if self.typ(r) != B: self.fail(n, 'not requires bool')
            out = self.reg(B); self.emit('EQ', out, r, self.literal(False)); return out
        if isinstance(n, ast.Compare) and len(n.ops) == 1:
            a, b = self.expr(n.left), self.expr(n.comparators[0])
            op = n.ops[0]
            if isinstance(op, (ast.Eq, ast.NotEq)) and self.typ(a) == self.typ(b):
                if self.typ(a) == REF: self.fail(n, 'resource comparisons are not supported')
                out = self.reg(B); self.emit('EQ', out, a, b)
            else:
                a, b, typ = self.pair(a, b)
                if isinstance(op, (ast.Eq, ast.NotEq)):
                    out = self.reg(B); self.emit('EQ', out, a, b)
                    if isinstance(op, ast.NotEq):
                        other = self.reg(B); self.emit('EQ', other, out, self.literal(False)); out = other
                    return out
                if isinstance(op, (ast.Gt, ast.LtE)): a, b = b, a
                if not isinstance(op, (ast.Lt, ast.Gt, ast.LtE, ast.GtE)): self.fail(n, 'unsupported comparison')
                out = self.reg(B); self.emit('FLT' if typ == F else 'ILT', out, a, b)
            if isinstance(op, (ast.NotEq, ast.LtE, ast.GtE)):
                negated = self.reg(B); self.emit('EQ', negated, out, self.literal(False)); out = negated
            return out
        if isinstance(n, ast.BoolOp):
            # Short-circuit with bool-only operands.
            out = self.reg(B)
            for i, item in enumerate(n.values):
                r = self.expr(item)
                if self.typ(r) != B: self.fail(n, 'boolean operands required')
                self.emit('MOV', out, r)
                if i == len(n.values) - 1: break
                if isinstance(n.op, ast.Or):
                    inv = self.reg(B); self.emit('EQ', inv, out, self.literal(False)); r = inv
                jump = self.emit('JZ', r)
                # Recurse over the tail to make one short-circuit region.
                tail = n.values[i+1:]
                other = self.expr(tail[0] if len(tail) == 1 else ast.BoolOp(op=n.op, values=tail))
                self.emit('MOV', out, other); self.code[jump][4] = len(self.code); break
            return out
        if isinstance(n, ast.Subscript):
            ref, kind = self.resource(n.value)
            if kind == 9: self.fail(n, 'RNG is not an array')
            if self.regs[ref][2] == 0 and not self.rights & 4: self.fail(n, 'only kernels may read working pixels')
            index = self.expr(n.slice)
            if self.typ(index) != I: self.fail(n, 'index must be i32')
            typ = I if kind in (7, 8) else C if kind == 10 else kind
            out = self.reg(typ); self.emit('LOAD_ELEM', out, ref, index); return out
        if isinstance(n, ast.Call):
            if isinstance(n.func, ast.Attribute) and n.func.attr in ('uniform', 'try_index'):
                if n.keywords: self.fail(n, 'random helper takes positional arguments')
                ref, kind = self.resource(n.func.value)
                if kind != 9: self.fail(n, 'owned RNG required')
                return self.native(n.func.attr, [ref] + [self.expr(a) for a in n.args], n)
            name = getattr(n.func, 'id', '')
            if name == 'len' and len(n.args) == 1 and not n.keywords:
                ref, kind = self.resource(n.args[0])
                if kind == 9: self.fail(n, 'RNG has no array length')
                out = self.reg(I); self.emit('LENGTH', out, ref); return out
            if name in NATIVES:
                args = n.args
                if name == 'cycle_complete':
                    if n.keywords:
                        if n.args or len(n.keywords) != 1 or n.keywords[0].arg != 'count': self.fail(n, 'invalid cycle count')
                        args = [n.keywords[0].value]
                    elif not args: args = [ast.Constant(value=1)]
                elif n.keywords: self.fail(n, 'native helper takes positional arguments')
                return self.native(name, [self.expr(a) for a in args], n)
            if name in self.helpers:
                if n.keywords or name in self.call_stack or len(self.call_stack) >= 8: self.fail(n, 'bounded nonrecursive positional helper required')
                fn = self.helpers[name]
                if len(n.args) != len(fn.args.args): self.fail(n, 'helper argument count mismatch')
                def returns(nodes):
                    return any(isinstance(item, ast.Return) and item.value is not None or
                               isinstance(item, ast.If) and returns(item.body) and returns(item.orelse)
                               for item in nodes)
                if not returns(fn.body): self.fail(fn, 'helper must return a value on every path')
                args = [self.expr(a) for a in n.args]
                previous_env, previous_writes, previous_loops = self.env, self.writes, self.loop_stack
                self.env = dict(self.names); self.loop_stack = []
                for formal, actual in zip(fn.args.args, args):
                    local = actual if self.typ(actual) == REF else self.reg(self.typ(actual))
                    if local != actual: self.emit('MOV', local, actual)
                    self.env[formal.arg] = local
                self.writes = {s for item in ast.walk(fn) if isinstance(item, ast.Global) for s in item.names}
                if self.writes & {a.arg for a in fn.args.args}: self.fail(fn, 'argument cannot also be global')
                self.call_stack.append(name); returns = []; self.return_stack.append(returns)
                self.statements(fn.body)
                self.return_stack.pop(); self.call_stack.pop()
                if not returns: self.fail(fn, 'helper must return a value')
                out = returns[0][1]
                for jump, result in returns:
                    if result != out: self.fail(fn, 'inconsistent helper return')
                    self.code[jump][4] = len(self.code)
                self.env, self.writes, self.loop_stack = previous_env, previous_writes, previous_loops
                return out
        return super().expr(n)

    def assign(self, name, value, node):
        if name in self.names and self.env.get(name) == self.names[name]:
            if name not in self.writes: self.fail(node, 'assignment requires global declaration')
            target = self.names[name]; role = self.regs[target][1]
            if not ((role == STATE and self.rights & 1) or (role == FRAME and self.rights & 2)):
                self.fail(node, 'stage cannot mutate this global')
        else:
            target = self.env.get(name)
            if target is None: target = self.reg(self.typ(value)); self.env[name] = target
        if self.typ(target) != self.typ(value) or self.typ(value) == REF: self.fail(node, 'assignment changes type or resource binding')
        self.emit('MOV', target, value)

    def statements(self, nodes):
        for n in nodes:
            if isinstance(n, (ast.Global, ast.Pass)) or isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str): continue
            if isinstance(n, ast.Assign) and len(n.targets) == 1:
                target = n.targets[0]
                if isinstance(target, ast.Tuple) and len(target.elts) == 2 and all(isinstance(t, ast.Name) for t in target.elts) and isinstance(n.value, ast.Call) and isinstance(n.value.func, ast.Attribute) and n.value.func.attr == 'try_index':
                    rank = self.expr(n.value); ok = self.reg(B)
                    self.emit('ILT', ok, self.literal(-1), rank)
                    self.assign(target.elts[0].id, ok, n)
                    clean = self.reg(I); self.emit('IMAX', clean, rank, self.literal(0)); self.assign(target.elts[1].id, clean, n)
                elif isinstance(target, ast.Name): self.assign(target.id, self.expr(n.value), n)
                elif isinstance(target, ast.Subscript):
                    ref, kind = self.resource(target.value); self.check_write(ref, n)
                    at, val = self.expr(target.slice), self.expr(n.value)
                    typ = I if kind in (7, 8) else kind
                    if self.typ(at) != I or self.typ(val) != typ: self.fail(n, 'indexed store type mismatch')
                    self.emit('STORE_ELEM', ref, at, val)
                else: self.fail(n, 'unsupported assignment')
            elif isinstance(n, ast.AugAssign) and isinstance(n.target, ast.Name):
                self.assign(n.target.id, self.binary(n.op, self.expr(n.target), self.expr(n.value)), n)
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call):
                call = n.value
                if isinstance(call.func, ast.Attribute) and call.func.attr in ('fill', 'copy_from'):
                    ref, kind = self.resource(call.func.value); self.check_write(ref, n)
                    if len(call.args) != 1: self.fail(n, 'one positional argument required')
                    if call.func.attr == 'copy_from':
                        other, other_kind = self.resource(call.args[0])
                        if self.regs[other][2] == 0 and not self.rights & 4: self.fail(n, 'only kernels may read working pixels')
                        if call.keywords or kind != (C if other_kind == 10 else other_kind): self.fail(n, 'copy requires matching resource types')
                        self.emit('RESOURCE_COPY', ref, other)
                    else:
                        kws = {k.arg: k.value for k in call.keywords}
                        if len(kws) != len(call.keywords) or set(kws) - {'start', 'stop'}: self.fail(n, 'invalid fill range')
                        val = self.expr(call.args[0]); typ = I if kind in (7, 8) else kind
                        if self.typ(val) != typ: self.fail(n, 'fill type mismatch')
                        start = self.expr(kws['start']) if 'start' in kws else self.literal(0)
                        stop = self.expr(kws['stop']) if 'stop' in kws else self.reg(I)
                        if 'stop' not in kws: self.emit('LENGTH', stop, ref)
                        if self.typ(start) != I or self.typ(stop) != I: self.fail(n, 'fill range must be i32')
                        self.emit('RESOURCE_FILL', ref, val, start, stop)
                else: self.expr(call)
            elif isinstance(n, ast.For) and isinstance(n.target, ast.Name) and isinstance(n.iter, ast.Call) and getattr(n.iter.func, 'id', '') == 'range' and len(n.iter.args) == 1 and not n.iter.keywords and not n.orelse:
                if n.target.id in self.names: self.fail(n, 'loop variable shadows global')
                stop = self.expr(n.iter.args[0])
                if self.typ(stop) != I: self.fail(n, 'range bound must be i32')
                bound = self.reg(I); self.emit('MOV', bound, stop)
                loop = self.reg(I); self.env[n.target.id] = loop; self.emit('MOV', loop, self.literal(0))
                head = len(self.code); cond = self.reg(B); self.emit('ILT', cond, loop, bound); end = self.emit('JZ', cond)
                breaks, continues = [], []; self.loop_stack.append((breaks, continues)); self.statements(n.body); self.loop_stack.pop()
                for j in continues: self.code[j][4] = len(self.code)
                self.emit('IADD', loop, loop, self.literal(1)); self.emit('JMP', imm=head)
                for j in breaks + [end]: self.code[j][4] = len(self.code)
            elif isinstance(n, (ast.Break, ast.Continue)):
                if not self.loop_stack: self.fail(n, 'loop control outside loop')
                self.loop_stack[-1][isinstance(n, ast.Continue)].append(self.emit('JMP'))
            elif isinstance(n, ast.Return):
                if self.return_stack:
                    if n.value is None: self.fail(n, 'helper must return a value')
                    val = self.expr(n.value); returns = self.return_stack[-1]
                    out = returns[0][1] if returns else self.reg(self.typ(val))
                    if self.typ(out) != self.typ(val): self.fail(n, 'helper return type mismatch')
                    self.emit('MOV', out, val); returns.append((self.emit('JMP'), out))
                elif self.stage == 2 and not self.kernel:
                    val = self.expr(n.value)
                    if self.typ(val) != C: self.fail(n, 'shade must return RGB')
                    self.emit('RETURN', val)
                elif n.value is None: self.emit('END')
                else: self.fail(n, 'void entry cannot return a value')
            elif isinstance(n, ast.If):
                cond = self.expr(n.test)
                if self.typ(cond) != B: self.fail(n, 'condition must be bool')
                jump = self.emit('JZ', cond); before = dict(self.env); self.statements(n.body)
                end = self.emit('JMP'); self.code[jump][4] = len(self.code); self.env = dict(before)
                self.statements(n.orelse); self.code[end][4] = len(self.code); self.env = before
            else: self.fail(n, 'unsupported statement in portable profile')

    def compile(self):
        allowed = set(NATIVES) | set(KINDS) | {'profile', 'setting', 'parameter', 'array', 'pixel_buffer', 'rng', 'rgb', 'hsv', 'hash01', 'init', 'step', 'prepare', 'shade', 'kernel'}
        marked = False
        for n in self.tree.body:
            if isinstance(n, ast.ImportFrom):
                if n.module != 'lightvm' or n.level or any(a.name not in allowed or a.asname for a in n.names): self.fail(n, 'only explicit lightvm imports supported')
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and getattr(n.value.func, 'id', '') == 'profile':
                if marked or n.value.keywords or len(n.value.args) != 1 or not isinstance(n.value.args[0], ast.Constant) or n.value.args[0].value != 'animation/1': self.fail(n, 'expected one animation/1 profile declaration')
                marked = True
            elif isinstance(n, ast.FunctionDef):
                if n.args.posonlyargs or n.args.kwonlyargs or n.args.vararg or n.args.kwarg or n.args.defaults: self.fail(n, 'simple positional functions required')
                if not n.decorator_list:
                    if n.name in self.helpers or n.name in self.names or n.name in allowed: self.fail(n, 'duplicate helper name')
                    self.helpers[n.name] = n; continue
                if len(n.decorator_list) != 1 or n.args.args: self.fail(n, 'entry must have no arguments and one decorator')
                decorator = n.decorator_list[0]; role = getattr(decorator.func if isinstance(decorator, ast.Call) else decorator, 'id', '')
                if role not in ('init', 'step', 'prepare', 'shade', 'kernel'): self.fail(n, 'unknown animation entry')
                if role == 'step':
                    if not isinstance(decorator, ast.Call) or decorator.args: self.fail(n, 'step requires explicit period_us')
                    kws = {k.arg: k.value for k in decorator.keywords}
                    if len(kws) != len(decorator.keywords) or 'period_us' not in kws or set(kws) - {'period_us', 'first', 'max_catch_up', 'overload'}: self.fail(n, 'invalid tick policy')
                    self.period = self.declarative(kws['period_us'])
                    if self.expressions[self.period][1] != I: self.fail(n, 'integer period required')
                    if self.expressions[self.period][0] == 0 and not 1000 <= self.expressions[self.period][4] <= 1000000:
                        self.fail(n, 'period_us must be in 1000..1000000')
                    try:
                        first = ast.literal_eval(kws.get('first', ast.Constant(value='immediate')))
                        self.catch_up = ast.literal_eval(kws.get('max_catch_up', ast.Constant(value=4)))
                        overload = ast.literal_eval(kws.get('overload', ast.Constant(value='slow')))
                    except (ValueError, TypeError): self.fail(n, 'tick policy requires literals')
                    if first not in ('immediate', 'after_period') or type(self.catch_up) != int or not 1 <= self.catch_up <= 64 or overload != 'slow': self.fail(n, 'unsupported tick policy')
                    self.first = int(first == 'immediate')
                elif isinstance(decorator, ast.Call): self.fail(n, 'this decorator takes no arguments')
                if role == 'kernel': self.kernel = True; role = 'shade'
                if role in self.funcs: self.fail(n, 'duplicate animation entry')
                self.funcs[role] = n
            elif isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
                name, val = n.targets[0].id, n.value
                if name in self.names or name in allowed or name in self.helpers: self.fail(n, 'duplicate/reserved declaration')
                call = getattr(val.func, 'id', '') if isinstance(val, ast.Call) else ''
                if call in ('array', 'pixel_buffer', 'rng'):
                    kws = {k.arg: k.value for k in val.keywords}
                    if len(kws) != len(val.keywords): self.fail(n, 'duplicate resource argument')
                    if call == 'rng':
                        if val.args or set(kws) - {'seed', 'stream'}: self.fail(n, 'rng takes seed and stream')
                        seed = self.declarative(kws.get('seed', ast.Constant(value=1))); stream = self.declarative(kws.get('stream', ast.Constant(value=0)))
                        if any(self.expressions[e][1] != I for e in (seed, stream)): self.fail(n, 'integer RNG seed and stream required')
                        self.add_resource(name, 9, 0, 0, seed, stream)
                    else:
                        kind = 3
                        if call == 'array':
                            if len(val.args) != 1 or getattr(val.args[0], 'id', '') not in KINDS: self.fail(n, 'array requires an element type')
                            kind = KINDS[val.args[0].id][0]
                        elif val.args: self.fail(n, 'pixel_buffer uses keyword arguments')
                        if 'length' not in kws or set(kws) - {'length', 'fill'}: self.fail(n, 'resource needs length and optional fill')
                        length = self.declarative(kws['length'])
                        if self.expressions[length][1] != I: self.fail(n, 'integer capacity required')
                        default = ast.Call(func=ast.Name(id='rgb'), args=[ast.Constant(value=0)]*3, keywords=[]) if kind == C else ast.Constant(value=False if kind == B else 0.0 if kind == F else 0)
                        fill = self.declarative(kws.get('fill', default))
                        self.add_resource(name, kind, length, fill)
                elif call == 'setting':
                    if len(val.args) != 1 or any(k.arg not in ('minimum', 'maximum') for k in val.keywords): self.fail(n, 'invalid setting declaration')
                    default = self.declarative(val.args[0]); typ = self.expressions[default][1]
                    bounds = {k.arg: self.declarative(k.value) for k in val.keywords}
                    if len(bounds) != len(val.keywords) or bounds and typ not in (I, F): self.fail(n, 'invalid setting bounds')
                    if any(self.expressions[e][1] != typ for e in bounds.values()): self.fail(n, 'setting bounds must match its type')
                    reg = self.reg(typ, SETTING); self.names[name] = reg
                    self.setting_names[name] = len(self.settings)
                    self.settings.append((name, reg, default, bounds.get('minimum', 0xffffffff), bounds.get('maximum', 0xffffffff)))
                    if len(self.settings) > 16: self.fail(n, 'setting limit exceeded')
                elif isinstance(val, ast.Tuple):
                    start = len(self.table)
                    self.table.extend(self.colour_literal(item) for item in val.elts)
                    if not val.elts: self.fail(n, 'empty palette')
                    length = self.declarative(ast.Constant(value=len(val.elts)))
                    offset = self.declarative(ast.Constant(value=start))
                    self.add_resource(name, 10, length, offset)
                else:
                    bounds = {}; role = STATE
                    if call == 'parameter':
                        if len(val.args) != 1 or any(k.arg not in ('minimum', 'maximum') for k in val.keywords): self.fail(n, 'invalid parameter')
                        bounds = {k.arg: ast.literal_eval(k.value) for k in val.keywords}; val = val.args[0]; role = PARAM
                    if isinstance(val, ast.Call): typ, bits = C, self.colour_literal(val); default = bits
                    else:
                        default = ast.literal_eval(val); r = self.literal(default); typ, _, bits = self.regs[r]
                    reg = self.reg(typ, role, bits); self.names[name] = reg
                    if role == PARAM:
                        if len(self.params) >= 8: self.fail(n, 'parameter limit exceeded')
                        for k, v in bounds.items():
                            if typ not in (I, F) or type(v) not in (int, float) or typ == I and type(v) != int: self.fail(n, 'invalid parameter bounds')
                            self.literal(v)
                            if k == 'minimum' and default < v or k == 'maximum' and default > v: self.fail(n, 'default outside bounds')
                        self.params.append({'name': name, 'type': {I:'int',F:'float',C:'rgb8',B:'bool'}[typ], 'default': default, **bounds})
            elif not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)): self.fail(n, 'unsupported module declaration')
        if not marked or 'shade' not in self.funcs: raise CompileError('animation/1 requires a shade or kernel entry')
        state_writes, frame_writes = set(), set()
        def global_effects(fn, seen):
            if fn.name in seen: self.fail(fn, 'recursive helpers are not supported')
            seen = seen | {fn.name}
            writes = {s for item in ast.walk(fn) if isinstance(item, ast.Global) for s in item.names}
            for item in ast.walk(fn):
                if isinstance(item, ast.Call) and getattr(item.func, 'id', '') in self.helpers:
                    writes |= global_effects(self.helpers[item.func.id], seen)
            return writes
        for role, fn in self.funcs.items():
            writes = global_effects(fn, set())
            if role in ('init', 'step'): state_writes |= writes
            if role == 'prepare': frame_writes |= writes
            if role == 'shade' and writes: self.fail(fn, 'render cannot mutate globals')
        if state_writes & frame_writes: raise CompileError('simulation and frame state overlap')
        for name in frame_writes:
            if name not in self.names: raise CompileError('undeclared frame global')
            reg = self.names[name]; typ, role, value = self.regs[reg]
            if role != STATE: raise CompileError('prepare cannot mutate this binding')
            self.regs[reg] = (typ, FRAME, value)
        functions = []
        for self.stage, role, self.rights in ((0,'step',25), (1,'prepare',2), (2,'shade',4 if self.kernel else 0), (3,'init',9)):
            start = len(self.code); fn = self.funcs.get(role); self.env = dict(self.names)
            self.writes = {s for item in ast.walk(fn) if isinstance(item, ast.Global) for s in item.names} if fn else set()
            if fn: self.statements(fn.body)
            result = C if role == 'shade' and not self.kernel else 0
            if result: self.emit('RETURN', self.colour_constant(0))
            else: self.emit('END')
            functions.append((start, len(self.code), self.rights, result, 0, 0))
        if len(self.code) > 2048: raise CompileError('code limit exceeded')
        self.allocate(functions)
        if len(self.code) > 2048 or len(self.table) > 1024 or len(self.calls) > 256: raise CompileError('portable package limits exceeded')
        manifest = {'schema':'lightvm.program/4','abi':3,'profile':'animation/1','pixels':{'format':'rgb8'},'parameters':self.params,
                    'settings':[{'name':n,'type':{I:'int',F:'float',C:'rgb8',B:'bool'}[self.typ(r)],'register':r} for n,r,*_ in self.settings]}
        metadata = json.dumps(manifest,separators=(',',':'),allow_nan=False).encode()
        header = struct.pack('<4s23I', b'LBC1', 3, len(self.regs), len(self.code), len(self.table), len(functions), len(self.resources), len(self.expressions), len(self.calls), len(self.settings),
                             0, 1, 2, 3, 2 if self.kernel else 1, self.period, self.catch_up, self.first, 1, len(metadata), 0, 0, 0, 0)
        raw = header + b''.join(struct.pack('<BBHI',t,role,0,v) for t,role,v in self.regs)
        raw += b''.join(struct.pack('<BBBBI',*ins) for ins in self.code)
        raw += b''.join(struct.pack('<I',c) for c in self.table)
        raw += b''.join(struct.pack('<6I',*fn) for fn in functions)
        raw += b''.join(struct.pack('<6I',*resource) for resource in self.resources)
        raw += b''.join(struct.pack('<BBHIII',op,typ,0,a,b,v) for op,typ,a,b,v in self.expressions)
        raw += b''.join(struct.pack('<II4BI',binding,len(args),*(args+[0]*(4-len(args))),0) for binding,args in self.calls)
        raw += b''.join(struct.pack('<4I',r,d,lo,hi) for _,r,d,lo,hi in self.settings)
        raw += metadata
        if len(raw) > 32768 or len(metadata) > 8192: raise CompileError('package limit exceeded')
        return raw, manifest

    def allocate(self, functions):
        """Typed liveness allocation. Persistent/input/constant bindings never alias."""
        # Each operand position's register meaning; immediate targets stay intact.
        binary = {P_OP[x] for x in ('IADD','ISUB','IMUL','IDIV','IMOD','FADD','FSUB','FMUL','FDIV','FMOD','ILT','FLT','EQ','IMIN','IMAX','FMIN','FMAX','HASH')}
        unary = {P_OP[x] for x in ('MOV','ITOF','FTOI','LENGTH')}
        def operands(ins):
            op,a,b,c,d = ins
            if op in binary: return [1], [2,3]
            if op in unary: return [1], [2]
            if op in (P_OP['RGB'],P_OP['HSV']): return [1], [2,3,4]
            if op == P_OP['LOAD_ELEM']: return [1], [2,3]
            if op == P_OP['STORE_ELEM']: return [], [1,2,3]
            if op == P_OP['RESOURCE_FILL']: return [], [1,2,3,4]
            if op == P_OP['RESOURCE_COPY']: return [], [1,2]
            if op in (P_OP['RETURN'],P_OP['JZ']): return [], [1]
            if op == P_OP['NATIVE']: return [1] if a != -1 else [], []
            return [], []
        graph = [set() for _ in self.regs]
        for start,end,*_ in functions:
            live = [set() for _ in range(end-start)]
            changed = True
            while changed:
                changed = False
                for pc in range(end-1,start-1,-1):
                    ins = self.code[pc]; defs,uses = operands(ins)
                    following = [] if ins[0] in (P_OP['END'],P_OP['RETURN']) else [pc+1] if pc+1 < end else []
                    if ins[0] == P_OP['JMP']: following = [ins[4]]
                    elif ins[0] == P_OP['JZ']: following += [ins[4]]
                    after = set().union(*(live[j-start] for j in following))
                    defined = {ins[k] for k in defs}; used = {ins[k] for k in uses}
                    if ins[0] == P_OP['NATIVE']: used.update(self.calls[ins[4]][1])
                    before = used | (after-defined)
                    if before != live[pc-start]: live[pc-start] = before; changed = True
                    for r in defined:
                        for other in after - {r}: graph[r].add(other); graph[other].add(r)
            # Parameters/locals used before definition must not accidentally alias.
            for group in live:
                for r in group:
                    graph[r].update(group - {r})
        mapping, physical = {}, []
        for r,(typ,role,val) in enumerate(self.regs):
            if role != LOCAL: mapping[r] = len(physical); physical.append((typ,role,val))
        first_local = len(physical)
        for r in sorted((r for r in range(len(self.regs)) if r not in mapping),key=lambda r:-len(graph[r])):
            typ = self.regs[r][0]; occupied = {mapping[o] for o in graph[r] if o in mapping}
            available = next((p for p in range(first_local,len(physical)) if physical[p][0] == typ and p not in occupied),None)
            if available is None: available = len(physical); physical.append((typ,LOCAL,0))
            mapping[r] = available
        if len(physical) > 128: raise CompileError(f'register limit exceeded after liveness allocation ({len(physical)})')
        for ins in self.code:
            defs, uses = operands(ins)
            for k in defs+uses: ins[k] = mapping[ins[k]]
            if ins[0] == P_OP['NATIVE'] and ins[1] == -1: ins[1] = 255
        self.calls = [(binding,[mapping[r] for r in args]) for binding,args in self.calls]
        self.expressions = [(op,typ,mapping[a] if op == 2 else a,b,v) for op,typ,a,b,v in self.expressions]
        self.settings = [(n,mapping[r],d,lo,hi) for n,r,d,lo,hi in self.settings]
        self.regs = physical
