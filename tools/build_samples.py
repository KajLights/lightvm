#!/usr/bin/env python3
"""Compile the sample WASM text files on the desktop, separately from firmware."""
import argparse
from pathlib import Path
import wasmtime

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "build" / "samples")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
for source in sorted((Path(__file__).resolve().parents[1] / "samples").glob("*.wat")):
    destination = args.output / (source.stem + ".wasm")
    destination.write_bytes(wasmtime.wat2wasm(source.read_text()))
    print(destination)
