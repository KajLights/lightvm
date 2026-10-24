#!/usr/bin/env python3
"""Measure frozen animation packages across boards and firmware profiles.

Uses the packages/manifest from the recorded Pico numeric campaign unchanged.
Restores active configuration and removes temporary programs; never saves config.
Reported layer completions distinguish useful animation work from presentations.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from client import Client, ProcessTransport, SerialTransport
from benchmark_controller import DEFAULT_PINS
from benchmark_saved_preset import sample

EMPTY = {"schema": "lightvm.config", "outputs": []}
STABLE_STARS = {"capacity": 20, "duration": 1000.0, "probability": 10000.0,
                "random_colors": False}
CASES = {name: [name] for name in ("map", "scalar", "draw", "sprites",
                                 "control-color", "control-rainbow", "control-stars", "control-sweep")}
CASES.update({"mixed": ["map", "scalar"], "stable-stars": ["control-stars"],
              "stars-background": ["control-color", "control-stars"],
              "saved-stars": ["saved-color", "saved-stars"], "saved-rainbow": ["saved-rainbow"]})
SAVED_STARS = {"capacity": 20, "duration": 2.0, "probability": 5.0,
               "color": 16777215, "random_colors": False}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(args):
    manifest = json.loads((args.packages / "manifest.json").read_text())
    selected = args.case or [name for name in CASES if not name.startswith('saved-')]
    used = sorted({name for case in selected for name in CASES[case]})
    packages = {}
    for name in used:
        data = (args.packages / (name + ".lbc")).read_bytes()
        if sha(data) != manifest[name]["bytecode_sha256"]:
            raise ValueError("package hash mismatch: " + name)
        packages[name] = data
    transport = (SerialTransport(args.port, no_reset=args.board.startswith("xiao_esp32"))
                 if args.port else ProcessTransport(args.process))
    client = Client(transport, timeout=15)
    result = {"label": args.label, "firmware_sha256": sha(args.firmware.read_bytes()),
              "board": args.board, "pin": args.pin or DEFAULT_PINS[args.board],
              "pixels": args.pixels, "warmup_seconds": args.warmup,
              "seconds_requested": args.seconds, "network_regime": args.network_regime,
              "packages": {name: manifest[name] for name in used}, "samples": []}
    created = []

    def checkpoint():
        args.result.write_text(json.dumps(result, indent=2) + "\n")

    try:
        result["description_before"] = client.describe()
        if result["description_before"]["board"] != args.board:
            raise ValueError("unexpected board")
        original = client.get_config()
        existing = {p["name"] for p in client.programs()}
        if existing.intersection("ap_" + name for name in used):
            raise ValueError("temporary benchmark names already exist")
        for name, data in packages.items():
            client.save_program("ap_" + name, data)
            created.append("ap_" + name)
            if client.get_program("ap_" + name) != data:
                raise ValueError("package readback mismatch: " + name)
        for trial in range(args.samples):
            for case in selected if trial % 2 == 0 else reversed(selected):
                client.configure(EMPTY)
                client.configure({"schema": "lightvm.config", "outputs": [{
                    "id": "bench", "type": "ws2812b", "config": {
                        "pin": result["pin"], "count": args.pixels,
                        "order": "GRB", "brightness_limit": 32}}]})
                stable = case in ("stable-stars", "stars-background")
                layers = [
                    {"id": "layer" + str(i), "program": "ap_" + name,
                     **({"settings": STABLE_STARS} if stable and name == "control-stars" else {})}
                    for i, name in enumerate(CASES[case])]
                if case == 'saved-stars':
                    layers[0]['inputs'] = {'color': {'control': 'color'}, 'brightness': {'value': 12 / 255}}
                    layers[1].update(settings=SAVED_STARS, inputs={'brightness': {'value': 1.0}})
                client.configure_composition(layers, output="bench")
                warmup_started = time.monotonic()
                warmup = max(7, args.warmup) if case == 'saved-stars' else max(2, args.warmup) if stable else args.warmup
                time.sleep(warmup)
                if stable:
                    while client.composition(output="bench")["layers"][-1]["frames"] < 25:
                        if time.monotonic() - warmup_started > 60:
                            raise ValueError("stable Stars population warmup incomplete")
                        time.sleep(.25)
                warmup_elapsed = time.monotonic() - warmup_started
                row = sample(client, "bench", max(30, args.seconds) if case == 'saved-stars' else args.seconds,
                             poll_hz=args.poll_hz)
                if stable and row["transition_before"]["A"]["layers"][-1]["frames"] < 25:
                    raise ValueError("stable Stars population warmup incomplete")
                row.update(case=case, trial=trial + 1, warmup_elapsed_seconds=warmup_elapsed,
                           settings=STABLE_STARS if stable else SAVED_STARS if case == 'saved-stars' else {})
                result["samples"].append(row)
                checkpoint()
                print(json.dumps({"label": args.label, "trial": trial + 1, "case": case,
                                  "render_us": {k: v["mean_render_us"] for k, v in row["layers"].items()},
                                  "updates_hz": {k: v["updates_per_second"] for k, v in row["layers"].items()},
                                  "usb_p95_ms": row["status_latency_ms"]["p95"]}), flush=True)
        result["description_after"] = client.describe()
        result["measurement_complete"] = True
        checkpoint()
    finally:
        try:
            if "original" in locals():
                client.configure(EMPTY)
                client.configure(original)
            for name in created:
                client.delete_program(name)
            result["restoration_complete"] = True
            checkpoint()
        finally:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument("--port")
    transport.add_argument("--process", type=Path)
    parser.add_argument("--board", required=True, choices=DEFAULT_PINS)
    parser.add_argument("--pin")
    parser.add_argument("--packages", required=True, type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--label", required=True)
    parser.add_argument("--result", required=True, type=Path)
    parser.add_argument("--network-regime", required=True)
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--pixels", type=int, default=200)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--seconds", type=float, default=3)
    parser.add_argument("--warmup", type=float, default=1)
    parser.add_argument("--poll-hz", type=float, default=20)
    args = parser.parse_args()
    if not 1 <= args.pixels <= 1024 or args.samples < 1 or args.seconds <= 0 or args.warmup < 0 or args.poll_hz < 0:
        parser.error("invalid pixel count, sample count, duration or poll rate")
    run(args)


if __name__ == "__main__":
    main()
