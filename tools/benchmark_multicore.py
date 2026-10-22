#!/usr/bin/env python3
"""Measure fixed multicore workloads; restore configuration and remove test programs."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import struct
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source
from client import Client, SerialTransport, ProcessTransport, FRAME, PLAY, STOP
from benchmark_controller import percentile95

PREFIX = "from lightvm import profile,init,render,rgb,hsv,PixelArray,Transform\nprofile('animation')\n"
SOURCES = {
    "map": PREFIX + """@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  pixels[i]=hsv((i/pixels.count)*(i/pixels.count)+0.37,0.6,0.8)
""",
    "scalar": PREFIX + """@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  value=i*0.001
  for j in range(12):
   value=(value+0.03125)*0.9375
  pixels[i]=rgb(int(value*200),i%255,87)
""",
    "draw": PREFIX + """@init
def setup(ctx,runtime):
 ctx.image=PixelArray(31)
 ctx.image.colorwheel(0,8)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ctx.image,transform=Transform(translate=-0.375,scale=3.25),overflow="wrap")
""",
    "solid": PREFIX + """from lightvm import ColorTransform
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(0,17,255))
 pixels.transform(ColorTransform(brightness=12/255))
""",
    "sprites": PREFIX + """from lightvm import Sprite,SpriteBatch,ColorTransform
IMAGE=PixelArray([64,64,64,255,255,255,255,255,64,64,64,255],format="rgba8",readonly=True)
def setup_star(s,runtime,position):
 s.position=position
def draw_star(s,runtime,pixels):
 pixels.draw(IMAGE)
 s.transform=Transform(translate=s.position,origin=1.0)
 s.color=ColorTransform(alpha=0.75)
Star=Sprite(width=3,init=setup_star,render=draw_star)
@init
def setup(ctx,runtime):
 ctx.stars=SpriteBatch(Star,capacity=12)
 for i in range(12):
  ctx.stars.spawn(position=(i*17)%runtime.pixel_count)
@render
def draw(ctx,runtime,pixels):
 ctx.stars.render(runtime,pixels)
