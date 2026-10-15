#!/usr/bin/env python3
"""Gate Pico W's native RAM reservation and retain an honest capacity report."""
import json
import subprocess
import sys
from pathlib import Path
size, image = sys.argv[1:3]
heap = int(sys.argv[3]) if len(sys.argv) > 3 else 65536
if heap not in (65536, 114688):
    raise SystemExit("Unknown Pico W VM reservation")
rows = subprocess.check_output([size, image], text=True).splitlines()
_, data, bss, *_ = map(int, rows[1].split()[:4])
static = data + bss
budgets = {"static_bytes": static, "vm_heap_ceiling": heap, "native_stack": 16384,
           "pixel_driver_max": 3072, "operating_headroom": 16384}
remaining = 264 * 1024 - sum(budgets.values())
report = {**budgets, "remaining_bytes_after_reservations": remaining,
          "dynamic_costs": "lwIP pools are static; other native allocations must fit operating headroom"}
Path(image).with_name("ram-report.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps(report))
if remaining < 0:
    raise SystemExit("Pico W RAM reservations exceed SRAM; reduce the networking buffers before shipping")
