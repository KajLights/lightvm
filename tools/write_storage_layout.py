#!/usr/bin/env python3
"""Export the linked Pico storage boundary for firmware installers."""
import json
from pathlib import Path
import subprocess
import sys

nm, image, output, storage_bytes, board = sys.argv[1:]
symbols = {}
for line in subprocess.check_output([nm, '--defined-only', image], text=True).splitlines():
    fields = line.split()
    if len(fields) == 3 and fields[2] in ('__lightvm_storage_start', '__lightvm_boot_storage_start'):
        symbols[fields[2]] = int(fields[0], 16)
start = symbols['__lightvm_storage_start']
size = int(storage_bytes)
Path(output).write_text(json.dumps({
    'board': board,
    'storage_start': start, 'storage_bytes': size,
    'flash_bytes': start + size - 0x10000000,
    'firmware_end': min(symbols.values()),
}, indent=2) + '\n')