""",
}
SOURCES['timed_sprites'] = (ROOT / 'tests/fixtures/performance/timed-sprites.py').read_text()
# (name, program, outputs, layers per composition, A/B transition)
CASES = (
    ("pure_map", "map", 1, 1, False),
    ("transformed_image", "draw", 1, 1, False),
    ("two_map_layers", "map", 1, 2, False),
    ("two_transformed_layers", "draw", 1, 2, False),
    ("four_scalar_layers", "scalar", 1, 4, False),
    ("two_outputs", "scalar", 2, 2, False),
    ("ab_transition", "scalar", 1, 2, True),
    ("mixed_sprite_layers", "sprites", 1, 2, False),
    ("timed_sprite_layers", "timed_sprites", 1, 2, False),
)


def measure(client, seconds, count, cases=CASES, *, warmup=7, samples=5, layer_samples=False):
    original = client.get_config()
    existing = {p["name"] for p in client.programs()}
    names = {kind: "mc_bench_" + kind for kind in SOURCES}
    if existing.intersection(names.values()):
        raise ValueError("remove or rename existing mc_bench_* programs before benchmarking")
    created, results, artifacts = [], [], {}
    try:
        for kind, source in SOURCES.items():
            artifact = compile_source(source, optimize=kind != "scalar")
            if not artifact.success:
                raise ValueError(artifact.diagnostics)
            artifacts[kind] = artifact.bytecode
            client.save_program(names[kind], artifact.bytecode)
            created.append(names[kind])
        # Reverse case order on alternate rounds; each round still starts with
        # fresh instances. Firmware AB/BA order is selected by the operator.
        trials=[(trial, case) for trial in range(samples) for case in (cases if trial%2==0 else tuple(reversed(cases)))]
        for trial, (name, kind, outputs, layers, ab) in trials:
            ids = ["bench" + str(i) for i in range(outputs)]
            # Applying the same output configuration preserves live scenes.
            # Give each workload a fresh runtime, particularly after A/B tests.
            client.configure({"schema": "lightvm.config", "outputs": []})
            client.configure({"schema": "lightvm.config", "outputs": [
                {"id": output, "type": "ws2812b", "config": {
                    "pin": "GP" + str(16 + i), "count": count, "brightness_limit": 32}}
                for i, output in enumerate(ids)]})
            slots = ("A", "B") if ab else ("A",)
            for output in ids:
                for slot in slots:
                    client.configure_composition([
                        {"id": "layer" + str(i), "program": names["solid" if kind in ("sprites", "timed_sprites") and i == 0 else kind]}
                        for i in range(layers)], output=output, slot=slot)
                if ab:
                    client.set_progress(37.5, output=output)
                client.require_ok(client.command(STOP, output=output))
                for slot in slots:
                    for i in range(layers):
                        client.reset_program(output=output, slot=slot, layer="layer" + str(i))
            crcs, render_times, per_layer_times = [], [], {}
            for _ in range(3):
                for output in ids:
                    client.require_ok(client.command(FRAME, struct.pack("<I", 16), output=output))
                    crcs.append(zlib.crc32(client.pixel_snapshot))
                    for slot in slots:
                        for layer in client.composition(output=output, slot=slot)["layers"]:
                            render_times.append(layer['render_us'])
                            per_layer_times.setdefault(f"{output}/{slot}/{layer['id']}", []).append(layer['render_us'])
            def contributions():
                return {f"{output}/{slot}/{layer['id']}": layer["frames"]
                        for output in ids for slot in slots
                        for layer in client.composition(output=output, slot=slot)["layers"]}
            for output in ids:
                client.require_ok(client.command(PLAY, output=output))
            time.sleep(warmup)
            before = client.describe()
            layers_before = contributions()
            start = time.monotonic()
            latencies, contribution_samples = [], []
            while time.monotonic() - start < seconds:
                time.sleep(0.05)
                for output in ids:
                    request_started=time.monotonic()
                    state = client.status_fields(output=output)
                    latencies.append((time.monotonic()-request_started)*1000)
                    if state.get("animation_error") != "-":
                        raise ValueError(state)
                if layer_samples:
                    contribution_samples.append({'elapsed_s':time.monotonic()-start,
                        'outputs':{f'{output}/{slot}':client.composition(output=output,slot=slot)
                                   for output in ids for slot in slots}})
            after = client.describe()
            layers_after = contributions()
            elapsed = time.monotonic() - start
            for i, output in enumerate(ids):
                _, frame = client.composition_snapshot(output=output, slot="A")
                if kind != 'timed_sprites' and zlib.crc32(frame) != crcs[i]:
                    raise ValueError(f"{name}: autonomous frame CRC {zlib.crc32(frame):08x} differs from manual reference {crcs[i]:08x}")
            a = {o["id"]: o["frames"] for o in before["outputs"]}
            b = {o["id"]: o["frames"] for o in after["outputs"]}
            results.append({
                "name": name, "pixels_per_output": count, "outputs": outputs,
                "trial": trial+1, "warmup_seconds": warmup,
                "layers_per_slot": layers, "slots": len(slots),
                "bytecode_sha256": hashlib.sha256(artifacts[kind]).hexdigest(),
                "frame_crc32": crcs, "seconds": round(elapsed, 3),
                "manual_layer_render_us_median": statistics.median(render_times),
                "manual_render_us_by_layer": per_layer_times,
                "programs": {key: {'source_sha256': hashlib.sha256(SOURCES[key].encode()).hexdigest(),
                                   'bytecode_sha256': hashlib.sha256(artifacts[key]).hexdigest(),
                                   'settings': {}, 'inputs': {}}
                             for key in ({kind, 'solid'} if kind in ('sprites', 'timed_sprites') else {kind})},
                "autonomous_crc_comparison": kind != 'timed_sprites',
                "status_latency_ms_samples": latencies,
                "contribution_samples": contribution_samples,
                "diagnostic_layer_sampling": layer_samples,
                "fps_per_output": {i: round((b[i] - a[i]) / elapsed, 2) for i in ids},
                "layer_fps": {i: round((layers_after[i] - layers_before[i]) / elapsed, 2)
                              for i in layers_before},
                "status_latency_ms_median": round(statistics.median(latencies), 3),
                "status_latency_ms_p95": round(percentile95(latencies), 3),
                "worker_stack_peak_bytes": after.get("worker_stack_peak_bytes"),
                "main_stack_peak_bytes": after.get("main_stack_peak_bytes"),
                "runtime_before": before, "runtime_after": after,
            })
            print(json.dumps(results[-1]), flush=True)
        # Exercise XIP lockout, replacement and resets while the other VMs run.
        for i in range(12):
            client.save_program(names["map"], artifacts["map"])
            client.reset_program(output="bench0", slot="A", layer="layer0")
            client.describe()
        description = client.describe()
        return {
            "profile": {k: description.get(k) for k in (
                "board", "version", "render_workers", "pico2_dcp_enabled", "max_refresh_hz",
                "bytecode_cache_bytes", "vm_heap_limit", "render_slice_us",
                "worker_stack_peak_bytes", "main_stack_peak_bytes", "render_offloads", "render_worker_us",
                "boot_id", "build_compiler", "cpu_clock_hz", "render_epoch_target_us")},
            "cases": results, "flash_replacement_and_reset_cycles": 12,
        }
    finally:
        client.configure(original)
        for name in created:
            client.delete_program(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port")
    transport.add_argument("--process", type=Path)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--warmup", type=float, default=7)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--layer-samples", action="store_true", help="collect contribution age/provenance; keep separate from throughput timing runs")
    parser.add_argument("--network-regime", default="unspecified", help="record broker traffic, clients and Wi-Fi conditions for this run")
    parser.add_argument("--firmware", type=Path, help="image that the operator verified is flashed; record its SHA256")
    parser.add_argument("--pixels", type=int, default=200)
    parser.add_argument("--timeout", type=float, default=30,
                        help="command timeout; RP2040 scene preparation can exceed five seconds while the old scene keeps playing")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", action="append", choices=[case[0] for case in CASES],
                        help="run selected workloads; repeat to select more than one")
    args = parser.parse_args()
    if args.seconds <= 0 or args.timeout <= 0 or args.pixels < 101 or args.warmup < 0 or args.samples < 1:
        parser.error("seconds and timeout must be positive; pixels must be at least 101 for the wrapped image")
    if args.port:
        deadline = time.monotonic() + 10
        while not Path(args.port).exists() and time.monotonic() < deadline:
            time.sleep(0.1)
    connection = SerialTransport(args.port) if args.port else ProcessTransport(args.process)
    if args.port:
        # Let USB reconnect, and drain an earlier abandoned reply before framing.
        connection.serial.timeout = 0.1
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline and connection.serial.read(4096):
            pass
    client = Client(connection, timeout=args.timeout)
    try:
        cases = [case for case in CASES if not args.case or case[0] in args.case]
        result = measure(client, args.seconds, args.pixels, cases,warmup=args.warmup,samples=args.samples,layer_samples=args.layer_samples)
        result['network_regime']=args.network_regime
        executable=args.process or args.firmware
        result['executable_sha256']=hashlib.sha256(executable.read_bytes()).hexdigest() if executable else None
        result['executable_identity_source']='host process' if args.process else 'operator verified flashed image' if args.firmware else 'unknown'
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    finally:
        client.close()


if __name__ == "__main__":
    main()
