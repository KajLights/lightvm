#!/usr/bin/env python3
"""Bound HA callback frames that remain live during Pico preset activation."""
import json
from pathlib import Path
import sys


def stack_report(text: str) -> dict[str, int]:
    # These are individual frame budgets, not a full call-graph stack proof.
    # A live JSON parser previously made message() 6328 bytes on RP2040 and
    # pushed manifest loading beyond the 16 KiB main-stack reservation.
    limits = {"message": 1024, "ha_tick": 3072, "light_command": 6144}
    frames = {}
    for line in text.splitlines():
        location, size, kind = line.split("\t")
        name = location.rsplit(":", 1)[-1].split(".", 1)[0]
        if name in limits:
            if kind != "static":
                raise ValueError(f"HA {name} has non-static stack usage")
            frames[name] = max(frames.get(name, 0), int(size))
    for name, limit in limits.items():
        if name not in frames:
            raise ValueError(f"missing HA stack usage for {name}")
        if frames[name] > limit:
            raise ValueError(f"HA {name} stack frame {frames[name]} exceeds {limit} bytes")
    return frames


if __name__ == "__main__":
    source, output = map(Path, sys.argv[1:])
    report = stack_report(source.read_text())
    output.write_text(json.dumps(report, indent=2) + "\n")
    print("Pico HA stack frames: " + json.dumps(report))
