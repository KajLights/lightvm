#!/usr/bin/env python3
"""Reject ESP32 images that silently resolve native telemetry to weak stubs."""
import subprocess
import sys


def check(symbols):
    required = {'platform_metric_supported', 'platform_metric_read', 'platform_cpu_hz'}
    strong = {parts[-1] for line in symbols.splitlines()
              if len(parts := line.split()) >= 3 and parts[-2] == 'T'}
    return sorted(required - strong)


if __name__ == '__main__':
    symbols = subprocess.check_output([sys.argv[1], '-g', sys.argv[2]], text=True)
    missing = check(symbols)
    if missing:
        raise SystemExit('ESP32 native telemetry readers missing/weak: ' + ', '.join(missing))
    print('ESP32 native telemetry readers linked (strong definitions)')
