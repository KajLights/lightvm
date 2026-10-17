"""Compile restricted Python to controller-independent LightVM bytecode.

Source is parsed and never executed. No WASM assembler or device toolchain is used.
"""
import argparse
import ast
from dataclasses import dataclass, field
import json
from pathlib import Path
from .bytecode import CompileError
from .portable import PortableCompiler

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

    @property
    def success(self):
        return not self.diagnostics


def compile_source(source, filename="<string>", target=None, *, optimize=True):
    try:
        if len(source.encode()) > 65536:
            raise CompileError("source exceeds 64 KiB")
        # Older targets execute the same source through the ordinary loop path.
        bulk = optimize and (target is None or target.get('native_drawing_api', 0) >= 4)
        bytecode, manifest = PortableCompiler(source, optimize=bulk).compile()
        if target is not None:
            if len(bytecode) > target.get("module_limit", 32768):
                raise CompileError("artifact exceeds target module limit")
            abi = manifest['abi']
            if abi not in target.get('program_abis', [target.get('control_abi')]) or manifest['schema'] not in target.get('program_schemas', []):
                raise CompileError(f'target does not advertise program ABI {abi} and {manifest["schema"]}')
            if f'LBC1/{abi}' not in target.get('bytecode_formats', [target.get('bytecode_format')]):
                raise CompileError(f'target does not advertise LBC1/{abi} bytecode')
            if abi == 4 and manifest['profile'] not in target.get('program_profiles', []):
                raise CompileError('target does not advertise ' + manifest['profile'])
            drawing_api = int.from_bytes(bytecode[80:84], 'little')
            if abi == 4 and drawing_api > target.get('native_drawing_api', 0):
                raise CompileError(f'target does not advertise native drawing API {drawing_api}')
        return Compilation(bytecode, manifest)
    except (CompileError, SyntaxError, ValueError, OverflowError, RecursionError) as error:
        node = getattr(error, "node", None)
        return Compilation(diagnostics=[Diagnostic(str(error), filename,
            getattr(node, "lineno", None) or getattr(error, "lineno", None) or 1,
            (getattr(node, "col_offset", 0) + 1) if node else getattr(error, "offset", None) or 1)])


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
