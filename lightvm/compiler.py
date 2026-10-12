"""Original restricted-Python compiler for the memoryless LightVM control ABI.

MIT License, copyright 2026 Kaj Wik Siebert. Source is parsed, never executed.
"""
import argparse
import ast
from dataclasses import dataclass, field
import json
import math
import symtable
from pathlib import Path


@dataclass
class Diagnostic:
    message: str
    filename: str
    line: int
    column: int


@dataclass
class Compilation:
    wasm: bytes = b""
    manifest: dict = field(default_factory=dict)
    diagnostics: list = field(default_factory=list)
    wat: str = ""

    @property
    def success(self):
        return not self.diagnostics


class CompileError(Exception):
    def __init__(self, node, message):
        self.node, self.message = node, message


@dataclass
class Expression:
    kind: str
    code: str


TYPES = {"int": "i64", "float": "f64", "bool": "i32", "RGB": "i32"}
INTRINSICS = {"int", "float", "bool", "min", "max", "abs", "len", "range"}
IMPORTS = {
    "resource": ("i32", ["i32"]), "count": ("i64", ["i32"]),
    "write": (None, ["i32", "i64", "i32"]), "fill": (None, ["i32", "i32"]),
    "erase": (None, ["i32", "i64"]), "read": ("i32", ["i32", "i64"]),
    "present": ("i32", ["i32", "i64"]), "rgb": ("i32", ["i64"] * 3),
    "hsv": ("i32", ["f64"] * 3), "param_int": ("i64", ["i32"]),
    "param_float": ("f64", ["i32"]), "param_bool": ("i32", ["i32"]),
    "param_color": ("i32", ["i32"]),
}
HELPERS = '''
(func $idiv (param $a i64) (param $b i64) (result i64) (local $q i64)
  (local.set $q (i64.div_s (local.get $a) (local.get $b)))
  (if (i32.and (i64.ne (i64.rem_s (local.get $a) (local.get $b)) (i64.const 0))
              (i64.lt_s (i64.xor (local.get $a) (local.get $b)) (i64.const 0)))
    (then (local.set $q (i64.sub (local.get $q) (i64.const 1))))) (local.get $q))
(func $imod (param $a i64) (param $b i64) (result i64) (local $r i64)
  (local.set $r (i64.rem_s (local.get $a) (local.get $b)))
  (if (i32.and (i64.ne (local.get $r) (i64.const 0))
              (i64.lt_s (i64.xor (local.get $r) (local.get $b)) (i64.const 0)))
    (then (local.set $r (i64.add (local.get $r) (local.get $b))))) (local.get $r))
(func $fdiv (param $a f64) (param $b f64) (result f64)
  (if (f64.eq (local.get $b) (f64.const 0)) (then unreachable))
  (f64.div (local.get $a) (local.get $b)))
(func $fmod (param $a f64) (param $b f64) (result f64)
  (f64.sub (local.get $a) (f64.mul (f64.floor (call $fdiv (local.get $a) (local.get $b))) (local.get $b))))
(func $imin (param $a i64) (param $b i64) (result i64)
  (select (local.get $a) (local.get $b) (i64.lt_s (local.get $a) (local.get $b))))
(func $imax (param $a i64) (param $b i64) (result i64)
  (select (local.get $a) (local.get $b) (i64.gt_s (local.get $a) (local.get $b))))
(func $iabs (param $a i64) (result i64)
  (if (result i64) (i64.lt_s (local.get $a) (i64.const 0))
    (then (i64.sub (i64.const 0) (local.get $a))) (else (local.get $a))))
'''


def uleb(n):
    out = bytearray()
    while n >= 128:
        out.append((n & 127) | 128)
        n >>= 7
    out.append(n)
    return bytes(out)


