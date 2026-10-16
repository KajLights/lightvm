"""Opt-in animation/1 frontend for the general LBC1/4 instruction container.

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
    'random': (3, F, (), 8), 'randrange': (4, I, (I, I, I), 8),
    'seed': (5, 0, (I,), 8), 'seed_default': (6, 0, (), 8),
    'sin': (7, F, (F,), 0), 'red': (8, I, (C,), 0),
    'green': (9, I, (C,), 0), 'blue': (10, I, (C,), 0),
    'colorwheel': (11, C, (F,), 0), 'pico_colorwheel': (12, C, (I,), 0),
    'hue_degrees': (13, F, (C,), 0),
    'color_int': (15, I, (C,), 0), 'int_color': (16, C, (I,), 0),
    'randint': (17, I, (I, I), 8),
    'rgba_fill': (18, 0, (REF, C, I), 4),
    'rgba_point': (19, 0, (REF, I, C, I), 4),
    'colorwheel_fill': (20, 0, (REF, F, F), 4),
    'sprites_advance': (21, 0, (REF, F), 1),
    'sprites_spawn': (22, B, (REF, C, F), 9),
    'sprites_draw': (23, 0, (REF, REF), 4),
    'sprites_active': (24, I, (REF,), 0),
    'gradient_fill': (25, 0, (REF, C, C), 4),
    'palette_fill': (26, 0, (REF, REF, F, F), 4),
}
ARRAY_TYPES = {'i': (1, I), 'f': (2, F), 'B': (7, I), 'H': (8, I), 'I': (11, I)}
PUBLIC = {'profile', 'setting', 'rgb', 'hsv', 'hash01', 'init', 'render', 'SpriteBatch',
          'red', 'green', 'blue', 'colorwheel', 'pico_colorwheel', 'hue_degrees'}
EXPR_OPS = {ast.Add: 3, ast.Sub: 4, ast.Mult: 5, ast.FloorDiv: 6, ast.Mod: 7, ast.Div: 8}


class PortableCompiler(Compiler):
    def __init__(self, source):
        super().__init__(source, fuse=False)
        # Runtime arguments are flattened into typed input registers. Context
        # fields and array bindings belong to this instance, never module globals.
        self.regs, self.names = [], {}
        self.inputs = {name: self.reg(typ, INPUT) for name, typ in
                       (('pixel_count', I), ('time', F), ('delta', F))}
        self.resources, self.settings, self.expressions, self.calls = [], [], [], []
        self.resource_names, self.setting_names = {}, {}
        self.helpers, self.call_stack, self.return_stack, self.loop_stack = {}, [], [], []
        self.fields, self.caps, self.declarations, self.imports = {}, {}, {}, {}
        self.rights, self.depth = 0, 0
        self.profile_name = 'animation/1'
        self.expressions.append((0, I, 0, 0, 0))
        count = len(self.expressions); self.expressions.append((1, I, 0, 0, 0))
        black = self.declarative(ast.Call(func=ast.Name(id='rgb'), args=[ast.Constant(value=0)]*3, keywords=[]))
        self.output = self.add_resource('__pixels', 3, count, black)

    def capability(self, node):
        return self.caps.get(node.id) if isinstance(node, ast.Name) else None

    def declaration_key(self, node):
        if isinstance(node, ast.Name): return node.id
        if isinstance(node, ast.Attribute) and self.capability(node.value) == 'ctx':
            return 'ctx.' + node.attr
        return None

    def truth(self, reg):
        if self.typ(reg) == B: return reg
        if self.typ(reg) not in (I, F, C): raise CompileError('condition requires a scalar')
        zero = self.literal(0.0 if self.typ(reg) == F else 0)
        if self.typ(reg) == C: reg = self.convert(reg, I)
        equal = self.reg(B); self.emit('EQ', equal, reg, zero)
        out = self.reg(B); self.emit('EQ', out, equal, self.literal(False)); return out

    def convert(self, reg, typ):
        if self.typ(reg) == C and typ == I: return self.native('color_int', [reg], None)
        if self.typ(reg) == I and typ == C: return self.native('int_color', [reg], None)
        return super().convert(reg, typ)

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
        elif isinstance(node, ast.Attribute) and self.capability(node.value) == 'runtime' and node.attr == 'pixel_count':
            value = (1, I, 0, 0, 0)
        elif self.declaration_key(node) in self.declarations:
            return self.declarations[self.declaration_key(node)]
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
            self.fail(node, 'declaration needs literals, runtime.pixel_count or earlier settings')
        if value in self.expressions: return self.expressions.index(value)
        if len(self.expressions) >= 128: self.fail(node, 'declaration expression limit exceeded')
        self.expressions.append(value)
        return len(self.expressions) - 1

    def add_resource(self, name, kind, length, fill=0, seed=0, stream=0):
        if len(self.resources) >= 16: raise CompileError('resource limit exceeded')
        r = self.reg(REF, CONST, len(self.resources))
        self.names[name] = r
        self.resource_names[name] = (len(self.resources), kind)
        self.resources.append((kind, length, fill, seed, stream, int(name == '__pixels')))
        return r

    def native(self, name, args, node):
        binding, result, types, rights = NATIVES[name]
        if len(args) != len(types) or rights & ~self.rights:
            self.fail(node, 'native call arguments or stage permissions do not match')
        args = [self.convert(r, t) for r, t in zip(args, types)]
        out = self.reg(result) if result else -1
        site = len(self.calls)
        self.calls.append((binding, args))
        self.emit('NATIVE', out, imm=site)
        return out

    def resource(self, node):
        if self.capability(node) == 'pixels': return self.output, 3
        if isinstance(node, ast.Attribute) and self.capability(node.value) == 'ctx':
            reg = self.fields.get(node.attr)
        elif isinstance(node, ast.Name): reg = self.env.get(node.id)
        else: reg = None
        if reg is not None and self.typ(reg) == REF:
            return reg, self.resources[self.regs[reg][2]][0]
        self.fail(node, 'managed array or pixels argument required')

    def check_write(self, ref, node):
        identity = self.regs[ref][2]
        if self.resources[identity][0] in (9, 10) or not self.rights & (4 if identity == 0 else 1):
            self.fail(node, 'stage cannot mutate this resource')

    def expr(self, n):
        if isinstance(n, ast.Attribute):
            cap = self.capability(n.value)
            if cap == 'ctx' and n.attr in self.fields: return self.fields[n.attr]
            if cap == 'runtime' and n.attr in self.inputs: return self.inputs[n.attr]
            if cap == 'pixels' and n.attr == 'count': return self.inputs['pixel_count']
            if isinstance(n.value, ast.Name) and self.imports.get(n.value.id) == 'math' and n.attr == 'pi': return self.literal(math.pi)
            if n.attr == 'active':
                ref, kind = self.resource(n.value)
                if kind == 13: return self.native('sprites_active', [ref], n)
            self.fail(n, 'unknown context field or caller argument property')
        if isinstance(n, ast.Name):
            if self.imports.get(n.id) == 'math.pi': return self.literal(math.pi)
            if self.capability(n): self.fail(n, 'caller objects may only be passed to helpers or accessed by property')
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute):
            module = self.imports.get(getattr(n.func.value, 'id', ''))
            if module == 'random': return self.random_call(n.func.attr, n)
            if module == 'math' and n.func.attr == 'sin':
                if n.keywords: self.fail(n, 'math.sin takes one positional argument')
                return self.native('sin', [self.expr(a) for a in n.args], n)
            if self.profile_name == 'layer/1':
                method = n.func.attr
                if self.capability(n.func.value) == 'pixels' and method in ('point', 'sprites', 'colorwheel', 'gradient', 'palette'):
                    if n.keywords: self.fail(n, 'native drawing takes positional arguments')
                    args = [self.output] + [self.expr(a) for a in n.args]
                    if method == 'point' and len(args) == 3: args.append(self.literal(255))
                    return self.native({'point':'rgba_point', 'sprites':'sprites_draw', 'colorwheel':'colorwheel_fill', 'gradient':'gradient_fill', 'palette':'palette_fill'}[method], args, n)
                if method in ('advance', 'spawn'):
                    ref, kind = self.resource(n.func.value)
                    if kind != 13 or n.keywords: self.fail(n, 'SpriteBatch method required')
                    return self.native('sprites_' + method, [ref] + [self.expr(a) for a in n.args], n)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.Not):
            r = self.truth(self.expr(n.operand))
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
                r = self.truth(self.expr(item))
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
            if kind in (9, 13): self.fail(n, 'resource is not an indexable array')
            if self.regs[ref][2] == 0 and not self.rights & 4: self.fail(n, 'initialization cannot access output pixels')
            index = self.expr(n.slice)
            if self.typ(index) != I: self.fail(n, 'index must be i32')
            typ = self.element_type(kind)
            out = self.reg(typ); self.emit('LOAD_ELEM', out, ref, index); return out
        if isinstance(n, ast.Call):
            name = getattr(n.func, 'id', '')
            imported = self.imports.get(name, '')
            if imported.startswith('random.'): return self.random_call(imported.split('.')[1], n)
            if name == 'len' and len(n.args) == 1 and not n.keywords:
                ref, kind = self.resource(n.args[0])
                out = self.reg(I); self.emit('LENGTH', out, ref); return out
            if imported == 'math.sin' or imported in NATIVES:
                if n.keywords: self.fail(n, 'helper takes positional arguments')
                return self.native('sin' if imported == 'math.sin' else imported, [self.expr(a) for a in n.args], n)
            if name in self.helpers:
                if n.keywords or name in self.call_stack or len(self.call_stack) >= 8: self.fail(n, 'bounded nonrecursive positional helper required')
                fn = self.helpers[name]
                if len(n.args) != len(fn.args.args): self.fail(n, 'helper argument count mismatch')
                def returns(nodes):
                    return any(isinstance(item, ast.Return) and item.value is not None or
                               isinstance(item, ast.If) and returns(item.body) and returns(item.orelse)
                               for item in nodes)
                if not returns(fn.body): self.fail(fn, 'helper must return a value on every path')
                args = [(self.capability(a), None) if self.capability(a) else (None, self.expr(a)) for a in n.args]
                previous_env, previous_writes, previous_loops, previous_caps = self.env, self.writes, self.loop_stack, self.caps
                self.env = {}; self.loop_stack = []; self.caps = {}
                for formal, (cap, actual) in zip(fn.args.args, args):
                    if cap:
                        self.caps[formal.arg] = cap; continue
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
                self.env, self.writes, self.loop_stack, self.caps = previous_env, previous_writes, previous_loops, previous_caps
                return out
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id not in {'int', 'float', 'min', 'max'} and self.imports.get(n.func.id) not in {'rgb', 'hsv', 'hash01'}:
            self.fail(n, 'unsupported or unimported function')
        return super().expr(n)

    def element_type(self, kind):
        return I if kind in (7, 8, 11) else C if kind == 10 else kind

    def random_call(self, name, node):
        args = list(node.args)
        kws = {k.arg: k.value for k in node.keywords}
        if len(kws) != len(node.keywords) or None in kws: self.fail(node, 'invalid random arguments')
        if name == 'seed':
            if len(args) > 2 or set(kws) - {'a', 'version'}: self.fail(node, 'random.seed(a=None, version=2) expected')
            if args and 'a' in kws or len(args) > 1 and 'version' in kws: self.fail(node, 'duplicate seed argument')
            version = args[1] if len(args) > 1 else kws.get('version', ast.Constant(value=2))
            if not isinstance(version, ast.Constant) or type(version.value) != int or version.value not in (1, 2): self.fail(node, 'seed version must be 1 or 2')
            seed = args[0] if args else kws.get('a', ast.Constant(value=None))
            if isinstance(seed, ast.Constant) and seed.value is None: return self.native('seed_default', [], node)
            reg = self.expr(seed)
            if self.typ(reg) != I: self.fail(node, 'supported seeds are integers or None')
            return self.native('seed', [reg], node)
        if name == 'random':
            if args or kws: self.fail(node, 'random.random() takes no arguments')
            return self.native('random', [], node)
        if name == 'randint':
            if len(args) > 2 or set(kws) - {'a', 'b'}: self.fail(node, 'random.randint(a, b) expected')
            for i, key in enumerate(('a', 'b')):
                if len(node.args) > i:
                    if key in kws: self.fail(node, 'duplicate randint argument')
                elif key in kws: args.append(kws[key])
                else: self.fail(node, 'random.randint needs both bounds')
        elif name == 'randrange':
            if kws or not 1 <= len(args) <= 3: self.fail(node, 'random.randrange(stop) or (start, stop[, step]) expected')
            if len(args) == 1: args.insert(0, ast.Constant(value=0))
            if len(args) == 2: args.append(ast.Constant(value=1))
        else: self.fail(node, 'unsupported random function')
        regs = [self.expr(a) for a in args]
        if any(self.typ(r) != I for r in regs): self.fail(node, 'random bounds and step must be integers')
        return self.native(name, regs, node)

    def assign(self, name, value, node):
        if name in self.caps or name in self.imports: self.fail(node, 'cannot rebind a caller argument or import')
        target = self.env.get(name)
        if target is None: target = self.reg(self.typ(value)); self.env[name] = target
        if self.regs[target][1] != LOCAL: self.fail(node, 'setting and array bindings are frozen')
        if self.typ(target) != self.typ(value) or self.typ(value) == REF: self.fail(node, 'assignment changes type or array binding')
        self.declarations.pop(name, None)
        self.emit('MOV', target, value)

    def assign_target(self, target, value, node):
        if isinstance(target, ast.Name): return self.assign(target.id, value, node)
        if isinstance(target, ast.Attribute) and self.capability(target.value) == 'ctx':
            reg = self.fields.get(target.attr)
            if reg is None:
                if self.stage != 0 or self.depth or self.call_stack: self.fail(node, 'create context fields unconditionally in @init')
                if self.typ(value) == REF: self.fail(node, 'array aliases are unsupported')
                reg = self.reg(self.typ(value), STATE); self.fields[target.attr] = reg
            if self.regs[reg][1] != STATE or self.typ(reg) == REF: self.fail(node, 'cannot rebind this context field')
            if self.typ(reg) != self.typ(value): self.fail(node, 'context field changes type')
            self.declarations.pop('ctx.' + target.attr, None)
            self.emit('MOV', reg, value); return
        if isinstance(target, ast.Subscript):
            ref, kind = self.resource(target.value); self.check_write(ref, node)
            at = self.expr(target.slice)
            if self.typ(at) != I: self.fail(node, 'index must be an integer')
            typ = self.element_type(kind)
            value = self.convert(value, typ)
            self.emit('STORE_ELEM', ref, at, value); return
        self.fail(node, 'only local variables, context fields and array elements are writable')

    def declaration(self, target, call, node):
        name = self.declaration_key(target)
        if self.stage != 0 or self.depth or self.call_stack or not name: self.fail(node, 'declare settings and arrays directly in @init')
        kind = self.imports.get(getattr(call.func, 'id', ''), '')
        if kind == 'SpriteBatch':
            if self.profile_name != 'layer/1' or len(call.args) != 1 or call.keywords: self.fail(node, 'SpriteBatch(capacity) requires layer/1')
            length = self.declarative(call.args[0])
            if self.expressions[length][1] != I: self.fail(node, 'sprite capacity must be an integer')
            if name.startswith('ctx.') and name[4:] in self.fields or not name.startswith('ctx.') and name in self.env: self.fail(node, 'duplicate sprite binding')
            reg = self.add_resource(name, 13, length, self.declarative(ast.Constant(value=0)))
            if name.startswith('ctx.'): self.fields[name[4:]] = reg
            else: self.env[name] = reg
            return
        if kind == 'setting':
            if len(call.args) != 1 or any(k.arg not in ('minimum', 'maximum') for k in call.keywords): self.fail(node, 'expected a default and optional minimum/maximum')
            external = name.removeprefix('ctx.')
            if len(external) > 32 or external in self.setting_names or any(p['name'] == external for p in self.params): self.fail(node, 'duplicate or invalid control name')
            default = self.declarative(call.args[0]); typ = self.expressions[default][1]
            bounds = {k.arg: self.declarative(k.value) for k in call.keywords}
            if len(bounds) != len(call.keywords) or bounds and typ not in (I, F): self.fail(node, 'invalid bounds')
            if any(self.expressions[e][1] != typ for e in bounds.values()): self.fail(node, 'bounds must match the default type')
            reg = self.reg(typ, SETTING)
            self.setting_names[external] = len(self.settings)
            self.settings.append((external, reg, default, bounds.get('minimum', 0xffffffff), bounds.get('maximum', 0xffffffff)))
            if len(self.settings) > 16: self.fail(node, 'setting limit exceeded')
            if name.startswith('ctx.'):
                if name[4:] in self.fields: self.fail(node, 'setting field already exists')
                self.fields[name[4:]] = reg
            else:
                if name in self.env or name in self.caps or name in self.imports: self.fail(node, 'setting binding already exists')
                self.env[name] = reg
            self.declarations[name] = len(self.expressions); self.expressions.append((2, typ, reg, 0, 0)); return
        if kind != 'array.array' or len(call.args) != 2 or call.keywords: self.fail(node, 'array(typecode, initializer) required')
        code, values = call.args
        if not isinstance(code, ast.Constant) or code.value not in ARRAY_TYPES: self.fail(node, 'supported array typecodes: i, I, f, B, H')
        resource_kind, typ = ARRAY_TYPES[code.value]
        repeated = isinstance(values, ast.BinOp) and isinstance(values.op, ast.Mult) and isinstance(values.left, ast.List) and len(values.left.elts) == 1
        if repeated: length = self.declarative(values.right); elements = values.left.elts
        elif isinstance(values, (ast.List, ast.Tuple)): length = self.declarative(ast.Constant(value=len(values.elts))); elements = values.elts
        else: self.fail(node, 'array initializer must be a literal sequence or [value] * fixed_length')
        if self.expressions[length][1] != I: self.fail(node, 'array length must be an integer')
        zero = self.declarative(ast.Constant(value=0.0 if typ == F else 0))
        if name.startswith('ctx.'):
            if name[4:] in self.fields: self.fail(node, 'array field already exists')
        elif name in self.env: self.fail(node, 'array binding already exists')
        reg = self.add_resource(name, resource_kind, length, zero)
        if name.startswith('ctx.'): self.fields[name[4:]] = reg
        else: self.env[name] = reg
        if repeated:
            end = self.reg(I); self.emit('LENGTH', end, reg)
            value = self.convert(self.expr(elements[0]), typ)
            self.emit('RESOURCE_FILL', reg, value, self.literal(0), end)
        else:
            for i, item in enumerate(elements):
                value = self.convert(self.expr(item), typ)
                self.emit('STORE_ELEM', reg, self.literal(i), value)

    def statements(self, nodes):
        for n in nodes:
            if isinstance(n, ast.Pass) or isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str): continue
            if isinstance(n, ast.Assign) and len(n.targets) == 1:
                if isinstance(n.value, ast.Call) and self.imports.get(getattr(n.value.func, 'id', '')) in ('setting', 'array.array', 'SpriteBatch'):
                    self.declaration(n.targets[0], n.value, n)
                else: self.assign_target(n.targets[0], self.expr(n.value), n)
            elif isinstance(n, ast.AugAssign):
                if isinstance(n.target, ast.Subscript):
                    ref, kind = self.resource(n.target.value); self.check_write(ref, n)
                    index = self.expr(n.target.slice)
                    if self.typ(index) != I: self.fail(n, 'index must be an integer')
                    at = self.reg(I); self.emit('MOV', at, index)
                    old = self.reg(self.element_type(kind)); self.emit('LOAD_ELEM', old, ref, at)
                    value = self.convert(self.binary(n.op, old, self.expr(n.value)), self.element_type(kind))
                    self.emit('STORE_ELEM', ref, at, value)
                else:
                    old = self.expr(n.target); saved = self.reg(self.typ(old)); self.emit('MOV', saved, old)
                    self.assign_target(n.target, self.binary(n.op, saved, self.expr(n.value)), n)
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call):
                call = n.value
                if isinstance(call.func, ast.Attribute) and call.func.attr == 'fill' and self.capability(call.func.value) == 'pixels':
                    ref, kind = self.resource(call.func.value); self.check_write(ref, n)
                    if len(call.args) != 1: self.fail(n, 'one positional argument required')
                    kws = {k.arg: k.value for k in call.keywords}
                    if 'alpha' in kws:
                        if self.profile_name != 'layer/1' or len(kws) != len(call.keywords) or set(kws) != {'alpha'}: self.fail(n, 'alpha fill requires layer/1 and covers the whole surface')
                        self.native('rgba_fill', [ref, self.expr(call.args[0]), self.expr(kws['alpha'])], n)
                        continue
                    if len(kws) != len(call.keywords) or set(kws) - {'start', 'stop'}: self.fail(n, 'invalid fill range')
                    val = self.convert(self.expr(call.args[0]), C)
                    start = self.expr(kws['start']) if 'start' in kws else self.literal(0)
                    stop = self.expr(kws['stop']) if 'stop' in kws else self.inputs['pixel_count']
                    if self.typ(start) != I or self.typ(stop) != I: self.fail(n, 'fill range must be i32')
                    self.emit('RESOURCE_FILL', ref, val, start, stop)
                else: self.expr(call)
            elif isinstance(n, ast.For) and isinstance(n.target, ast.Name) and isinstance(n.iter, ast.Call) and getattr(n.iter.func, 'id', '') == 'range' and len(n.iter.args) == 1 and not n.iter.keywords and not n.orelse:
                if n.target.id in self.names or n.target.id in self.caps or n.target.id in self.imports: self.fail(n, 'loop variable shadows a binding')
                stop = self.expr(n.iter.args[0])
                if self.typ(stop) != I: self.fail(n, 'range bound must be i32')
                bound = self.reg(I); self.emit('MOV', bound, stop)
                loop = self.reg(I); self.env[n.target.id] = loop; self.emit('MOV', loop, self.literal(0))
                head = len(self.code); cond = self.reg(B); self.emit('ILT', cond, loop, bound); end = self.emit('JZ', cond)
                breaks, continues = [], []; self.loop_stack.append((breaks, continues)); self.depth += 1; self.statements(n.body); self.depth -= 1; self.loop_stack.pop()
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
                elif self.stage == 0: self.fail(n, 'initialization must initialize all context fields')
                elif n.value is None: self.emit('END')
                else: self.fail(n, 'void entry cannot return a value')
            elif isinstance(n, ast.If):
                cond = self.truth(self.expr(n.test))
                jump = self.emit('JZ', cond); before = dict(self.env); before_decl = dict(self.declarations); self.depth += 1; self.statements(n.body); body_decl = dict(self.declarations)
                end = self.emit('JMP'); self.code[jump][4] = len(self.code); self.env = dict(before)
                self.declarations = dict(before_decl); self.statements(n.orelse); self.depth -= 1; self.code[end][4] = len(self.code); self.env = before
                self.declarations = {k:v for k,v in self.declarations.items() if body_decl.get(k) == v}
            else: self.fail(n, 'unsupported statement in portable profile')

    def compile(self):
        marked = False
        for n in self.tree.body:
            if isinstance(n, ast.Import):
                for item in n.names:
                    if item.name not in ('random', 'math'): self.fail(n, 'only random and math module imports supported')
                    self.imports[item.asname or item.name] = item.name
            elif isinstance(n, ast.ImportFrom):
                allowed = PUBLIC if n.module == 'lightvm' else {'array'} if n.module == 'array' else {'sin', 'pi'} if n.module == 'math' else {'seed', 'random', 'randint', 'randrange'} if n.module == 'random' else set()
                if n.level or any(a.name not in allowed or a.asname for a in n.names): self.fail(n, 'unsupported import; animation decorators are @init and @render')
                for item in n.names: self.imports[item.name] = item.name if n.module == 'lightvm' else n.module + '.' + item.name
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and getattr(n.value.func, 'id', '') == 'profile':
                if self.imports.get('profile') != 'profile' or marked or n.value.keywords or len(n.value.args) != 1 or not isinstance(n.value.args[0], ast.Constant) or n.value.args[0].value not in ('animation/1','layer/1'): self.fail(n, 'expected one animation/1 or layer/1 profile declaration')
                self.profile_name = n.value.args[0].value
                if self.profile_name == 'layer/1':
                    kind, length, fill, seed, stream, flags = self.resources[0]
                    self.resources[0] = (12, length, fill, seed, stream, flags)
                marked = True
            elif isinstance(n, ast.FunctionDef):
                if n.args.posonlyargs or n.args.kwonlyargs or n.args.vararg or n.args.kwarg or n.args.defaults or n.args.kw_defaults: self.fail(n, 'simple positional functions required')
                if len({a.arg for a in n.args.args}) != len(n.args.args): self.fail(n, 'duplicate argument')
                if n.name in self.helpers or any(fn.name == n.name for fn in self.funcs.values()) or n.name in self.imports: self.fail(n, 'duplicate function name')
                if not n.decorator_list: self.helpers[n.name] = n; continue
                if len(n.decorator_list) != 1 or not isinstance(n.decorator_list[0], ast.Name): self.fail(n, 'use @init or @render without arguments')
                role = n.decorator_list[0].id
                if role not in ('init', 'render') or self.imports.get(role) != role: self.fail(n, 'only @init and @render are supported')
                if role in self.funcs or len(n.args.args) != (2 if role == 'init' else 3): self.fail(n, '@init takes (ctx, runtime); @render takes (ctx, runtime, pixels)')
                self.funcs[role] = n
            elif not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)):
                self.fail(n, 'module state is unsupported; initialize ctx fields in @init')
        if not marked or 'render' not in self.funcs: raise CompileError('animation/1 requires a @render entry')
        functions = []
        for self.stage, role, self.rights in ((0, 'init', 9), (1, 'render', 13)):
            start = len(self.code); fn = self.funcs.get(role); self.env = {}; self.writes = set(); self.caps = {}; self.declarations = {}
            if fn:
                self.caps = {a.arg: cap for a, cap in zip(fn.args.args, ('ctx', 'runtime', 'pixels'))}
                if any(a.arg in self.imports for a in fn.args.args): self.fail(fn, 'caller argument shadows an import')
                self.statements(fn.body)
            self.emit('END')
            functions.append((start, len(self.code), self.rights, 0, 0, 3))
        if len(self.code) > 2048: raise CompileError('code limit exceeded')
        self.allocate(functions)
        if len(self.code) > 2048 or len(self.table) > 1024 or len(self.calls) > 256: raise CompileError('portable package limits exceeded')
        layer = self.profile_name == 'layer/1'
        manifest = {'schema':'lightvm.program/6' if layer else 'lightvm.program/5','abi':4,'profile':self.profile_name,'pixels':{'format':'rgba8-premul' if layer else 'rgb8'},'parameters':self.params,
                    'settings':[{'name':n,'type':{I:'int',F:'float',C:'rgb8',B:'bool'}[self.typ(r)],'register':r} for n,r,*_ in self.settings]}
        metadata = json.dumps(manifest,separators=(',',':'),allow_nan=False).encode()
        header = struct.pack('<4s23I', b'LBC1', 4, len(self.regs), len(self.code), len(self.table), len(functions), len(self.resources), len(self.expressions), len(self.calls), len(self.settings),
                             0, 1, 0, 0, 0, 0, 0, 0, 2 if layer else 1, len(metadata), 0, 0, 0, 0)
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
