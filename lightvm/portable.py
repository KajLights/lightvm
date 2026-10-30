"""The animation frontend for the general LBC1/4 instruction container.

The function table carries explicit permissions/result types; animation entry
selection is profile metadata. No source code runs during compilation.
"""
import ast
import copy
import json
import math
import struct
from pathlib import Path
from .sprite_compiler import SpriteCompiler, FEATURE_NATIVES
from .input_compiler import InputCompiler
from .release import VERSION, BASELINE, normalize_release, release_error
from .compiler_core import Compiler, CompileError, OP, I, F, C, B, Q, LOCAL, INPUT, CONST, STATE
from . import numeric

REF, SETTING = 6, 6
P_OP = {**OP, 'LOAD_ELEM': 33, 'STORE_ELEM': 34, 'LENGTH': 35,
        'RESOURCE_FILL': 36, 'RESOURCE_COPY': 37, 'NATIVE': 38, 'PIXEL_MAP': 39}
# name: binding ID, result type, argument types, required effect permissions
NATIVES = {
    'random': (3, F, (), 8), 'randrange': (4, I, (I, I, I), 8),
    'seed': (5, 0, (I,), 8), 'seed_default': (6, 0, (), 8),
    'sin': (7, F, (F,), 0), 'red': (8, I, (C,), 0),
    'green': (9, I, (C,), 0), 'blue': (10, I, (C,), 0),
    'colorwheel': (11, C, (F,), 0), 'pico_colorwheel': (12, C, (I,), 0),
    'hue_degrees': (13, F, (C,), 0),
    'require': (14, 0, (B,), 0),
    'color_int': (15, I, (C,), 0), 'int_color': (16, C, (I,), 0),
    'randint': (17, I, (I, I), 8),
}
NATIVES.update({name:(row['binding'], F, tuple(row['signature'][1:]), 0) for name,row in numeric.NOISE_OPERATIONS.items()})
NATIVES.update(FEATURE_NATIVES)
NATIVES.update({
    'composition_mix': (53, 0, (6, F, I, I), 0),
    'composition_copy': (54, 0, (6, I, I, I), 0),
    'input_int': (55, I, (I,), 4), 'input_float': (56, F, (I,), 4),
    'input_color': (57, C, (I,), 4), 'input_bool': (58, B, (I,), 4),
})
ARRAY_TYPES = {'i': (1, I), 'f': (2, F), 'B': (7, I), 'H': (8, I), 'I': (11, I)}
PUBLIC = {'requires', 'profile', 'setting', 'Input', 'Ramp', 'rgb', 'hsv', 'hash01', 'init', 'render', 'SpriteBatch', 'Sprite', 'PixelArray', 'Transform', 'ColorTransform',
          'red', 'green', 'blue', 'colorwheel', 'pico_colorwheel', 'hue_degrees',
          'fixed', 'fixed_bits', 'fixed_from_bits', 'fixed_sqrt', 'FixedArray', 'floor', 'ceil', 'trunc'}
PUBLIC.update(numeric.NOISE_OPERATIONS)
EXPR_OPS = {ast.Add: 3, ast.Sub: 4, ast.Mult: 5, ast.FloorDiv: 6, ast.Mod: 7, ast.Div: 8}
BIT_OPS = {ast.BitAnd:'IAND', ast.BitOr:'IOR', ast.BitXor:'IXOR', ast.LShift:'ISHL', ast.RShift:'ISHR'}
LIBRARIES = {'lightvm.motion', 'lightvm.easing', 'lightvm.transitions'}