class Compiler:
    def __init__(self, source):
        self.tree = ast.parse(source)
        symtable.symtable(source, "<source>", "exec")
        self.aliases, self.functions, self.signatures = {}, {}, {}
        self.globals, self.parameters, self.palettes = {}, {}, {}
        self.handlers, self.calls, self.resource = {}, {}, None
        self.serial = 0

    def fail(self, node, message):
        raise CompileError(node, message)

    def typename(self, node):
        if isinstance(node, ast.Name) and self.aliases.get(node.id, node.id) in TYPES:
            return self.aliases.get(node.id, node.id)
        self.fail(node, "use int, float, bool or RGB type annotations")

    def literal(self, node):
        if isinstance(node, ast.Call) and self.aliases.get(getattr(node.func, "id", "")) == "rgb":
            if len(node.args) != 3 or node.keywords:
                self.fail(node, "rgb requires three channels")
            parts = [self.literal(a) for a in node.args]
            if any(type(v) is not int or not 0 <= v <= 255 for v in parts):
                self.fail(node, "RGB channels must be integers in 0..255")
            return (parts[0] << 16) | (parts[1] << 8) | parts[2]
        try:
            value = ast.literal_eval(node)
        except (ValueError, TypeError, SyntaxError):
            self.fail(node, "declarations require literal values")
        if type(value) not in (bool, int, float, str, tuple):
            self.fail(node, "unsupported declaration value")
        if type(value) is int and not -(1 << 63) <= value < (1 << 63):
            self.fail(node, "integer must fit signed 64 bits")
        if type(value) is float and not math.isfinite(value):
            self.fail(node, "finite values required")
        return value

    def declarations(self):
        easing_names = {n.name for n in ast.parse(Path(__file__).with_name("easing.py").read_text()).body if isinstance(n, ast.FunctionDef)}
        allowed = {"pixels", "parameter", "rgb", "hsv", "hash01", "on_start", "on_frame", "RGB", "Context"}
        # Import validation is explicit; no Python import of user-specified names occurs.
        for n in self.tree.body:
            if isinstance(n, ast.ImportFrom):
                names = easing_names if n.module == "lightvm.easing" else allowed if n.module == "lightvm" else set()
                if n.level or any(a.name not in names for a in n.names):
                    self.fail(n, "only explicit LightVM SDK imports are supported")
                for a in n.names:
                    alias = a.asname or a.name
                    if alias in self.aliases or alias in INTRINSICS:
                        self.fail(n, "duplicate import name")
                    self.aliases[alias] = a.name
            elif isinstance(n, ast.FunctionDef):
                if n.name in self.functions or n.name in self.aliases or n.name in INTRINSICS:
                    self.fail(n, "duplicate or reserved function name")
                self.functions[n.name] = n
            elif isinstance(n, (ast.Assign, ast.AnnAssign)):
                targets = n.targets if isinstance(n, ast.Assign) else [n.target]
                if len(targets) != 1 or not isinstance(targets[0], ast.Name) or n.value is None:
                    self.fail(n, "module declarations require one named initializer")
                name, value = targets[0].id, n.value
                if name in self.globals or name in self.parameters or name in self.palettes or name in self.aliases or name in INTRINSICS or (self.resource and name == self.resource[0]):
                    self.fail(n, "duplicate or reserved declaration")
                builtin = self.aliases.get(getattr(getattr(value, "func", None), "id", ""))
                if isinstance(value, ast.Call) and builtin == "pixels":
                    kw = {k.arg: self.literal(k.value) for k in value.keywords}
                    if self.resource or len(value.args) != 1 or set(kw) - {"retention"} or len(kw) != len(value.keywords):
                        self.fail(n, "declare exactly one pixel resource")
                    binding, retention = self.literal(value.args[0]), kw.get("retention", "fresh")
                    if binding != "A" or retention not in ("fresh", "retained"):
                        self.fail(n, "initial profile supports A with fresh or retained pixels")
                    self.resource = (name, retention)
                elif isinstance(value, ast.Call) and builtin == "parameter":
                    kw = {k.arg: self.literal(k.value) for k in value.keywords}
                    if len(value.args) != 2 or set(kw) - {"minimum", "maximum", "kind"} or len(kw) != len(value.keywords):
                        self.fail(n, "parameter requires name, default and optional bounds/kind")
                    label, default = (self.literal(a) for a in value.args)
                    kind = kw.get("kind", "bool" if type(default) is bool else "float" if type(default) is float else "int")
                    if kind not in ("bool", "float", "int", "rgb8") or not isinstance(label, str) or not label or len(label) > 32 or not all(c.isascii() and (c.isalnum() or c in "_.-") for c in label):
                        self.fail(n, "invalid parameter name or type")
                    if len(self.parameters) == 8 or any(p[1]["name"] == label for p in self.parameters.values()):
                        self.fail(n, "at most eight uniquely named parameters")
                    expected = {"bool": bool, "float": float, "int": int, "rgb8": int}[kind]
                    if type(default) is not expected:
                        self.fail(n, "parameter default must match its type")
                    lo, hi = kw.get("minimum"), kw.get("maximum")
                    if kind in ("bool", "rgb8") and (lo is not None or hi is not None):
                        self.fail(n, "bounds only apply to numeric parameters")
                    if any(v is not None and type(v) is not expected for v in (lo, hi)) or (lo is not None and default < lo) or (hi is not None and default > hi) or (kind == "rgb8" and not 0 <= default <= 0xffffff):
                        self.fail(n, "invalid parameter bounds/default")
                    item = {"name": label, "type": kind, "default": default}
                    if lo is not None: item["minimum"] = lo
                    if hi is not None: item["maximum"] = hi
                    self.parameters[name] = (len(self.parameters), item)
                elif isinstance(value, ast.Tuple) and value.elts and all(isinstance(e, ast.Call) for e in value.elts):
                    colors = []
                    for e in value.elts:
                        v = self.literal(e)
                        if type(v) is not int or not 0 <= v <= 0xffffff:
                            self.fail(e, "palettes contain literal RGB8 colours")
                        colors.append(v)
                    if len(colors) > 16: self.fail(n, "palettes have at most sixteen colours")
                    self.palettes[name] = colors
                else:
                    v = self.literal(value)
                    kind = "RGB" if builtin == "rgb" else "bool" if type(v) is bool else "float" if type(v) is float else "int" if type(v) is int else None
                    if kind is None: self.fail(n, "module state must be scalar")
                    if isinstance(n, ast.AnnAssign) and self.typename(n.annotation) != kind:
                        self.fail(n, "initializer must match its annotation")
                    self.globals[name] = (kind, v)
            elif not (isinstance(n, ast.Expr) and isinstance(n.value, ast.Constant) and isinstance(n.value.value, str)):
                self.fail(n, "unsupported module statement; source is never executed by the compiler")
        if not self.resource: self.fail(self.tree, "declare one pixels('A') resource")
        reserved = set(self.globals) | set(self.parameters) | set(self.palettes) | {self.resource[0]}
        if reserved & set(self.functions): self.fail(self.tree, "function and declaration names must be distinct")
        # Include only original, locally authored mathematical helpers.
        helpers = [n for n in ast.parse(Path(__file__).with_name("easing.py").read_text()).body if isinstance(n, ast.FunctionDef)]
        sdk = ast.parse(Path(__file__).with_name("__init__.py").read_text())
        helpers += [n for n in sdk.body if isinstance(n, ast.FunctionDef) and n.name == "hash01"]
        for h in helpers:
            internal = "sdk." + h.name
            self.functions[internal] = h
        self.sdk = {h.name: "sdk." + h.name for h in helpers}
        for name, f in self.functions.items():
            if not name.startswith("sdk."):
                protected = set(self.aliases) | set(self.parameters) | set(self.palettes) | INTRINSICS | {self.resource[0]}
                if any(a.arg in protected for a in f.args.args):
                    self.fail(f, "arguments cannot shadow SDK declarations or builtins")
            if f.args.defaults or f.args.kw_defaults or f.args.vararg or f.args.kwarg or f.args.kwonlyargs or f.args.posonlyargs or len(f.decorator_list) > 1:
                self.fail(f, "functions have fixed positional arguments")
            trigger = self.aliases.get(getattr(f.decorator_list[0], "id", "")) if f.decorator_list else None
            if f.decorator_list and trigger not in ("on_start", "on_frame"):
                self.fail(f, "only @on_start and @on_frame are supported")
            if trigger:
                if trigger in self.handlers or len(f.args.args) != 1 or f.returns is not None:
                    self.fail(f, "one handler per trigger, with one context argument and no return annotation")
                self.handlers[trigger] = name
                self.signatures[name] = (["context"], None)
            else:
                if f.returns is None: self.fail(f, "helper functions require a return type annotation")
                self.signatures[name] = ([self.typename(a.annotation or a) for a in f.args.args], self.typename(f.returns))
        if "on_frame" not in self.handlers: self.fail(self.tree, "declare an @on_frame handler")

    def cast(self, e, kind, node):
        if e.kind == kind: return e.code
        if kind == "float" and e.kind in ("int", "bool"):
            return f'(f64.convert_{TYPES[e.kind]}_s {e.code})'
        self.fail(node, f"expected {kind}, got {e.kind}")

    def truth(self, e):
        if e.kind == "bool": return e.code
        if e.kind == "int": return f'(i64.ne {e.code} (i64.const 0))'
        if e.kind == "float": return f'(f64.ne {e.code} (f64.const 0))'
        self.fail(self.current, "conditions require numeric or boolean values")

    def const(self, kind, value):
        return f'({TYPES[kind]}.const {int(value) if kind != "float" else repr(value)})'

    def expr(self, n):
        if isinstance(n, ast.Constant):
            v = self.literal(n)
            kind = "bool" if type(v) is bool else "int" if type(v) is int else "float" if type(v) is float else None
            if not kind: self.fail(n, "expression must be scalar")
            return Expression(kind, self.const(kind, v))
        if isinstance(n, ast.Name):
            if n.id in self.locals:
                if n.id not in self.assigned: self.fail(n, "local may be read before assignment")
                return Expression(self.locals[n.id], f'(local.get ${n.id})')
            if n.id in self.globals:
                if n.id in self.local_names and n.id not in self.global_names:
                    self.fail(n, "declare global before reading/writing module state")
                return Expression(self.globals[n.id][0], f'(global.get $g_{n.id})')
            self.fail(n, f"unknown scalar {n.id}")
        if isinstance(n, ast.Attribute):
            base = getattr(n.value, "id", "")
            if self.locals.get(base) == "context":
                if n.attr in ("program_us", "delta_us"):
                    return Expression("int", f'(local.get $context.{n.attr})')
                if n.attr in ("time", "delta"):
                    var = "program_us" if n.attr == "time" else "delta_us"
                    return Expression("float", f'(f64.div (f64.convert_i64_s (local.get $context.{var})) (f64.const 1000000))')
            if base in self.parameters and n.attr == "value":
                slot, p = self.parameters[base]
                typ = "RGB" if p["type"] == "rgb8" else p["type"]
                imp = "color" if typ == "RGB" else typ
                return Expression(typ, f'(call $param_{imp} (i32.const {slot}))')
            if base == self.resource[0] and n.attr == "count":
                return Expression("int", '(call $count (global.get $handle))')
            self.fail(n, "unsupported property")
        if isinstance(n, ast.Subscript):
            base = getattr(n.value, "id", "")
            index = self.cast(self.expr(n.slice), "int", n)
            if base == self.resource[0]: return Expression("RGB", f'(call $read (global.get $handle) {index})')
            if base in self.palettes: return Expression("RGB", f'(call $palette_{base} {index})')
            self.fail(n, "only pixels and immutable palettes can be indexed")
        if isinstance(n, ast.UnaryOp):
            if isinstance(n.op, ast.USub) and isinstance(n.operand, ast.Constant):
                v = self.literal(n)
                typ = "float" if type(v) is float else "int"
                return Expression(typ, self.const(typ, v))
            e = self.expr(n.operand)
            if isinstance(n.op, ast.Not): return Expression("bool", f'(i32.eqz {self.truth(e)})')
            if e.kind not in ("int", "float"): self.fail(n, "numeric operand required")
            if isinstance(n.op, ast.UAdd): return e
            if isinstance(n.op, ast.USub): return Expression(e.kind, f'({TYPES[e.kind]}.sub ({TYPES[e.kind]}.const 0) {e.code})')
            if isinstance(n.op, ast.Invert) and e.kind == "int": return Expression("int", f'(i64.xor {e.code} (i64.const -1))')
        if isinstance(n, ast.BinOp):
            a, b = self.expr(n.left), self.expr(n.right)
            if a.kind not in ("int", "float") or b.kind not in ("int", "float"): self.fail(n, "numeric operands required")
            typ = "float" if "float" in (a.kind, b.kind) or isinstance(n.op, ast.Div) else "int"
            ac, bc = self.cast(a, typ, n), self.cast(b, typ, n)
            op = {ast.Add: "add", ast.Sub: "sub", ast.Mult: "mul", ast.BitAnd: "and", ast.BitOr: "or", ast.BitXor: "xor", ast.LShift: "shl", ast.RShift: "shr_s"}.get(type(n.op))
            if op:
                if op in ("and", "or", "xor", "shl", "shr_s") and typ != "int": self.fail(n, "integer operands required")
                if op in ("shl", "shr_s"): self.fail(n, "shift operators are outside the initial subset")
                return Expression(typ, f'({TYPES[typ]}.{op} {ac} {bc})')
            if isinstance(n.op, ast.Div): return Expression("float", f'(call $fdiv {ac} {bc})')
            if isinstance(n.op, ast.FloorDiv): return Expression(typ, f'(f64.floor (call $fdiv {ac} {bc}))' if typ == "float" else f'(call $idiv {ac} {bc})')
            if isinstance(n.op, ast.Mod): return Expression(typ, f'(call ${"fmod" if typ == "float" else "imod"} {ac} {bc})')
        if isinstance(n, ast.Compare):
            if len(n.ops) != 1: self.fail(n, "write chained comparisons as explicit boolean conditions")
            a, b = self.expr(n.left), self.expr(n.comparators[0])
            typ = "float" if "float" in (a.kind, b.kind) else a.kind
            ac, bc = self.cast(a, typ, n), self.cast(b, typ, n)
            op = {ast.Eq: "eq", ast.NotEq: "ne", ast.Lt: "lt", ast.LtE: "le", ast.Gt: "gt", ast.GtE: "ge"}.get(type(n.ops[0]))
            if not op: self.fail(n, "unsupported comparison")
            if typ == "RGB" and op not in ("eq", "ne"):
                self.fail(n, "RGB values support equality, not ordering")
            suffix = "_s" if typ in ("int", "bool") and op not in ("eq", "ne") else ""
            return Expression("bool", f'({TYPES[typ]}.{op}{suffix} {ac} {bc})')
        if isinstance(n, ast.BoolOp):
            values = [self.expr(v) for v in n.values]
            if any(v.kind != "bool" for v in values):
                self.fail(n, "and/or operands must be boolean in this subset")
            parts = [v.code for v in values]
            code = parts[-1]
            for part in reversed(parts[:-1]):
                code = f'(if (result i32) {part} (then {code}) (else (i32.const 0)))' if isinstance(n.op, ast.And) else f'(if (result i32) {part} (then (i32.const 1)) (else {code}))'
            return Expression("bool", code)
        if isinstance(n, ast.IfExp):
            condition = self.truth(self.expr(n.test))
            a, b = self.expr(n.body), self.expr(n.orelse)
            typ = "float" if "float" in (a.kind, b.kind) else a.kind
            return Expression(typ, f'(if (result {TYPES[typ]}) {condition} (then {self.cast(a, typ, n)}) (else {self.cast(b, typ, n)}))')
        if isinstance(n, ast.Call):
            if n.keywords: self.fail(n, "runtime calls use positional arguments")
            if isinstance(n.func, ast.Attribute) and getattr(n.func.value, "id", "") == self.resource[0]:
                method = n.func.attr
                kinds = {"fill": ["RGB"], "clear": [], "erase": ["int"], "present": ["int"]}
                if method not in kinds or len(n.args) != len(kinds[method]): self.fail(n, "invalid pixel operation")
                args = [self.cast(self.expr(a), t, a) for a, t in zip(n.args, kinds[method])]
                if method == "clear": method, args = "fill", ['(i32.const 0)']
                return Expression("bool" if method == "present" else "void", f'(call ${method} (global.get $handle) {" ".join(args)})')
            name = getattr(n.func, "id", "")
            if name in self.locals:
                self.fail(n, "scalar values are not callable")
            builtin = name if self.current_name.startswith("sdk.") else self.aliases.get(name, name)
            if builtin in ("rgb", "hsv"):
                typ = "int" if builtin == "rgb" else "float"
                if len(n.args) != 3: self.fail(n, "colour helper requires three components")
                return Expression("RGB", f'(call ${builtin} ' + ' '.join(self.cast(self.expr(a), typ, a) for a in n.args) + ')')
            if builtin == "len" and len(n.args) == 1 and isinstance(n.args[0], ast.Name):
                base = n.args[0].id
                if base in self.palettes: return Expression("int", f'(i64.const {len(self.palettes[base])})')
                if base == self.resource[0]: return Expression("int", '(call $count (global.get $handle))')
            if builtin in ("int", "float", "bool") and len(n.args) == 1:
                e = self.expr(n.args[0])
                if builtin == "bool": return Expression("bool", self.truth(e))
                if builtin == e.kind: return e
                if builtin == "float": return Expression("float", self.cast(e, "float", n))
                if e.kind == "float": return Expression("int", f'(i64.trunc_f64_s {e.code})')
                if e.kind in ("bool", "RGB"): return Expression("int", f'(i64.extend_i32_u {e.code})')
            if builtin in ("min", "max") and len(n.args) == 2:
                a, b = map(self.expr, n.args)
                typ = "float" if "float" in (a.kind, b.kind) else "int"
                ac, bc = self.cast(a, typ, n), self.cast(b, typ, n)
                return Expression(typ, f'(f64.{builtin} {ac} {bc})' if typ == "float" else f'(call $i{builtin} {ac} {bc})')
            if builtin == "abs" and len(n.args) == 1:
                e = self.expr(n.args[0])
                if e.kind == "float": return Expression("float", f'(f64.abs {e.code})')
                if e.kind == "int": return Expression("int", f'(call $iabs {e.code})')
            target = self.sdk.get(builtin) if builtin in self.sdk and (name in self.aliases or self.current_name.startswith("sdk.")) else name
            if target in self.functions and target not in self.handlers.values():
                kinds, result = self.signatures[target]
                if len(n.args) != len(kinds): self.fail(n, "wrong number of function arguments")
                self.calls[self.current_name].add(target)
                return Expression(result, f'(call $fn_{target} ' + ' '.join(self.cast(self.expr(a), t, a) for a, t in zip(n.args, kinds)) + ')')
        self.fail(n, f"unsupported expression: {type(n).__name__}")

    def assign(self, target, e):
        if isinstance(target, ast.Name):
            name = target.id
            if name in self.global_names:
                if name not in self.globals: self.fail(target, "global must have a module initializer")
                typ = self.globals[name][0]
                return f'(global.set $g_{name} {self.cast(e, typ, target)})'
            if name in self.parameters or name in self.palettes or name in self.aliases or name in INTRINSICS or name == self.resource[0]: self.fail(target, "cannot reassign a declaration or builtin")
            if e.kind not in TYPES: self.fail(target, "only scalar local state is supported")
            typ = self.locals.setdefault(name, e.kind)
            self.assigned.add(name)
            return f'(local.set ${name} {self.cast(e, typ, target)})'
        if isinstance(target, ast.Subscript) and getattr(target.value, "id", "") == self.resource[0]:
            color = self.cast(e, "RGB", target)
            index = self.cast(self.expr(target.slice), "int", target)
            self.serial += 1
            temporary = f'_rhs.{self.serial}'
            self.locals[temporary] = "RGB"
            # Python evaluates the RHS before evaluating the assignment target.
            return f'(local.set ${temporary} {color}) (call $write (global.get $handle) {index} (local.get ${temporary}))'
        self.fail(target, "assign a scalar or indexed pixel")

    def statements(self, body):
        lines = []
        for n in body:
            if isinstance(n, ast.Global): continue
            if isinstance(n, ast.Assign) and len(n.targets) == 1:
                lines.append(self.assign(n.targets[0], self.expr(n.value)))
            elif isinstance(n, ast.AnnAssign) and n.value is not None:
                e = self.expr(n.value)
                if e.kind != self.typename(n.annotation): self.fail(n, "initializer must match annotation")
                lines.append(self.assign(n.target, e))
            elif isinstance(n, ast.AugAssign):
                # Pixel augmented assignments would evaluate an index twice; disallow them.
                if not isinstance(n.target, ast.Name): self.fail(n, "augmented assignment requires scalar state")
                lines.append(self.assign(n.target, self.expr(ast.copy_location(ast.BinOp(n.target, n.op, n.value), n))))
            elif isinstance(n, ast.Expr):
                if isinstance(n.value, ast.Constant) and isinstance(n.value.value, str):
                    continue
                e = self.expr(n.value)
                lines.append(e.code if e.kind == "void" else f'(drop {e.code})')
            elif isinstance(n, ast.Return):
                if self.return_type is None:
                    if n.value is not None and not (isinstance(n.value, ast.Constant) and n.value.value is None): self.fail(n, "handlers return no value")
                    lines.append('(return)')
                else:
                    if n.value is None: self.fail(n, "typed functions must return a value")
                    lines.append(f'(return {self.cast(self.expr(n.value), self.return_type, n)})')
            elif isinstance(n, ast.If):
                condition = self.truth(self.expr(n.test))
                before = self.assigned.copy()
                yes = self.statements(n.body)
                after_yes = self.assigned.copy()
                self.assigned = before.copy()
                no = self.statements(n.orelse)
                self.assigned &= after_yes
                lines.append(f'(if {condition} (then {yes}) (else {no}))')
            elif isinstance(n, ast.For):
                if n.orelse or not isinstance(n.target, ast.Name) or not isinstance(n.iter, ast.Call) or not isinstance(n.iter.func, ast.Name) or n.iter.func.id != "range" or n.iter.keywords or not 1 <= len(n.iter.args) <= 3:
                    self.fail(n, "loops use for NAME in range(...), without else")
                args = [self.cast(self.expr(a), "int", a) for a in n.iter.args]
                start, stop, step = ('(i64.const 0)', args[0], '(i64.const 1)') if len(args) == 1 else (args[0], args[1], args[2] if len(args) == 3 else '(i64.const 1)')
                self.serial += 1
                label, end, stride, old = (f'_loop.{self.serial}.{s}' for s in ('block', 'end', 'stride', 'old'))
                for v in (end, stride, old): self.locals[v] = "int"
                var = n.target.id
                if var in self.global_names: self.fail(n, "range iteration variables must be local")
                before = self.assigned.copy()
                iterator = f'_loop.{self.serial}.iterator'
                self.locals[iterator] = "int"
                assign_iteration = self.assign(n.target, Expression("int", f'(local.get ${iterator})'))
                self.loops.append(label)
                inner = self.statements(n.body)
                self.loops.pop()
                self.assigned = before
                lines.append(f'''(local.set ${iterator} {start})
                  (local.set ${end} {stop}) (local.set ${stride} {step})
                  (if (i64.eqz (local.get ${stride})) (then unreachable))
                  (block ${label}_break (loop ${label}_repeat
                    (br_if ${label}_break (i32.eqz
                      (if (result i32) (i64.gt_s (local.get ${stride}) (i64.const 0))
                        (then (i64.lt_s (local.get ${iterator}) (local.get ${end})))
                        (else (i64.gt_s (local.get ${iterator}) (local.get ${end}))))))
                    {assign_iteration} (block ${label}_continue {inner})
                    (local.set ${old} (local.get ${iterator}))
                    (local.set ${iterator} (i64.add (local.get ${iterator}) (local.get ${stride})))
                    (br_if ${label}_break
                      (if (result i32) (i64.gt_s (local.get ${stride}) (i64.const 0))
                        (then (i64.lt_s (local.get ${iterator}) (local.get ${old})))
                        (else (i64.gt_s (local.get ${iterator}) (local.get ${old})))))
                    (br ${label}_repeat)))''')
            elif isinstance(n, (ast.Break, ast.Continue)):
                if not self.loops: self.fail(n, "loop control outside a loop")
                lines.append(f'(br ${self.loops[-1]}_{"break" if isinstance(n, ast.Break) else "continue"})')
            elif isinstance(n, ast.Pass): pass
            else: self.fail(n, f"unsupported statement: {type(n).__name__}")
        return '\n'.join(lines)

    def function(self, name, f):
        self.current, self.current_name = f, name
        argtypes, self.return_type = self.signatures[name]
        self.locals = {a.arg: t for a, t in zip(f.args.args, argtypes)}
        self.assigned, self.loops = set(self.locals), []
        self.global_names = {s for n in ast.walk(f) if isinstance(n, ast.Global) for s in n.names}
        self.local_names = {n.id for n in ast.walk(f) if isinstance(n, ast.Name) and isinstance(n.ctx, ast.Store)}
        if self.global_names & set(self.locals): self.fail(f, "argument cannot be global")
        self.calls[name] = set()
        def returns(body):
            for statement in body:
                if isinstance(statement, ast.Return): return True
                if isinstance(statement, ast.If) and returns(statement.body) and returns(statement.orelse): return True
            return False
        if self.return_type and not returns(f.body):
            self.fail(f, "typed functions must return a value on every path")
        code = self.statements(f.body)
        params = '(param $context.program_us i64) (param $context.delta_us i64)' if argtypes == ["context"] else ' '.join(f'(param ${a.arg} {TYPES[t]})' for a, t in zip(f.args.args, argtypes))
        locals_code = ' '.join(f'(local ${n} {TYPES[t]})' for n, t in self.locals.items() if n not in {a.arg for a in f.args.args})
        result = f'(result {TYPES[self.return_type]})' if self.return_type else ''
        return f'(func $fn_{name} {params} {result} {locals_code}\n{code}\n{"unreachable" if self.return_type else ""})'

    def build(self):
        self.declarations()
        functions = [self.function(name, f) for name, f in self.functions.items()]
        def visit(name, stack):
            if name in stack: self.fail(self.functions[name], "recursive functions are unsupported")
            for child in self.calls[name]: visit(child, stack | {name})
        for name in self.functions: visit(name, set())
        imports = [f'(import "lightvm.control.v1" "{n}" (func ${n} {" ".join(f"(param {t})" for t in args)} {f"(result {ret})" if ret else ""}))' for n, (ret, args) in IMPORTS.items()]
        globals_code = [f'(global $g_{n} (mut {TYPES[t]}) {self.const(t, v)})' for n, (t, v) in self.globals.items()]
        palettes = [f'(func $palette_{name} (param $index i64) (result i32) ' + ' '.join(f'(if (i64.eq (local.get $index) (i64.const {i})) (then (return (i32.const {color}))))' for i, color in enumerate(colors)) + ' unreachable)' for name, colors in self.palettes.items()]
        start = f'(call $fn_{self.handlers["on_start"]} (local.get $time) (local.get $delta))' if "on_start" in self.handlers else ''
        step = f'''(func (export "control_step") (param $trigger i32) (param $time i64) (param $delta i64) (result i32)
          (if (i32.eq (local.get $trigger) (i32.const 0))
            (then (global.set $handle (call $resource (i32.const 0))) {start})
            (else (if (i32.ne (local.get $trigger) (i32.const 1)) (then unreachable))
              (call $fn_{self.handlers['on_frame']} (local.get $time) (local.get $delta)))) (i32.const 0))'''
        wat = '(module\n' + '\n'.join(imports + ['(global $handle (mut i32) (i32.const 0))', HELPERS] + globals_code + palettes + functions + [step]) + '\n)'
        manifest = {"schema": "lightvm.program/1", "abi": 1, "binding": "A", "retention": self.resource[1], "parameters": [p for _, p in self.parameters.values()]}
        return wat, manifest


