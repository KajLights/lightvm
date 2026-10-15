#!/usr/bin/env python3
"""Compile the animation examples into controller-independent bytecode."""
import argparse
import json
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=ROOT / "build/examples")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for source in sorted((ROOT / "examples/animations").glob("*.py")):
    result = compile_source(source.read_text(), str(source))
    if not result.success:
        raise SystemExit(str(result.diagnostics))
    destination = args.output / (source.stem + ".lbc")
    destination.write_bytes(result.bytecode)
    destination.with_suffix(".json").write_text(json.dumps(result.manifest, indent=2) + "\n")
    print(destination)
