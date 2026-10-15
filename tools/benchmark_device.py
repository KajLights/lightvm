#!/usr/bin/env python3
"""Compare the same 100-pixel rainbow bytecode on host, Pico and XIAO boards."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import time

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lightvm.compiler import compile_source

from client import FRAME, PLAY, RESET, STATUS, STOP, Client, ProcessTransport, ProtocolError, SerialTransport

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PINS = {
    "simulator": "GP16",
    "test_175": "GP16",
    "pico": "GP16",
    "pico_w": "GP16",
    "xiao_esp32c3": "D1",
    "xiao_esp32s3": "D0",
    "xiao_esp32c6": "D0",
}


def percentile95(values):
    ordered = sorted(values)
    return ordered[(95 * len(ordered) + 99) // 100 - 1]


def measure(client, board, pin, pixels, samples, seconds):
    initial = client.status_fields()
    if initial.get("board") != board:
        raise ProtocolError(f"expected board {board}, got {initial.get('board')}")
    original_config = client.get_config()
    config = {
        "schema": "lightvm.config/1",
        "devices": [{"id": "benchmark", "type": "ws2812b", "config": {
            "pin": pin, "count": pixels, "order": "GRB", "brightness_limit": 32}}],
        "targets": {"A": {"device": "benchmark"}},
    }
    source = (ROOT / "samples/python/rainbow.py").read_text()
    artifact = compile_source(source)
    if not artifact.success:
        raise ValueError(artifact.diagnostics)
    module = artifact.bytecode
    try:
        client.configure(config)
        client.upload(module)
        render_us, show_us = [], []
        for index in range(samples + 5):
            reply = client.require_ok(client.command(FRAME, (16).to_bytes(4, "little")))
            if reply.value != pixels or len(client.pixel_snapshot) != pixels * 3:
                raise ProtocolError("FRAME returned an incomplete pixel snapshot")
            fields = client.status_fields()
            if index >= 5:
                render_us.append(int(fields["render_us"]))
                show_us.append(int(fields["show_us"]))

        client.require_ok(client.command(PLAY))
        start_before = time.monotonic()
        start = client.status_fields()
        start_after = time.monotonic()
        time.sleep(seconds)
        end_before = time.monotonic()
        end = client.status_fields()
        end_after = time.monotonic()
        if end["mode"] != "bytecode" or end["animation_error"] != "-":
            raise ProtocolError(f"playback stopped: {end['animation_error']}")
        elapsed = (end_before + end_after - start_before - start_after) / 2
        frames = int(end["frames"]) - int(start["frames"])
        if frames <= 0:
            raise ProtocolError("no autonomous frames were produced")
        return {
            "timestamp_utc": datetime.now(timezone.utc).isoformat(),
            "board": board,
            "platform": end["platform"],
            "output": end["output"],
            "pin": pin,
            "pixels": pixels,
            "brightness_limit": 32,
            "workload": "samples/python/rainbow.py",
            "bytecode_sha256": hashlib.sha256(module).hexdigest(),
            "frame_interval_us": int(end["frame_interval_us"]),
            "samples": samples,
            "render_us_median": statistics.median(render_us),
            "render_us_p95": percentile95(render_us),
            "show_us_median": statistics.median(show_us),
            "show_us_p95": percentile95(show_us),
            "playback_seconds": round(elapsed, 3),
            "playback_frames": frames,
            "playback_fps": round(frames / elapsed, 2),
            "heap_peak_bytes": int(end["heap_peak"]),
        }
    finally:
        try:
            client.require_ok(client.command(STOP))
            client.require_ok(client.command(RESET))
        finally:
            client.configure(original_config)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port", help="USB serial device")
    transport.add_argument("--process", type=Path, help="host receiver executable")
    parser.add_argument("--board", required=True, choices=DEFAULT_PINS)
    parser.add_argument("--pin", help="override the board's example output pin")
    parser.add_argument("--pixels", type=int, default=100)
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--seconds", type=float, default=3.0)
    parser.add_argument("--output", type=Path, help="write JSON result to this file")
    args = parser.parse_args()
    if args.pixels < 1 or args.samples < 1 or args.seconds <= 0:
        parser.error("pixels, samples and seconds must be positive")
    connection = SerialTransport(args.port, no_reset=args.board.startswith("xiao_esp32")) if args.port else ProcessTransport(args.process)
    client = Client(connection)
    try:
        result = measure(client, args.board, args.pin or DEFAULT_PINS[args.board],
                         args.pixels, args.samples, args.seconds)
    finally:
        client.close()
    print(f"{result['board']}: {result['pixels']} pixels, rainbow bytecode, "
          f"render median/p95 {result['render_us_median']}/{result['render_us_p95']} us, "
          f"output median/p95 {result['show_us_median']}/{result['show_us_p95']} us, "
          f"autonomous {result['playback_fps']} fps, peak heap {result['heap_peak_bytes']} B")
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()