class PortableCompiler(InputCompiler, SpriteCompiler, Compiler):
    def __init__(self, source, optimize=True, debug=False):
        super().__init__(source)
        self.optimize = optimize and not debug
        self.debug = debug
        self.source_node = None
        self.code_locations, self.checkpoints = [], {}
        self.library_location = None
        # Runtime arguments are flattened into typed input registers. Context
        # fields and array bindings belong to this instance, never module globals.
        self.regs, self.names = [], {}
        self.profile_name = 'transition' if any(
            isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and
            getattr(n.value.func, 'id', '') == 'profile' and len(n.value.args) == 1 and
            isinstance(n.value.args[0], ast.Constant) and n.value.args[0].value == 'transition'
            for n in self.tree.body) else 'animation'
        arguments = (('pixel_count', I), ('progress', F)) if self.profile_name == 'transition' else (
            ('pixel_count', I), ('time', F), ('delta', F))
        self.inputs = {name: self.reg(typ, INPUT) for name, typ in
                       arguments}
        self.resources, self.settings, self.expressions, self.calls = [], [], [], []
        self.map_reports = []
        self.resource_names, self.setting_names = {}, {}
        self.helpers, self.call_stack, self.return_stack, self.loop_stack = {}, [], [], []
        self.fields, self.caps, self.declarations, self.imports = {}, {}, {}, {}
        self.module_values, self.module_constants = {}, {}
        self.rights, self.depth = 0, 0
        self.feature_setup()
        self.input_setup()
        self.expressions.append((0, I, 0, 0, 0))
        count = len(self.expressions); self.expressions.append((1, I, 0, 0, 0))
        black = self.declarative(ast.Call(func=ast.Name(id='rgb'), args=[ast.Constant(value=0)]*3, keywords=[]))
        self.output = self.add_resource('__pixels', 12, count, black)

    def capability(self, node):
        return self.caps.get(node.id) if isinstance(node, ast.Name) else None

    def declaration_key(self, node):
        if isinstance(node, ast.Name): return node.id
        if isinstance(node, ast.Attribute) and self.capability(node.value) == 'ctx':
            return 'ctx.' + node.attr
        return None

    def import_library(self, node):
        """Parse only bundled, trusted sources; guest imports never execute."""
        module = node.module
        tree = ast.parse(Path(__file__).with_name(module.split('.')[-1] + '.py').read_text())
        functions = {fn.name: fn for fn in tree.body if isinstance(fn, ast.FunctionDef)}

        class Qualify(ast.NodeTransformer):
            def visit_Call(self, call):
                self.generic_visit(call)
                if isinstance(call.func, ast.Name) and call.func.id in functions:
                    call.func.id = module + '.' + call.func.id
                elif isinstance(call.func, ast.Attribute) and isinstance(call.func.value, ast.Name) and call.func.value.id == 'math':
                    call.func = ast.Name(id='lightvm.math.' + call.func.attr, ctx=ast.Load())
                elif isinstance(call.func, ast.Name) and call.func.id in ('min','max','float','int'):
                    call.func.id = 'lightvm.builtin.' + call.func.id
                return call

        for name, fn in functions.items():
            self.helpers[module + '.' + name] = Qualify().visit(fn)
        for item in node.names:
            name = item.asname or item.name
            if item.name.startswith('_') or item.name not in functions or name in self.imports or name in self.helpers or name in self.artwork or name in self.sprite_definitions:
                self.fail(node, 'unknown or duplicate library helper: ' + name)
            self.imports[name] = module + '.' + item.name

    def truth(self, reg):
        if self.typ(reg) == B: return reg
        if self.typ(reg) not in (I, F, C, Q): raise CompileError('condition requires a scalar')
        zero = self.raw_constant(Q, 0) if self.typ(reg) == Q else self.literal(0.0 if self.typ(reg) == F else 0)
        if self.typ(reg) == C: reg = self.convert(reg, I)
        equal = self.reg(B); self.emit('EQ', equal, reg, zero)
        out = self.reg(B); self.emit('EQ', out, equal, self.literal(False)); return out

    def snapshot(self, reg, *, local=False):
        """An earlier operand must survive a later helper changing context."""
        if reg >= 0 and self.regs[reg][1] in ((LOCAL, STATE) if local else (STATE,)):
            saved = self.reg(self.typ(reg)); self.emit('MOV', saved, reg)
            return saved
        return reg

    def comparison(self, op, a, b, node):
        if isinstance(op, (ast.Eq, ast.NotEq)) and self.typ(a) == self.typ(b):
            if self.typ(a) == REF: self.fail(node, 'Compare values from an array, rather than the arrays themselves.')
            out = self.reg(B); self.emit('EQ', out, a, b)
        else:
            a, b, typ = self.pair(a, b)
            if isinstance(op, (ast.Eq, ast.NotEq)):
                out = self.reg(B); self.emit('EQ', out, a, b)
            else:
                if isinstance(op, (ast.Gt, ast.LtE)): a, b = b, a
                if not isinstance(op, (ast.Lt, ast.Gt, ast.LtE, ast.GtE)):
                    self.fail(node, 'Use <, <=, >, >=, == or != to compare these values.')
                if typ == Q: out = self.numeric_op('QLT', a, b)
                else:
                    out = self.reg(B); self.emit('FLT' if typ == F else 'ILT', out, a, b)
        if isinstance(op, (ast.NotEq, ast.LtE, ast.GtE)):
            negated = self.reg(B); self.emit('EQ', negated, out, self.literal(False)); out = negated
        return out

    def convert(self, reg, typ, explicit=False):
        if self.typ(reg) == C and typ == I: return self.native('color_int', [reg], None)
        if self.typ(reg) == I and typ == C: return self.native('int_color', [reg], None)
        return super().convert(reg, typ, explicit=explicit)

    def colour_constant(self, value):
        key = C, value
        if key not in self.constants: self.constants[key] = self.reg(C, CONST, value)
        return self.constants[key]

    def reg(self, typ, role=LOCAL, value=0):
        if len(self.regs) >= 4096:
            raise CompileError('virtual register limit exceeded')
        self.regs.append((typ, role, value))
        return len(self.regs) - 1

    def typ(self, register):
        if register < 0: raise CompileError('This function draws or changes values but does not give back a value.', self.source_node)
        return super().typ(register)

    def emit(self, op, a=0, b=0, c=0, imm=0):
        self.code.append([P_OP[op], a, b, c, imm])
        node = self.library_location or self.source_node
        self.code_locations.append({'line': getattr(node, 'lineno', 1), 'column': getattr(node, 'col_offset', 0) + 1})
        return len(self.code) - 1

    def declarative(self, node):
        """Checked acyclic numeric expressions, evaluated by native admission."""
        if isinstance(node, ast.Name) and node.id in self.module_values:
            return self.declarative(self.module_values[node.id])
        if isinstance(node, ast.Constant):
            r = self.literal(node.value)
            typ, _, bits = self.regs[r]
            value = (0, typ, 0, 0, bits)
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub) and isinstance(node.operand, ast.Constant):
            return self.declarative(ast.Constant(value=-node.operand.value))
        elif isinstance(node, ast.Attribute) and self.capability(node.value) == 'runtime' and node.attr == 'pixel_count':
            value = (1, I, 0, 0, 0)
        elif isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id in self.sprite_definitions and node.attr == 'width':
            return self.declarative(ast.Constant(value=self.sprite_definitions[node.value.id]['width']))
        elif self.declaration_key(node) in self.declarations:
            return self.declarations[self.declaration_key(node)]
        elif isinstance(node, ast.Call) and getattr(node.func, 'id', '') == 'rgb':
            value = (0, C, 0, 0, self.colour_literal(node))
        elif isinstance(node, ast.Call) and self.imports.get(getattr(node.func, 'id', '')) in ('fixed', 'fixed_bits', 'fixed_from_bits', 'fixed_sqrt'):
            name = self.imports[node.func.id]
            if len(node.args) != 1 or node.keywords: self.fail(node, name + ' takes one positional argument')
            literal = self.fixed_literal(node.args[0]) if name == 'fixed' else None
            if literal is not None:
                value = (0, Q, 0, 0, self.regs[literal][2])
            else:
                a = self.declarative(node.args[0]); typ = self.expressions[a][1]
                if name == 'fixed' and typ == Q: return a
                operation = ({I:'ITOQ', F:'FTOQ'}.get(typ) if name == 'fixed' else
                             {'fixed_bits':'QBITS', 'fixed_from_bits':'QFROMBITS', 'fixed_sqrt':'QSQRT'}[name])
                if operation is None: self.fail(node, 'fixed conversion requires i32 or f32')
                return self.declarative_numeric(operation, [a], node)
        elif isinstance(node, ast.Call) and getattr(node.func, 'id', '') in ('int', 'float') and len(node.args) == 1 and not node.keywords:
            a = self.declarative(node.args[0]); typ = I if node.func.id == 'int' else F
            if self.expressions[a][1] == typ: return a
            if self.expressions[a][1] != Q: self.fail(node, 'unsupported declaration conversion')
            return self.declarative_numeric('QTOI' if typ == I else 'QTOF', [a], node)
        elif isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.Invert)):
            a = self.declarative(node.operand)
            return self.declarative_numeric('QNEG' if isinstance(node.op, ast.USub) else 'INOT', [a], node)
        elif isinstance(node, ast.BinOp) and type(node.op) in (EXPR_OPS.keys() | BIT_OPS.keys()):
            a, b = self.declarative(node.left), self.declarative(node.right)
            ta, tb = self.expressions[a][1], self.expressions[b][1]
            if type(node.op) in BIT_OPS:
                return self.declarative_numeric(BIT_OPS[type(node.op)], [a, b], node)
            if Q in (ta, tb):
                operation = {ast.Add:'QADD', ast.Sub:'QSUB', ast.Mult:'QMUL', ast.Div:'QDIV', ast.Mod:'QMOD'}.get(type(node.op))
                if operation is None: self.fail(node, 'fixed arithmetic supports +, -, *, / and %')
                return self.declarative_numeric(operation, [a, b], node)
            if ta not in (I, F) or tb not in (I, F): self.fail(node, 'numeric declaration required')
            typ = F if F in (ta, tb) or isinstance(node.op, ast.Div) else I
            if isinstance(node.op, ast.FloorDiv) and typ != I: self.fail(node, 'integer floor division required')
            value = (EXPR_OPS[type(node.op)], typ, a, b, 0)
        elif isinstance(node, ast.Call) and getattr(node.func, 'id', '') in ('min', 'max') and len(node.args) == 2 and not node.keywords:
            a, b = map(self.declarative, node.args)
            ta, tb = self.expressions[a][1], self.expressions[b][1]
            if Q in (ta, tb): return self.declarative_numeric('Q' + node.func.id.upper(), [a, b], node)
            if ta not in (I, F) or tb not in (I, F): self.fail(node, 'numeric declaration required')
            value = (9 if node.func.id == 'min' else 10, F if F in (ta, tb) else I, a, b, 0)
        else:
            self.fail(node, 'declaration needs literals, runtime.pixel_count or earlier settings')
        return self.declarative_record(value, node)

    def declarative_numeric(self, name, args, node):
        signature = numeric.SIGNATURES[name]
        if tuple(self.expressions[a][1] for a in args) != signature[1:]:
            self.fail(node, 'numeric declaration requires explicit matching types')
        value = (P_OP[name], signature[0], args[0], args[1] if len(args) == 2 else 0, 0)
        if self.optimize and all(self.expressions[a][0] == 0 for a in args):
            try: bits = numeric.evaluate(name, *(self.expressions[a][4] for a in args))
            except ArithmeticError: pass
            else: value = (0, signature[0], 0, 0, bits)
        return self.declarative_record(value, node)

    def declarative_record(self, value, node):
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
        if name in numeric.NOISE_OPERATIONS:
            dimensions = int(name[-1])
            defaults = [0] + ([256]*dimensions if name.startswith('perlin') else [])
            if dimensions <= len(args) <= len(types):
                args = args + [self.literal(v) for v in defaults[len(args)-dimensions:]]
            if any(self.typ(r) != I for r in args[dimensions:]):
                self.fail(node, 'noise seeds and repeat periods require i32 values')
        if len(args) != len(types) or rights & ~self.rights:
            self.fail(node, 'native call arguments or stage permissions do not match')
        args = [self.convert(r, t) for r, t in zip(args, types)]
        out = self.reg(result) if result else -1
        site = len(self.calls)
        self.calls.append((binding, args))
        self.emit('NATIVE', out, imm=site)
        return out

    def resource(self, node):
        if self.capability(node) == 'pixels': return self.output, self.resources[self.regs[self.output][2]][0]
        if isinstance(node, ast.Attribute) and self.capability(node.value) == 'ctx':
            reg = self.fields.get(node.attr)
        elif isinstance(node, ast.Name): reg = self.env.get(node.id, self.artwork.get(node.id))
        else: reg = None
        if reg is not None and self.typ(reg) == REF:
            return reg, self.resources[self.regs[reg][2]][0]
        self.fail(node, 'managed array or pixels argument required')

    def check_write(self, ref, node):
        identity = self.regs[ref][2]
        if self.resources[identity][0] in (9, 10) or self.resources[identity][5]&2 or not self.rights & (4 if identity == 0 else 1):
            self.fail(node, 'stage cannot mutate this resource')

    def expr(self, n):
        before = self.source_node
        self.source_node = n
        try: return self._expr(n)
        except CompileError as error:
            if error.node is None: error.node = n
            raise
        finally: self.source_node = before

    def _expr(self, n):
        live_input = self.input_read(n)
        if live_input is not None: return live_input
        feature = self.feature_expr(n)
        if feature is not None: return feature
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id.startswith('lightvm.math.'):
            return self.math_call(n.func.id.rsplit('.',1)[1], n)
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id.startswith('lightvm.builtin.'):
            return super().expr(ast.Call(func=ast.Name(id=n.func.id.rsplit('.',1)[1]), args=n.args, keywords=n.keywords))
        if isinstance(n, ast.Attribute):
            cap = self.capability(n.value)
            if cap == 'ctx' and n.attr in self.fields: return self.fields[n.attr]
            if cap == 'runtime' and n.attr in self.inputs: return self.inputs[n.attr]
            if cap == 'pixels' and n.attr == 'count': return self.inputs['pixel_count']
            if isinstance(n.value, ast.Name) and self.imports.get(n.value.id) == 'math' and n.attr == 'pi': return self.literal(math.pi)
            self.fail(n, 'unknown context field or caller argument property')
        if isinstance(n, ast.Name):
            if self.imports.get(n.id) == 'math.pi': return self.literal(math.pi)
            if self.capability(n): self.fail(n, 'caller objects may only be passed to helpers or accessed by property')
            if n.id in self.module_constants and n.id not in self.env: return self.module_constants[n.id]
            if n.id not in self.env:
                self.fail(n, 'Give "' + n.id + '" a value before using it. If it is set inside a loop or condition, set a starting value first.')
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute):
            if n.func.attr in ('fill', 'copy_from'):
                try: ref, kind = self.resource(n.func.value)
                except CompileError: kind = None
                if kind in (1, 2, 7, 8, 11, 20):
                    self.check_write(ref, n)
                    if len(n.args) != 1: self.fail(n, 'one positional argument required')
                    if n.func.attr == 'copy_from':
                        if n.keywords: self.fail(n, 'copy_from takes one source array')
                        source, source_kind = self.resource(n.args[0])
                        if source_kind != kind: self.fail(n, 'copy_from requires matching array types')
                        self.emit('RESOURCE_COPY', ref, source)
                    else:
                        kws = {k.arg:k.value for k in n.keywords}
                        if len(kws) != len(n.keywords) or set(kws) - {'start', 'stop'}: self.fail(n, 'invalid fill range')
                        value = self.snapshot(self.convert(self.expr(n.args[0]), self.element_type(kind)))
                        ranges = {key: self.snapshot(self.expr(item)) for key, item in kws.items()}
                        start = ranges['start'] if 'start' in ranges else self.literal(0)
                        if 'stop' in ranges: stop = ranges['stop']
                        else: stop = self.reg(I); self.emit('LENGTH', stop, ref)
                        if self.typ(start) != I or self.typ(stop) != I: self.fail(n, 'fill range must be i32')
                        self.emit('RESOURCE_FILL', ref, value, start, stop)
                    return -1
            module = self.imports.get(getattr(n.func.value, 'id', ''))
            if module == 'random': return self.random_call(n.func.attr, n)
            if module == 'math' and n.func.attr in ('abs','fabs','floor','ceil','trunc','cos','sqrt','log','exp','pow'):
                return self.math_call(n.func.attr,n)
            if module == 'math' and n.func.attr == 'sin':
                if n.keywords: self.fail(n, 'math.sin takes one positional argument')
                return self.native('sin', [self.snapshot(self.expr(a)) for a in n.args], n)
            method = n.func.attr
            if self.capability(n.func.value) == 'pixels' and method in ('mix', 'copy_composition'):
                if self.profile_name != 'transition' or self.stage != 1:
                    self.fail(n, 'composition drawing requires a transition render callback')
                args = self.arguments(n, ('progress' if method == 'mix' else 'composition', 'start', 'stop'),
                                      {'start': 0, 'stop': ast.Attribute(value=n.func.value, attr='count')})
                if method == 'mix':
                    value = self.expr(args['progress'])
                else:
                    composition = args['composition']
                    if not isinstance(composition, ast.Constant) or composition.value not in ('A', 'B'):
                        self.fail(n, 'copy_composition requires the literal composition name A or B')
                    value = self.literal(int(composition.value == 'B'))
                return self.native('composition_mix' if method == 'mix' else 'composition_copy',
                                   [self.output, value, self.expr(args['start']), self.expr(args['stop'])], n)
            if self.capability(n.func.value) == 'pixels' and method in ('point', 'colorwheel', 'gradient', 'palette'):
                if n.keywords: self.fail(n, 'native drawing takes positional arguments')
                args = [self.output] + [self.snapshot(self.expr(a)) for a in n.args]
                if method == 'point' and len(args) == 3: args.append(self.literal(255))
                return self.native({'point':'image_point', 'colorwheel':'image_colorwheel', 'gradient':'image_gradient', 'palette':'image_palette'}[method], args, n)
        if isinstance(n, ast.UnaryOp) and isinstance(n.op, ast.Not):
            r = self.truth(self.expr(n.operand))
            out = self.reg(B); self.emit('EQ', out, r, self.literal(False)); return out
        if isinstance(n, ast.BinOp):
            a = self.snapshot(self.expr(n.left))
            return self.binary(n.op, a, self.expr(n.right))
        if isinstance(n, ast.IfExp):
            cond = self.truth(self.expr(n.test)); branch = self.emit('JZ', cond)
            a = self.expr(n.body); out = self.reg(self.typ(a)); self.emit('MOV', out, a)
            jump = self.emit('JMP'); self.code[branch][4] = len(self.code)
            b = self.expr(n.orelse)
            if self.typ(a) != self.typ(b): self.fail(n, 'Both choices must produce the same type of value.')
            self.emit('MOV', out, b); self.code[jump][4] = len(self.code)
            return out
        if isinstance(n, ast.Compare):
            left = self.snapshot(self.expr(n.left))
            if len(n.ops) == 1:
                return self.comparison(n.ops[0], left, self.expr(n.comparators[0]), n)
            out = self.reg(B); exits = []
            for index, (op, item) in enumerate(zip(n.ops, n.comparators)):
                right = self.snapshot(self.expr(item))
                self.emit('MOV', out, self.comparison(op, left, right, n))
                if index + 1 < len(n.ops): exits.append(self.emit('JZ', out))
                left = right
            for jump in exits: self.code[jump][4] = len(self.code)
            return out
        if isinstance(n, ast.BoolOp):
            first = self.expr(n.values[0]); out = self.reg(self.typ(first))
            if self.typ(first) not in (I, F, C, B, Q): self.fail(n, 'Use numbers, colours or true/false values with and/or.')
            self.emit('MOV', out, first); exits = []
            for item in n.values[1:]:
                r = self.truth(out)
                if isinstance(n.op, ast.Or):
                    inv = self.reg(B); self.emit('EQ', inv, r, self.literal(False)); r = inv
                exits.append(self.emit('JZ', r))
                other = self.expr(item)
                if self.typ(other) != self.typ(out):
                    self.fail(n, 'The values joined by and/or must have the same type. Use comparisons for a true/false condition.')
                self.emit('MOV', out, other)
            for jump in exits: self.code[jump][4] = len(self.code)
            return out
        if isinstance(n, ast.Subscript):
            ref, kind = self.resource(n.value)
            if self.regs[ref][2] == 0 and not self.rights & 4: self.fail(n, 'initialization cannot access output pixels')
            index = self.expr(n.slice)
            if self.typ(index) != I: self.fail(n, 'index must be i32')
            typ = self.element_type(kind)
            out = self.reg(typ); self.emit('LOAD_ELEM', out, ref, index); return out
        if isinstance(n, ast.Call):
            name = getattr(n.func, 'id', '')
            imported = self.imports.get(name, '')
            if imported in ('floor','ceil','trunc'):
                if len(n.args)!=1 or n.keywords:self.fail(n,'one numeric argument required')
                return self.numeric_op('F'+imported.upper(),self.convert(self.expr(n.args[0]),F))
            if imported.startswith('random.'): return self.random_call(imported.split('.')[1], n)
            if name == 'len' and len(n.args) == 1 and not n.keywords:
                ref, kind = self.resource(n.args[0])
                out = self.reg(I); self.emit('LENGTH', out, ref); return out
            if imported in {'math.'+name for name in ('fabs','floor','ceil','trunc','cos','sqrt','log','exp','pow')}:
                return self.math_call(imported[5:],n)
            if imported == 'math.sin' or imported in NATIVES:
                if n.keywords: self.fail(n, 'helper takes positional arguments')
                return self.native('sin' if imported == 'math.sin' else imported, [self.snapshot(self.expr(a)) for a in n.args], n)
            if name not in self.helpers and imported in self.helpers:
                name = imported
            if name in self.helpers:
                library = name.startswith('lightvm.')
                if name in self.call_stack or len(self.call_stack) >= 8:
                    self.fail(n, 'Functions cannot call themselves: ' + ' → '.join(self.call_stack + [name]))
                fn = self.helpers[name]
                names = tuple(arg.arg for arg in fn.args.args)
                defaults = dict(zip(names[len(names)-len(fn.args.defaults):], fn.args.defaults)) if fn.args.defaults else {}
                values = self.arguments(n, names, defaults)
                def returns(nodes):
                    return any(isinstance(item, ast.Return) and item.value is not None or
                               isinstance(item, ast.If) and returns(item.body) and returns(item.orelse)
                               for item in nodes)
                valued = any(isinstance(item, ast.Return) and item.value is not None for item in ast.walk(fn))
                if valued and (not returns(fn.body) or any(isinstance(item, ast.Return) and item.value is None for item in ast.walk(fn))):
                    self.fail(fn, 'This function needs to return a value on every path, including the else case.')
                supplied = set(names[:len(n.args)]) | {keyword.arg for keyword in n.keywords}
                evaluated = {}
                for key, argument in values.items():
                    if key in supplied:
                        evaluated[key] = (self.capability(argument), None) if self.capability(argument) else (None, self.snapshot(self.expr(argument)))
                    else:
                        # Defaults are immutable declaration expressions, not
                        # expressions in the caller's local scope.
                        caller_env, caller_caps = self.env, self.caps
                        self.env, self.caps = {}, {}
                        evaluated[key] = (None, self.expr(argument))
                        self.env, self.caps = caller_env, caller_caps
                args = [evaluated[key] for key in names]
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
                previous_library_location = self.library_location
                if library and self.library_location is None: self.library_location = n
                self.statements(fn.body)
                self.library_location = previous_library_location
                self.return_stack.pop(); self.call_stack.pop()
                out = returns[0][1] if returns else -1
                for jump, result in returns:
                    if result != out: self.fail(fn, 'inconsistent helper return')
                    self.code[jump][4] = len(self.code)
                self.env, self.writes, self.loop_stack, self.caps = previous_env, previous_writes, previous_loops, previous_caps
                return out
        if isinstance(n, ast.Call) and isinstance(n.func, ast.Name) and n.func.id not in {'int', 'float', 'min', 'max', 'abs'} and self.imports.get(n.func.id) not in {'rgb', 'hsv', 'hash01', 'fixed', 'fixed_bits', 'fixed_from_bits', 'fixed_sqrt'}:
            self.fail(n, 'unsupported or unimported function')
        return super().expr(n)

    def math_call(self, name, node):
        expected=2 if name=='pow' else 1
        if len(node.args)!=expected or node.keywords:self.fail(node, name+' takes '+str(expected)+' positional arguments')
        operation='FABS' if name in ('abs','fabs') else 'F'+name.upper()
        args=[self.snapshot(self.convert(self.expr(arg),F)) for arg in node.args]
        result=self.numeric_op(operation,*args)
        return self.convert(result,I) if name in ('floor','ceil','trunc') else result

    def element_type(self, kind):
        return Q if kind == 20 else I if kind in (7, 8, 11) else C if kind in (12, 14, 15, 16, 17) else kind

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
        regs = [self.snapshot(self.expr(a)) for a in args]
        if any(self.typ(r) != I for r in regs): self.fail(node, 'random bounds and step must be integers')
        return self.native(name, regs, node)

    def assign(self, name, value, node):
        if name in self.module_values and name not in self.env: self.fail(node, '"' + name + '" is a shared constant. Use a local variable or remembered value for something that changes.')
        if name in self.input_symbols: self.fail(node, 'Input declarations cannot be rebound')
        if name in self.caps or name in self.imports: self.fail(node, 'cannot rebind a caller argument or import')
        target = self.env.get(name)
        if target is None: target = self.reg(self.typ(value)); self.env[name] = target
        if self.regs[target][1] != LOCAL: self.fail(node, 'setting and array bindings are frozen')
        if self.typ(target) != self.typ(value) or self.typ(value) == REF: self.fail(node, 'Keep "' + name + '" the same kind of value. For a number that needs fractions, start with 0.0. Create arrays once in @init.')
        self.declarations.pop(name, None)
        self.emit('MOV', target, value)

    def assign_target(self, target, value, node):
        if isinstance(target, ast.Name) and target.id in self.input_symbols: self.fail(node, 'Input declarations cannot be rebound')
        if isinstance(target, ast.Name): return self.assign(target.id, value, node)
        if isinstance(target, ast.Attribute) and self.capability(target.value) == 'sprite':
            return self.sprite_set(target.attr, value, node)
        if isinstance(target, ast.Attribute) and self.capability(target.value) == 'ctx':
            reg = self.fields.get(target.attr)
            if reg is None:
                if self.stage != 0 or self.depth or self.call_stack: self.fail(node, 'Give this remembered value a starting value directly in @init, before using it in a loop or condition.')
                if self.typ(value) == REF: self.fail(node, 'array aliases are unsupported')
                reg = self.reg(self.typ(value), STATE); self.fields[target.attr] = reg
            if self.regs[reg][1] != STATE or self.typ(reg) == REF: self.fail(node, 'cannot rebind this context field')
            if self.typ(reg) != self.typ(value): self.fail(node, 'Keep this remembered value the same kind of value. Start with 0.0 if it needs fractions.')
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
        if kind == 'PixelArray': return self.image_declaration(target, call, node)
        if kind == 'SpriteBatch': return self.batch_declaration(target, call, node)
        if kind == 'setting':
            if len(call.args) != 1 or any(k.arg not in ('minimum', 'maximum') for k in call.keywords): self.fail(node, 'expected a default and optional minimum/maximum')
            external = name.removeprefix('ctx.')
            if len(external) > 32 or external in self.setting_names: self.fail(node, 'duplicate or invalid control name')
            default = self.declarative(call.args[0]); typ = self.expressions[default][1]
            if typ == Q: self.fail(node, 'fixed-valued settings are unsupported; convert an int or float setting explicitly')
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
        if kind == 'FixedArray':
            if len(call.args) != 1 or len(call.keywords) > 1 or any(k.arg != 'fill' for k in call.keywords):
                self.fail(node, 'FixedArray(count, fill=fixed_value) required')
            length = self.declarative(call.args[0])
            fill = self.declarative(call.keywords[0].value) if call.keywords else self.declarative_record((0, Q, 0, 0, 0), node)
            if self.expressions[length][1] != I or self.expressions[fill][1] != Q:
                self.fail(node, 'FixedArray needs an i32 count and explicit fixed fill')
            if name.startswith('ctx.') and name[4:] in self.fields or not name.startswith('ctx.') and name in self.env:
                self.fail(node, 'array binding already exists')
            reg = self.add_resource(name, 20, length, fill)
            if name.startswith('ctx.'): self.fields[name[4:]] = reg
            else: self.env[name] = reg
            return
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
        before = self.source_node
        try:
            for node in nodes:
                self.source_node = node
                self._statements([node])
                if self.debug and not self.library_location:
                    values = {name: r for name, r in self.env.items() if not name.startswith('__') and self.typ(r) != REF}
                    values.update({'state.' + name: r for name, r in self.fields.items() if self.typ(r) != REF})
                    self.checkpoints[len(self.code)] = {'line': getattr(node, 'lineno', 1), 'column': getattr(node, 'col_offset', 0) + 1, 'variables': values}
        except CompileError as error:
            if error.node is None: error.node = self.source_node
            raise
        finally: self.source_node = before

    def _statements(self, nodes):
        for n in nodes:
            if isinstance(n, ast.Pass) or isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str): continue
            if isinstance(n, ast.Assert):
                if n.msg is not None: self.fail(n, 'assert messages are unsupported')
                self.native('require', [self.truth(self.expr(n.test))], n)
            elif isinstance(n, ast.Assign) and len(n.targets) == 1:
                if isinstance(n.targets[0], (ast.Tuple, ast.List)):
                    target = n.targets[0]
                    if not isinstance(n.value, (ast.Tuple, ast.List)) or len(target.elts) != len(n.value.elts):
                        self.fail(n, 'Give one value for each name on the left of =.')
                    if not all(isinstance(item, ast.Name) for item in target.elts): self.fail(n, 'Use simple variable names for this assignment.')
                    values = [self.snapshot(self.expr(item), local=True) for item in n.value.elts]
                    for item, value in zip(target.elts, values): self.assign_target(item, value, n)
                    continue
                if self.feature_assignment(n.targets[0], n.value, n): continue
                if isinstance(n.value, ast.Call) and self.imports.get(getattr(n.value.func, 'id', '')) in ('setting', 'array.array', 'SpriteBatch', 'PixelArray', 'FixedArray'):
                    self.declaration(n.targets[0], n.value, n)
                else: self.assign_target(n.targets[0], self.snapshot(self.expr(n.value)), n)
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
                if isinstance(call.func, ast.Attribute) and call.func.attr == 'fill' and self.capability(call.func.value) == 'pixels' and self.resources[self.regs[self.output][2]][0] in (3,12):
                    ref, kind = self.resource(call.func.value); self.check_write(ref, n)
                    if len(call.args) != 1: self.fail(n, 'one positional argument required')
                    kws = {k.arg: k.value for k in call.keywords}
                    if 'alpha' in kws:
                        if len(kws) != len(call.keywords) or set(kws) != {'alpha'}: self.fail(n, 'alpha fill covers the whole surface')
                        self.native('image_fill', [ref, self.snapshot(self.expr(call.args[0])), self.snapshot(self.expr(kws['alpha'])), self.literal(0), self.inputs['pixel_count']], n)
                        continue
                    if len(kws) != len(call.keywords) or set(kws) - {'start', 'stop'}: self.fail(n, 'invalid fill range')
                    val = self.snapshot(self.convert(self.expr(call.args[0]), C))
                    ranges = {key: self.snapshot(self.expr(item)) for key, item in kws.items()}
                    start = ranges['start'] if 'start' in ranges else self.literal(0)
                    stop = ranges['stop'] if 'stop' in ranges else self.inputs['pixel_count']
                    if self.typ(start) != I or self.typ(stop) != I: self.fail(n, 'fill range must be i32')
                    self.emit('RESOURCE_FILL', ref, val, start, stop)
                else: self.expr(call)
            elif isinstance(n, ast.While) and not n.orelse:
                # Shared frontend loop primitive. Like for loops, execution is
                # resumable under the VM instruction budget, never run at compile time.
                head = len(self.code)
                cond = self.truth(self.expr(n.test)); end = self.emit('JZ', cond)
                before = dict(self.env)
                breaks, continues = [], []; self.loop_stack.append((breaks, continues))
                self.depth += 1; self.statements(n.body); self.depth -= 1; self.loop_stack.pop()
                for jump in continues: self.code[jump][4] = head
                self.emit('JMP', imm=head)
                for jump in breaks + [end]: self.code[jump][4] = len(self.code)
                self.env = before
                self.map_reports.append((n, False, 'a while loop checks its changing condition at each step'))
            elif isinstance(n, ast.For):
                self.for_loop(n)
            elif isinstance(n, (ast.Break, ast.Continue)):
                if not self.loop_stack: self.fail(n, 'loop control outside loop')
                self.loop_stack[-1][isinstance(n, ast.Continue)].append(self.emit('JMP'))
            elif isinstance(n, ast.Return):
                if self.return_stack:
                    val = self.expr(n.value) if n.value is not None else -1; returns = self.return_stack[-1]
                    out = returns[0][1] if returns else self.reg(self.typ(val)) if val != -1 else -1
                    if (out == -1) != (val == -1) or val != -1 and self.typ(out) != self.typ(val): self.fail(n, 'Each return in this function must give the same type of value.')
                    if val != -1: self.emit('MOV', out, val)
                    returns.append((self.emit('JMP'), out))
                elif self.sprite_context:
                    if n.value is not None or self.sprite_context[2]=='init': self.fail(n, 'sprite render may only return without a value')
                    self.sprite_returns.append(self.emit('JMP'))
                elif self.stage == 0: self.fail(n, 'initialization must initialize all context fields')
                elif n.value is None: self.emit('END')
                else: self.fail(n, 'void entry cannot return a value')
            elif isinstance(n, ast.If):
                cond = self.truth(self.expr(n.test))
                jump = self.emit('JZ', cond); before = dict(self.env); before_decl = dict(self.declarations); self.depth += 1; self.statements(n.body); body_decl = dict(self.declarations); body_env = dict(self.env)
                # An exiting arm has no fallthrough to skip. Emitting another
                # jump after it would leave unreachable code in pure map CFGs.
                def exits(body):
                    if not body: return False
                    last = body[-1]
                    return isinstance(last, (ast.Return, ast.Break, ast.Continue)) or (
                        isinstance(last, ast.If) and exits(last.body) and exits(last.orelse))
                end = self.emit('JMP') if n.orelse and not exits(n.body) else None
                self.code[jump][4] = len(self.code); self.env = dict(before)
                self.declarations = dict(before_decl); self.statements(n.orelse); self.depth -= 1
                other_env = dict(self.env)
                if exits(n.body):
                    merged = other_env
                elif exits(n.orelse):
                    merged = body_env
                else:
                    merged = dict(before)
                    for name in sorted(body_env.keys() & other_env.keys() - before.keys()):
                        a, b = body_env[name], other_env[name]
                        if self.typ(a) != self.typ(b): self.fail(n, 'Both choices must give "' + name + '" the same type of value.')
                        if self.typ(a) == REF: self.fail(n, 'Create this array once, in the starting function.')
                        if a != b: self.emit('MOV', a, b)
                        merged[name] = a
                if end is not None: self.code[end][4] = len(self.code)
                self.env = merged
                self.declarations = {k:v for k,v in self.declarations.items() if body_decl.get(k) == v}
            else: self.fail(n, 'unsupported statement in portable profile')

    def for_loop(self, node):
        """A private cursor advances independently of assignments to the target."""
        if not isinstance(node.target, ast.Name) or node.orelse:
            self.fail(node, 'Use one variable in this loop. A loop else clause is not supported.')
        name = node.target.id
        if name in self.names or name in self.caps or name in self.imports or name in self.module_values:
            self.fail(node, 'Choose a loop variable that does not replace an existing array, input or imported name.')
        iterator = node.iter; source = None; positive = False
        if isinstance(iterator, ast.Call) and getattr(iterator.func, 'id', '') == 'range':
            if iterator.keywords or not 1 <= len(iterator.args) <= 3:
                self.fail(iterator, 'Use range(stop), range(start, stop), or range(start, stop, step).')
            values = [self.snapshot(self.expr(arg)) for arg in iterator.args]
            if any(self.typ(r) != I for r in values): self.fail(iterator, 'Range needs whole numbers. Use int(value) if you want to remove a fractional part.')
            if len(values) == 1: values.insert(0, self.literal(0))
            if len(values) == 2: values.append(self.literal(1))
            start, stop, step = values
            def literal(r):
                return numeric.signed(self.regs[r][2]) if self.regs[r][1] == CONST else None
            first, last, stride = map(literal, values)
            if stride == 0: self.fail(iterator, 'The step in range cannot be zero: the loop would never move forward.')
            if stride is None:
                self.native('require', [self.comparison(ast.NotEq(), step, self.literal(0), node)], node)
            if None not in (first, last, stride): positive = (first < last if stride > 0 else first > last)
            elif first == 0 and stride == 1 and stop == self.inputs['pixel_count']: positive = True
            typ = I
        else:
            source, kind = self.resource(iterator)
            if kind not in (1, 2, 7, 8, 11, 20): self.fail(iterator, 'Loop over a number array, or use range(...) for pixel positions.')
            typ = self.element_type(kind)
            start, step, stride, first = self.literal(0), self.literal(1), 1, 0
            stop = self.reg(I); self.emit('LENGTH', stop, source)
            length = self.expressions[self.resources[self.regs[source][2]][1]]
            positive = length[0] == 0 and numeric.signed(length[4]) > 0
        before = dict(self.env)
        target = before.get(name)
        if target is None: target = self.reg(typ)
        elif self.typ(target) != typ or self.regs[target][1] != LOCAL:
            self.fail(node, 'The loop variable "' + name + '" needs the same type as the values being counted or read.')
        bound = self.reg(I); self.emit('MOV', bound, stop)
        cursor = self.reg(I); self.emit('MOV', cursor, start)
        head = len(self.code)
        if stride is None:
            direction = self.comparison(ast.Gt(), step, self.literal(0), node)
            negative = self.emit('JZ', direction)
            cond = self.reg(B); self.emit('ILT', cond, cursor, bound)
            joined = self.emit('JMP'); self.code[negative][4] = len(self.code)
            self.emit('ILT', cond, bound, cursor); self.code[joined][4] = len(self.code)
        else:
            cond = self.reg(B); self.emit('ILT', cond, cursor if stride > 0 else bound, bound if stride > 0 else cursor)
        end = self.emit('JZ', cond)
        self.env[name] = target
        if source is None: self.emit('MOV', target, cursor)
        else: self.emit('LOAD_ELEM', target, source, cursor)
        breaks, continues = [], []; self.loop_stack.append((breaks, continues))
        body = self.common_pixel_store(node.body, name) if self.optimize else node.body
        self.depth += 1; self.statements(body); self.depth -= 1; self.loop_stack.pop()
        body_env = dict(self.env)
        for jump in continues: self.code[jump][4] = len(self.code)
        overflow = []
        if stride not in (1, -1):
            # i32 arithmetic wraps, but a Python range must finish before its
            # next value would leave the representable integer domain.
            if stride is None:
                direction = self.comparison(ast.Gt(), step, self.literal(0), node)
                negative = self.emit('JZ', direction)
            if stride is None or stride > 0:
                limit = self.binary(ast.Sub(), self.literal(2147483647), step)
                safe = self.comparison(ast.LtE(), cursor, limit, node)
                overflow.append(self.emit('JZ', safe))
            if stride is None:
                joined = self.emit('JMP'); self.code[negative][4] = len(self.code)
            if stride is None or stride < 0:
                limit = self.binary(ast.Sub(), self.literal(-2147483648), step)
                safe = self.comparison(ast.GtE(), cursor, limit, node)
                overflow.append(self.emit('JZ', safe))
            if stride is None: self.code[joined][4] = len(self.code)
        self.emit('IADD', cursor, cursor, step); self.emit('JMP', imm=head)
        for jump in breaks + overflow + [end]: self.code[jump][4] = len(self.code)
        reason = 'optimisation is disabled' if not self.optimize else 'this loop needs ordered steps'
        if self.optimize and (breaks or continues): reason = 'break/continue changes which steps run'
        if self.optimize and first == 0 and stride == 1 and source is None and not breaks and not continues:
            reason = self.optimize_pixel_loop(head, cursor, bound, target)
        self.map_reports.append((node, self.code[head][0] == P_OP['PIXEL_MAP'], reason))
        self.env = before
        if positive:
            self.env[name] = target
            if not breaks and not continues: self.env.update(body_env)

    def common_pixel_store(self, body, index):
        """Join exclusive stores without changing when colours are evaluated."""
        if not body or not isinstance(body[-1], ast.If): return body
        stores = []
        def collect(nodes):
            if not nodes: return False
            last = nodes[-1]
            if any(isinstance(item, (ast.Return, ast.Break, ast.Continue, ast.For, ast.While)) for node in nodes[:-1] for item in ast.walk(node)):
                return False
            if isinstance(last, ast.If): return collect(last.body) and collect(last.orelse)
            if isinstance(last, ast.Assign) and len(last.targets) == 1:
                target = last.targets[0]
                if isinstance(target, ast.Subscript) and isinstance(target.slice, ast.Name) and target.slice.id == index:
                    try: ref, kind = self.resource(target.value)
                    except CompileError: return False
                    if kind in (12,14,15,16,17): stores.append((last, ref)); return True
            return False
        branch = copy.deepcopy(body[-1])
        if not collect([branch]) or len({ref for _, ref in stores}) != 1: return body
        # No source assignment to the index may change the delayed store's
        # location. The containing map analysis independently rejects effects.
        if any(isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store) and n.id == index for n in ast.walk(branch)):
            return body
        name = '__lightvm_colour_' + str(len(self.regs))
        while any(isinstance(n, ast.Name) and n.id == name for n in ast.walk(self.tree)): name += '_'
        destination = copy.deepcopy(stores[0][0].targets[0])
        for store, _ in stores: store.targets = [ast.copy_location(ast.Name(id=name, ctx=ast.Store()), store)]
        final = ast.copy_location(ast.Assign(targets=[destination], value=ast.Name(id=name, ctx=ast.Load())), body[-1])
        return [*body[:-1], branch, final]

    def constant_expression(self, node):
        """Check static expressions without evaluating guest source code."""
        if isinstance(node, ast.Constant) and type(node.value) in (int, float, bool): return
        if isinstance(node, ast.Name) and (node.id in self.module_values or self.imports.get(node.id) == 'math.pi'): return
        if isinstance(node, ast.Attribute) and self.imports.get(getattr(node.value, 'id', '')) == 'math' and node.attr == 'pi': return
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub, ast.Not, ast.Invert)):
            self.constant_expression(node.operand); return
        if isinstance(node, ast.BinOp):
            self.constant_expression(node.left); self.constant_expression(node.right); return
        if isinstance(node, ast.Call) and not node.keywords and isinstance(node.func, ast.Name):
            name = self.imports.get(node.func.id, node.func.id)
            if name in ('int', 'float', 'min', 'max', 'rgb', 'fixed', 'fixed_bits', 'fixed_from_bits', 'fixed_sqrt'):
                for argument in node.args: self.constant_expression(argument)
                return
        self.fail(node, 'Use a number, colour, or calculation with earlier constants here. Put changing values in the starting or frame function.')

    def optimize_pixel_loop(self, head, index, bound, source_target=None):
        """Recognize independent maps after normal typing/helper inlining.

        Keep unsafe/unsupported loops unchanged. The body retains its existing
        typed operations, including f32 rounding and scalar colour functions.
        """
        body = [list(ins) for ins in self.code[head+2:-2]]
        locations = self.code_locations[head+2:-2]
        sites = {}
        # A simple source target can be replaced by the private cursor inside
        # the map. Restore its last yielded value after a nonempty map.
        restore_target = source_target is not None
        if source_target is not None:
            if any(ins[1] == source_target and ins[0] not in (P_OP['STORE_ELEM'], P_OP['JZ'], P_OP['JMP'], P_OP['RESOURCE_FILL'], P_OP['RESOURCE_COPY']) for ins in body[1:]):
                return 'the body changes the loop variable'
            body.pop(0)
            locations = locations[1:]
            # All current kernel operands except branch targets/native site IDs
            # are registers. Native arguments are handled below separately.
            for ins in body:
                op = ins[0]
                positions = [2, 3] if op not in (P_OP['JMP'], P_OP['JZ'], P_OP['NATIVE']) else [1] if op == P_OP['JZ'] else []
                if op in (P_OP['RGB'], P_OP['HSV']): positions.append(4)
                for pos in positions:
                    if ins[pos] == source_target: ins[pos] = index
                if op in (P_OP['JMP'], P_OP['JZ']): ins[4] -= 1
            # Stage native operands too. Commit only after verifying the map.
            for ins in body:
                if ins[0] == P_OP['NATIVE']:
                    binding, args = self.calls[ins[4]]
                    sites[ins[4]] = (binding, [index if r == source_target else r for r in args])
        if not 1 <= len(body) <= 128:return 'the body exceeds the 1..128 instruction limit'
        unary = {P_OP[n] for n in ('MOV','ITOF','FTOI','LENGTH')}
        binary = {P_OP[n] for n in ('IADD','ISUB','IMUL','IDIV','IMOD','FADD','FSUB','FMUL','FDIV','FMOD','ILT','FLT','EQ','IMIN','IMAX','FMIN','FMAX','HASH')}
        unary.update(P_OP[n] for n,s in numeric.SIGNATURES.items() if len(s)==2)
        binary.update(P_OP[n] for n,s in numeric.SIGNATURES.items() if len(s)==3)
        written, stores, read_sets = set(), [], []
        first, end = head+2, head+2+len(body)
        for offset, (op,a,b,c,d) in enumerate(body):
            writes = op not in (P_OP['STORE_ELEM'],P_OP['JMP'],P_OP['JZ'])
            if op in unary:reads=[] if op==P_OP['LENGTH'] else [b]
            elif op in binary:reads=[b,c]
            elif op in (P_OP['RGB'],P_OP['HSV']):reads=[b,c,d]
            elif op==P_OP['NATIVE']:
                binding,reads=sites.get(d,self.calls[d])
                if binding not in (7,8,9,10,11) and binding not in {row['binding'] for row in numeric.NOISE_OPERATIONS.values()}:return 'a native call has state/RNG effects or is unsupported in a kernel'
            elif op in (P_OP['JMP'],P_OP['JZ']):
                if not first+offset<d<end:return 'the branch shape has a backward edge or can skip the final store'
                reads=[a] if op==P_OP['JZ'] else []
            elif op==P_OP['LOAD_ELEM']:
                if self.regs[b][0:2]!=(REF,CONST):return 'a gather needs a stable constant resource identity'
                if self.resources[self.regs[b][2]][0] not in (1,2,7,8,11,20):return 'the source resource is not a resident numeric array; feedback and aliasing stay ordered'
                reads=[c]
            elif op==P_OP['STORE_ELEM']:
                if b!=index:return 'a scatter store does not use the current pixel index'
                if self.resources[self.regs[a][2]][0] not in (3,12,14,15,16,17):return 'numeric source mutation must remain ordered'
                stores.append(a);reads=[c]
            else:return 'an instruction is unsupported in a pure pixel kernel'
            if writes:
                if self.regs[a][1]!=LOCAL:return 'persistent state writes must remain ordered'
                if a==index or a==bound:return 'the body changes loop control values'
                written.add(a)
            read_sets.append(reads)
        if len(stores)!=1 or body[-1][0]!=P_OP['STORE_ELEM']:return 'every path needs one common final pixel store'
        # Acyclic definite assignment. Reads of written locals must be defined
        # on every incoming path, so no iteration carries hidden dependencies.
        incoming=[None]*len(body);incoming[0]={index}
        work=[0]*(len(body)+1)
        for offset,(op,a,b,c,d) in enumerate(body):
            defined=incoming[offset]
            if defined is None:return 'the body contains an unreachable instruction'
            if any(r in written and r not in defined for r in read_sets[offset]):return 'a local has a loop-carried dependency or is not defined on every path'
            weight=numeric.instruction_work(op,self.calls[d][0] if op==P_OP['NATIVE'] else None)
            total=work[offset]+weight
            if total>numeric.MAP_WORK_BUDGET:return 'the body exceeds the 1024 first-pixel work budget'
            after=defined | ({a} if op not in (P_OP['STORE_ELEM'],P_OP['JMP'],P_OP['JZ']) else set())
            targets=[] if offset+1==len(body) else [offset+1]
            if op==P_OP['JMP']:targets=[d-first]
            elif op==P_OP['JZ']:targets.append(d-first)
            for target in targets:
                work[target]=max(work[target],total)
                incoming[target]=after.copy() if incoming[target] is None else incoming[target]&after
        for ins in body:
            if ins[0] in (P_OP['JMP'],P_OP['JZ']):ins[4]-=1
        end=head+1+len(body)
        self.code[head:]=[[P_OP['PIXEL_MAP'],stores[0],index,bound,end]]+body
        self.code_locations[head:] = [self.code_locations[head]] + locations
        for site, call in sites.items(): self.calls[site] = call
        if restore_target:
            nonempty = self.comparison(ast.Gt(), bound, self.literal(0), None)
            skip = self.emit('JZ', nonempty)
            self.emit('ISUB', source_target, bound, self.literal(1))
            self.code[skip][4] = len(self.code)

    def compile(self):
        marked = False
        minimum_release = None
        self.env = {}
        for n in self.tree.body:
            if isinstance(n, ast.Import):
                for item in n.names:
                    if item.name not in ('random', 'math'): self.fail(n, 'only random and math module imports supported')
                    self.imports[item.asname or item.name] = item.name
            elif isinstance(n, ast.ImportFrom):
                if not n.level and n.module in LIBRARIES:
                    self.import_library(n)
                    continue
                allowed = PUBLIC if n.module == 'lightvm' else {'array'} if n.module == 'array' else {'sin','pi','fabs','floor','ceil','trunc','cos','sqrt','log','exp','pow'} if n.module == 'math' else {'seed', 'random', 'randint', 'randrange'} if n.module == 'random' else set()
                if n.level or any(a.name not in allowed or a.asname for a in n.names): self.fail(n, 'unsupported import; animation decorators are @init and @render')
                for item in n.names: self.imports[item.name] = item.name if n.module == 'lightvm' else n.module + '.' + item.name
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and getattr(n.value.func, 'id', '') == 'requires':
                call = n.value
                if self.imports.get('requires') != 'requires' or minimum_release is not None or call.keywords or len(call.args) != 1 or not isinstance(call.args[0], ast.Constant):
                    self.fail(n, 'expected one requires("0.1.0") declaration')
                try: minimum_release = normalize_release(call.args[0].value)
                except ValueError as error: self.fail(n, str(error))
            elif isinstance(n, ast.Expr) and isinstance(n.value, ast.Call) and getattr(n.value.func, 'id', '') == 'profile':
                if self.imports.get('profile') != 'profile' or marked or n.value.keywords or len(n.value.args) != 1 or not isinstance(n.value.args[0], ast.Constant) or n.value.args[0].value not in ('animation', 'transition'): self.fail(n, 'expected one animation or transition profile declaration')
                self.profile_name = n.value.args[0].value
                marked = True
            elif isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.value,ast.Call) and self.imports.get(getattr(n.value.func,'id',''))=='PixelArray':
                self.image_declaration(n.targets[0],n.value,n,module=True)
            elif isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.value,ast.Call) and self.imports.get(getattr(n.value.func,'id',''))=='Sprite':
                self.sprite_definition(n.targets[0],n.value,n)
            elif isinstance(n,ast.Assign) and len(n.targets)==1 and isinstance(n.value,ast.Call) and self.imports.get(getattr(n.value.func,'id',''))=='Input':
                self.input_declaration(n.targets[0],n.value,n)
            elif isinstance(n, ast.Assign) and len(n.targets) == 1 and isinstance(n.targets[0], ast.Name):
                name = n.targets[0].id
                if name in self.module_values or name in self.imports or name in self.helpers or name in self.input_symbols or name in self.artwork:
                    self.fail(n, 'The name "' + name + '" is already in use.')
                self.constant_expression(n.value)
                self.module_values[name] = n.value
            elif isinstance(n, ast.FunctionDef):
                if n.args.posonlyargs or n.args.kwonlyargs or n.args.vararg or n.args.kwarg or n.args.kw_defaults: self.fail(n, 'Use named arguments, with optional starting values; *args and **kwargs are not supported.')
                if n.decorator_list and n.args.defaults: self.fail(n, 'Starting and frame functions receive their arguments automatically; leave out default values here.')
                for value in n.args.defaults: self.constant_expression(value)
                if len({a.arg for a in n.args.args}) != len(n.args.args): self.fail(n, 'duplicate argument')
                if n.name in self.helpers or any(fn.name == n.name for fn in self.funcs.values()) or n.name in self.imports or n.name in self.input_symbols: self.fail(n, 'duplicate function name')
                if any(a.arg in self.input_symbols for a in n.args.args): self.fail(n, 'function argument shadows an Input declaration')
                if not n.decorator_list: self.helpers[n.name] = n; continue
                if len(n.decorator_list) != 1 or not isinstance(n.decorator_list[0], ast.Name): self.fail(n, 'use @init or @render without arguments')
                role = n.decorator_list[0].id
                if role not in ('init', 'render') or self.imports.get(role) != role: self.fail(n, 'only @init and @render are supported')
                if role in self.funcs or len(n.args.args) != (2 if role == 'init' else 3): self.fail(n, '@init takes (ctx, runtime); @render takes (ctx, runtime, pixels)')
                self.funcs[role] = n
            elif not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)):
                self.fail(n, 'module state is unsupported; initialize ctx fields in @init')
        if not marked or 'render' not in self.funcs: raise CompileError('program requires a profile and @render entry')
        for name in self.module_values.keys() & self.imports.keys():
            self.fail(self.module_values[name], 'Choose a constant name that is different from an imported name.')
        for fn in (*self.helpers.values(), *self.funcs.values()):
            if any(arg.arg in self.input_symbols for arg in fn.args.args): self.fail(fn, 'function argument shadows an Input declaration')
            if fn.name in self.module_values: self.fail(fn, 'Choose a function name that is different from a shared constant.')
        if minimum_release and release_error(minimum_release, BASELINE):
            raise CompileError('source requires a release below the supported application baseline ' + BASELINE)
        functions = []
        for self.stage, role, self.rights in ((0, 'init', 9), (1, 'render', 13)):
            self.descriptions = {k:v for k,v in self.descriptions.items() if k.startswith('ctx.')}
            start = len(self.code); fn = self.funcs.get(role); self.env = {}; self.writes = set(); self.caps = {}; self.declarations = {}
            if role == 'init':
                for name, value in self.module_values.items():
                    reg = self.expr(value)
                    if self.regs[reg][1] != CONST:
                        stored = self.reg(self.typ(reg), STATE); self.emit('MOV', stored, reg); reg = stored
                    self.module_constants[name] = reg
            if fn:
                self.caps = {a.arg: cap for a, cap in zip(fn.args.args, ('ctx', 'runtime', 'pixels'))}
                if any(a.arg in self.imports for a in fn.args.args): self.fail(fn, 'caller argument shadows an import')
                self.statements(fn.body)
            self.emit('END')
            if role=='render':
                pending = bool(self.pending_batches)
                self.finish_batches()
                if pending: self.emit('END')
            functions.append((start, len(self.code), self.rights, 0, 0, len(self.inputs)))
        if len(self.code) > 2048: raise CompileError('code limit exceeded')
        self.allocate(functions)
        if len(self.code) > 2048 or len(self.table) > 1024 or len(self.calls) > 256: raise CompileError('portable package limits exceeded')
        manifest = {'schema':'lightvm.program','compiler_version':VERSION,'requires':minimum_release or BASELINE,'profile':self.profile_name,'pixels':{'format':'rgba8-premul'},
                    'settings':[{'name':n,'type':{I:'int',F:'float',C:'rgb8',B:'bool'}[self.typ(r)],'register':r} for n,r,*_ in self.settings]}
        if self.input_manifest: manifest['inputs'] = self.input_manifest
        def json_tokens(value):
            if isinstance(value, dict): return 1 + sum(1 + json_tokens(item) for item in value.values())
            if isinstance(value, list): return 1 + sum(map(json_tokens, value))
            return 1
        if json_tokens(manifest) > 256: raise CompileError('program metadata exceeds the native JSON token limit (256)')
        metadata = json.dumps(manifest,separators=(',',':'),allow_nan=False).encode()
        header = struct.pack('<4s23I', b'LBC1', 4, len(self.regs), len(self.code), len(self.table), len(functions), len(self.resources), len(self.expressions), len(self.calls), len(self.settings),
                             0, 1, 0, 0, 0, 0, 0, 0, 4 if self.profile_name == 'transition' else 3, len(metadata), 0, 0, 0, 0)
        raw = header + b''.join(struct.pack('<BBHI',t,role,0,v) for t,role,v in self.regs)
        raw += b''.join(struct.pack('<BBBBI',*ins) for ins in self.code)
        raw += b''.join(struct.pack('<I',c) for c in self.table)
        raw += b''.join(struct.pack('<6I',*fn) for fn in functions)
        raw += b''.join(struct.pack('<6I',*resource) for resource in self.resources)
        raw += b''.join(struct.pack('<BBHIII',op,typ,0,a,b,v) for op,typ,a,b,v in self.expressions)
        raw += b''.join(struct.pack('<II8B',binding,len(args),*(args+[0]*(8-len(args)))) for binding,args in self.calls)
        raw += b''.join(struct.pack('<4I',r,d,lo,hi) for _,r,d,lo,hi in self.settings)
        raw += metadata
        if len(raw) > 32768 or len(metadata) > 8192: raise CompileError('package limit exceeded')
        return raw, manifest

    def allocate(self, functions):
        """Typed liveness allocation. Persistent/input/constant bindings never alias."""
        # Each operand position's register meaning; immediate targets stay intact.
        binary = {P_OP[x] for x in ('IADD','ISUB','IMUL','IDIV','IMOD','FADD','FSUB','FMUL','FDIV','FMOD','ILT','FLT','EQ','IMIN','IMAX','FMIN','FMAX','HASH')}
        unary = {P_OP[x] for x in ('MOV','ITOF','FTOI','LENGTH')}
        binary.update(P_OP[name] for name, sig in numeric.SIGNATURES.items() if len(sig) == 3)
        unary.update(P_OP[name] for name, sig in numeric.SIGNATURES.items() if len(sig) == 2)
        def operands(ins):
            op,a,b,c,d = ins
            if op in binary: return [1], [2,3]
            if op in unary: return [1], [2]
            if op in (P_OP['RGB'],P_OP['HSV']): return [1], [2,3,4]
            if op == P_OP['LOAD_ELEM']: return [1], [2,3]
            if op == P_OP['STORE_ELEM']: return [], [1,2,3]
            if op == P_OP['RESOURCE_FILL']: return [], [1,2,3,4]
            if op == P_OP['RESOURCE_COPY']: return [], [1,2]
            if op == P_OP['PIXEL_MAP']: return [2], [1,3]
            if op in (P_OP['RETURN'],P_OP['JZ']): return [], [1]
            if op == P_OP['NATIVE']: return [1] if a != -1 else [], []
            return [], []
        graph = [set() for _ in self.regs]
        # A map writes its index while retaining the bound across every chunk.
        for op, _, index, bound, _ in self.code:
            if op == P_OP['PIXEL_MAP']:
                graph[index].add(bound); graph[bound].add(index)
        for start,end,*_ in functions:
            map_tails = {ins[4]-1:pc for pc,ins in enumerate(self.code[start:end],start) if ins[0]==P_OP['PIXEL_MAP']}
            live = [set() for _ in range(end-start)]
            changed = True
            while changed:
                changed = False
                for pc in range(end-1,start-1,-1):
                    ins = self.code[pc]; defs,uses = operands(ins)
                    following = [] if ins[0] in (P_OP['END'],P_OP['RETURN']) else [pc+1] if pc+1 < end else []
                    if ins[0] == P_OP['JMP']: following = [ins[4]]
                    elif ins[0] == P_OP['JZ']: following += [ins[4]]
                    elif ins[0] == P_OP['PIXEL_MAP']: following += [ins[4]]
                    if pc in map_tails:following += [map_tails[pc]]
                    after = set().union(*(live[j-start] for j in following))
                    defined = {ins[k] for k in defs}; used = {ins[k] for k in uses}
                    if ins[0] == P_OP['NATIVE']: used.update(self.calls[ins[4]][1])
                    if self.debug: used.update(self.checkpoints.get(pc, {}).get('variables', {}).values())
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
            if ins[0] == P_OP['PIXEL_MAP'] and ins[2] == ins[3]:
                raise CompileError('internal error: pixel map index aliases its bound')
            if ins[0] == P_OP['NATIVE'] and ins[1] == -1: ins[1] = 255
        self.calls = [(binding,[mapping[r] for r in args]) for binding,args in self.calls]
        self.expressions = [(op,typ,mapping[a] if op == 2 else a,b,v) for op,typ,a,b,v in self.expressions]
        self.settings = [(n,mapping[r],d,lo,hi) for n,r,d,lo,hi in self.settings]
        self.checkpoints = {pc: {**point, 'variables': {name: mapping[r] for name, r in point['variables'].items()}} for pc, point in self.checkpoints.items() if pc < len(self.code)}
        self.regs = physical
