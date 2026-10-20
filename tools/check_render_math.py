#!/usr/bin/env python3
"""Record deterministic transform/math frames for host and Pico build comparisons."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from client import Client, SerialTransport, ProcessTransport, FRAME, STOP
from lightvm.compiler import compile_source

SOURCE = """from lightvm import profile,init,render,setting,PixelArray,Transform,ColorTransform,rgb,pico_colorwheel,hue_degrees
profile("animation")
@init
def setup(ctx,runtime):
 ctx.shift=setting(0.0)
 ctx.scale=setting(1.0)
 ctx.image=PixelArray(31)
 ctx.image.colorwheel(0,8)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ctx.image,transform=Transform(translate=ctx.shift,scale=ctx.scale,origin=0.25),color=ColorTransform(alpha=0.75),overflow="wrap")
 for i in range(4):
  pixels[i]=pico_colorwheel(i*89)
 pixels[4]=rgb(int(hue_degrees(rgb(255,127,0))),0,0)
"""


def measure(client):
    name = "mc_bench_math"
    if any(p["name"] == name for p in client.programs()):
        raise ValueError("mc_bench_math already exists")
    artifact = compile_source(SOURCE)
    if not artifact.success:
        raise ValueError(artifact.diagnostics)
    original, created = client.get_config(), False
    try:
        client.save_program(name, artifact.bytecode)
        created = True
        client.configure({"schema": "lightvm.config", "outputs": [{
            "id": "math", "type": "ws2812b", "config": {"pin": "GP16", "count": 200}}]})
        client.configure_composition([{"id": "image", "program": name}], output="math")
        client.require_ok(client.command(STOP, output="math"))
        results = []
        for shift in (-1e9, -6700.5, -0.375, 0.0, 0.5, 66.5, 6766.5, 1e9):
            for scale in (-3.25, -0.25, 0.5, 3.25):
                client.reset_program(output="math", layer="image",
                                     settings={"shift": shift, "scale": scale})
                client.require_ok(client.command(FRAME, struct.pack("<I", 16), output="math"))
                results.append({"shift": shift, "scale": scale,
                                "sha256": hashlib.sha256(client.pixel_snapshot).hexdigest()})
        return {"bytecode_sha256": hashlib.sha256(artifact.bytecode).hexdigest(),
                "cases": results}
    finally:
        client.configure(original)
        if created:
            client.delete_program(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port")
    transport.add_argument("--process", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    connection = SerialTransport(args.port) if args.port else ProcessTransport(args.process)
    if args.port:
        connection.serial.timeout = 0.1
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and connection.serial.read(4096):
            pass
    client = Client(connection)
    try:
        result = measure(client)
        args.output.write_text(json.dumps(result, indent=2) + "\n")
        print(f"Recorded {len(result['cases'])} transform/math frames in {args.output}")
    finally:
        client.close()


if __name__ == "__main__":
    main()
