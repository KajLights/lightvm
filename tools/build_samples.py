#!/usr/bin/env python3
"""Compile the sample WASM text files on the desktop, separately from firmware."""
import argparse
from pathlib import Path
import wasmtime
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "build" / "samples")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for source in sorted((Path(__file__).resolve().parents[1] / "samples").glob("*.wat")):
    destination = args.output / (source.stem + ".wasm")
    destination.write_bytes(wasmtime.wat2wasm(source.read_text()))
    print(destination)
for source in sorted((ROOT / "samples" / "python").glob("*.py")):
    result = compile_source(source.read_text(), str(source))
    if not result.success:
        raise SystemExit(str(result.diagnostics))
    destination = args.output / (source.stem + "_control.wasm")
    destination.write_bytes(result.wasm)
    import json
    destination.with_suffix(".json").write_text(json.dumps(result.manifest, indent=2) + "\n")
    print(destination)