def compile_source(source, filename="<string>", target=None):
    try:
        wat, manifest = Compiler(source).build()
        import wasmtime
        wasm = bytes(wasmtime.wat2wasm(wat))
        metadata = json.dumps(manifest, separators=(",", ":"), allow_nan=False).encode()
        section_name = b"lightvm.program"
        section = uleb(len(section_name)) + section_name + metadata
        wasm += b'\0' + uleb(len(section)) + section
        if target is not None:
            if len(wasm) > target.get("module_limit", 32768): raise CompileError(ast.Module(), "artifact exceeds target module limit")
            if target.get("control_abi") != 1: raise CompileError(ast.Module(), "target does not advertise control ABI 1")
        return Compilation(wasm, manifest, wat=wat)
    except (CompileError, SyntaxError) as error:
        n = error.node if isinstance(error, CompileError) else error
        message = error.message if isinstance(error, CompileError) else error.msg
        return Compilation(diagnostics=[Diagnostic(message, filename, getattr(n, "lineno", 1), getattr(n, "col_offset", getattr(n, "offset", 0)) + 1)])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("-o", "--output", required=True, type=Path)
    parser.add_argument("--target", type=Path, help="JSON from device discovery")
    parser.add_argument("--wat", action="store_true", help="also save generated WAT")
    args = parser.parse_args()
    result = compile_source(args.source.read_text(), str(args.source), json.loads(args.target.read_text()) if args.target else None)
    for d in result.diagnostics: print(f'{d.filename}:{d.line}:{d.column}: {d.message}')
    if not result.success: return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result.wasm)
    args.output.with_suffix('.json').write_text(json.dumps(result.manifest, indent=2) + '\n')
    if args.wat: args.output.with_suffix('.wat').write_text(result.wat)
    print(f'{args.output}: {len(result.wasm)} bytes; control ABI 1; no linear memory')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
