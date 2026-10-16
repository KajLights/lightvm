#!/usr/bin/env python3
"""Read JSON results from output_probe.uf2 after explicitly flashing it."""
import argparse
import json
from pathlib import Path
import time

from measure import SerialPort


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    port = SerialPort(args.port)
    observations = []
    buffered = b""
    deadline = time.monotonic() + 45
    try:
        # macOS may deliver (then flush) the initial ready line while setting
        # raw mode. Starting proactively also works when that line was lost.
        port.write(b"r")
        while time.monotonic() < deadline:
            buffered += port.read(4096, 0.1)
            while b"\n" in buffered:
                line, buffered = buffered.split(b"\n", 1)
                if not line.strip():
                    continue
                observation = json.loads(line)
                observations.append(observation)
                print(json.dumps(observation), flush=True)
                args.output.write_text(json.dumps(observations, indent=2) + "\n")
                if observation.get("ready"):
                    port.write(b"r")
                if observation.get("done"):
                    assert observation["passed"], "hardware probe failed"
                    return
        raise TimeoutError("hardware probe did not finish in 45 seconds")
    finally:
        port.close()


if __name__ == "__main__":
    main()
