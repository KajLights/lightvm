#!/usr/bin/env python3
"""Read compiler-evaluated constants; this object is never linked into firmware."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('objcopy')
parser.add_argument('object')
parser.add_argument('output', type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory() as directory:
    raw = Path(directory) / 'limits.bin'
    subprocess.run([args.objcopy, '--dump-section', '.kajlights_limits=' + str(raw),
                    args.object, str(Path(directory) / 'copy.o')], check=True)
    values = struct.unpack('<7I', raw.read_bytes())
result = dict(zip(('config_bytes', 'module_bytes', 'pixel_capacity', 'storage_bytes',
                   'vm_heap_ceiling_bytes', 'vm_heap_automatic', 'native_headroom_bytes'), values))
args.output.write_text(json.dumps(result, indent=2) + '\n')
