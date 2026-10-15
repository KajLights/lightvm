#!/usr/bin/env python3
"""Build the same C kernels used by the native benchmark as portable WASM."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--zig', required=True)
parser.add_argument('--output', type=Path, default=ROOT/'build/runtime-bench/kernels.wasm')
args = parser.parse_args()
if subprocess.check_output([args.zig, 'version'], text=True).strip() != '0.13.0':
    raise SystemExit('Expected Zig 0.13.0')
args.output.parent.mkdir(parents=True, exist_ok=True)
source = ROOT/'experiments/runtime/kernels.c'
cmd = [args.zig, 'cc', '-target', 'wasm32-freestanding', '-O2', '-ffp-contract=off',
       '-fno-builtin', '-nostdlib', str(source), '-Wl,--no-entry',
       '-Wl,--initial-memory=0x10000', '-Wl,--max-memory=0x10000',
       '-Wl,-z,stack-size=0x1000', '-Wl,--strip-all', '-o', str(args.output)]
subprocess.run(cmd, check=True, env=os.environ | {'ZIG_GLOBAL_CACHE_DIR':str(args.output.parent/'zig-cache')})
record = {'bytes':args.output.stat().st_size, 'sha256':hashlib.sha256(args.output.read_bytes()).hexdigest(),
          'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(), 'command':cmd}
args.output.with_suffix('.json').write_text(json.dumps(record, indent=2)+'\n')
print(json.dumps(record))
