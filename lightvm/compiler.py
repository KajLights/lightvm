"""Compile restricted Python to controller-independent LightVM bytecode.

Source is parsed and never executed. No WASM assembler or device toolchain is used.
"""
import argparse
import ast
from dataclasses import dataclass, field
import json
from pathlib import Path
from .compiler_core import CompileError
from .portable import PortableCompiler
from .release import release_error

@dataclass
class Diagnostic:
    message: str
    filename: str
    line: int
    column: int

@dataclass
class Compilation:
    bytecode: bytes = b""
    manifest: dict = field(default_factory=dict)
    diagnostics: list = field(default_factory=list)
    advisories: list = field(default_factory=list)

    @property
    def success(self):
        return not self.diagnostics


def compile_source(source, filename="<string>", target=None, *, optimize=True):
    try:
        if len(source.encode()) > 65536:
            raise CompileError("source exceeds 64 KiB")
        bulk = optimize
        compiler = PortableCompiler(source, optimize=bulk)
        bytecode, manifest = compiler.compile()
        if target is not None:
            if manifest['profile'] == 'transition' and not target.get('transition_programs'):
                raise CompileError('target requires Python transition support')
            if manifest.get('inputs') and not target.get('live_inputs'):
                raise CompileError('target requires live input support')
            if any('control' in port for port in manifest.get('inputs', [])) and not target.get('light_controls'):
                raise CompileError('target requires output light controls')
            if len(bytecode) > target.get("module_limit", 32768):
                raise CompileError("artifact exceeds target module limit")
            error = release_error(target.get('version'), manifest['requires'])
            if error:
                raise CompileError(error)
        return Compilation(bytecode, manifest, advisories=performance_advice(compiler, filename))
    except (CompileError, SyntaxError, ValueError, OverflowError, RecursionError) as error:
        node = getattr(error, "node", None)
        return Compilation(diagnostics=[Diagnostic(str(error), filename,
            getattr(node, "lineno", None) or getattr(error, "lineno", None) or 1,
            (getattr(node, "col_offset", 0) + 1) if node else getattr(error, "offset", None) or 1)])


def performance_advice(compiler, filename):
    """Source advice is separate from errors and never grants runtime reuse.

    Only report facts about emitted callbacks, or conditional suggestions. The
    device proves contribution reuse from verified bytecode at admission.
    """
    notes, seen = [], set()
    for definition in compiler.sprite_definitions.values():
        fn = definition['render']
        if fn.name in seen:
            continue
        seen.add(fn.name)
        clocks = compiler.callback_clocks(fn)
        unused = [name for name in ('time', 'delta') if name not in clocks]
        if compiler.optimize and unused:
            notes.append(Diagnostic('Optimised: unused sprite ' + ' and '.join(unused) +
                ' calculations are omitted from each frame.', filename, fn.lineno, fn.col_offset + 1))
        for node in ast.walk(fn):
            if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.Call):
                continue
            kind = compiler.imports.get(getattr(node.value.func, 'id', ''))
            if kind not in ('Transform', 'ColorTransform'):
                continue
            if any(isinstance(part, ast.Attribute) and part.attr in ('time', 'delta') for part in ast.walk(node.value)):
                continue
            notes.append(Diagnostic('This sprite ' + ('geometry' if kind == 'Transform' else 'colour style') +
                ' is rebuilt each frame. If its values stay fixed for the sprite’s lifetime, assign it in the sprite init callback.',
                filename, node.lineno, node.col_offset + 1))
    for node, selected in compiler.map_reports:
        message = ('Optimised: this independent pixel loop uses a native map.' if selected else
                   'Native-map optimisation is disabled for this compilation; this loop uses the interpreter.' if not compiler.optimize else
                   'This loop uses the interpreter: native maps require one independent pixel store, no state/RNG effects, and a supported straight-line body.')
        notes.append(Diagnostic(message, filename, node.lineno, node.col_offset + 1))
    render = compiler.funcs.get('render')
    if render:
        dynamic = any(isinstance(node, (ast.For, ast.While, ast.If)) or
                      isinstance(node, ast.Attribute) and node.attr in ('time', 'delta')
                      for node in ast.walk(render))
        notes.append(Diagnostic('Reuse: ' + ('control flow or frame clocks require conservative execution; the device verifies eligibility from bytecode.' if dynamic else
            'the device checks this render for unchanged-input reuse; state, RNG and resource reads may still prevent reuse.'),
            filename, render.lineno, render.col_offset + 1))
        notes.append(Diagnostic(f'Resource estimate: {len(compiler.resources)} program resources, {len(compiler.regs)} register slots, and {len(compiler.code)} instructions. '
            'Pixel buffers and resource lengths depend on output size/settings; use device heap measurements for total RAM. This is not a frame-rate estimate.',
            filename, render.lineno, render.col_offset + 1))
    return notes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--target", type=Path)
    parser.add_argument("--no-optimize", action="store_true", help="retain ordinary pixel loops for comparison")
    args = parser.parse_args()
    result = compile_source(args.source.read_text(), str(args.source), json.loads(args.target.read_text()) if args.target else None, optimize=not args.no_optimize)
    if not result.success:
        for item in result.diagnostics:
            print(f"{item.filename}:{item.line}:{item.column}: {item.message}")
        raise SystemExit(1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result.bytecode)
    args.output.with_suffix(".json").write_text(json.dumps(result.manifest, indent=2) + "\n")

if __name__ == "__main__":
    main()
