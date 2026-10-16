#!/usr/bin/env python3
"""Capture or measure the attached Pico, without changing persisted settings.

Uses the repository protocol client and a small POSIX transport, so pyserial is
not required. Snapshots/backups belong under ignored build/, never in results/.
"""
import argparse
import copy
from datetime import datetime, timezone
import fcntl
import json
import os
from pathlib import Path
import select
import statistics
import struct
import sys
import termios
import time
import tty

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from client import Client


class SerialPort:
    def __init__(self, path):
        # picotool returns before USB CDC has necessarily re-enumerated.
        deadline = time.monotonic() + 15
        while True:
            try:
                self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
                break
            except FileNotFoundError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)
        self.old = termios.tcgetattr(self.fd)
        try:
            tty.setraw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[4] = attrs[5] = termios.B115200
            attrs[2] |= termios.CLOCAL | termios.CREAD
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            fcntl.ioctl(self.fd, termios.TIOCMBIS, struct.pack("I", termios.TIOCM_DTR))
        except BaseException:
            self.close()
            raise

    def write(self, data):
        while data:
            if not select.select([], [self.fd], [], 3)[1]:
                raise TimeoutError("USB write timed out")
            data = data[os.write(self.fd, data):]

    def read(self, size, timeout):
        return os.read(self.fd, size) if select.select([self.fd], [], [], timeout)[0] else b""

    def close(self):
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.old)
        finally:
            os.close(self.fd)


def brief(state):
    return {k: v for k, v in state.items() if k not in ("manifest", "values")}


def restore(client, before):
    assert client.describe()["device_id"] == before["description"]["device_id"]
    client.configure(copy.deepcopy(before["config"]), save=False)
    for device in before["description"]["devices"]:
        state = before["states"][device["id"]]
        client.activate(device["active_procedure"], device=device["id"],
                        settings=state["settings_values"])
        if state["paused"]:
            client.pause_program(device=device["id"])
    assert client.get_config(saved=True) == before["saved_config"]
    assert client.procedures() == before["library"]


def measure(client, seconds, selected=None):
    first = client.describe()
    all_ids = [d["id"] for d in first["devices"]]
    ids = selected or all_ids
    states = {d: brief(client.program_parameters(device=d)) for d in ids}
    memory_before = client.status_fields()
    samples = []
    origin = time.monotonic()
    for n in range(int(seconds) + 1):
        time.sleep(max(0, origin + n - time.monotonic()))
        start = time.monotonic()
        desc = client.describe()
        end = time.monotonic()
        assert desc["boot_id"] == first["boot_id"], "unexpected reboot"
        assert [d["id"] for d in desc["devices"]] == all_ids, "device list changed"
        assert all(d["playing"] and not d["animation_error"] for d in desc["devices"] if d["id"] in ids), desc["devices"]
        samples.append({"elapsed_s": (start + end) / 2 - origin,
                        "query_ms": (end - start) * 1000, "devices": desc["devices"]})
        if n and n % 10 == 0:
            print(json.dumps({"elapsed_s": n, "frames": {d["id"]: d["frames"] for d in desc["devices"]}}), flush=True)
    elapsed = samples[-1]["elapsed_s"] - samples[0]["elapsed_s"]
    memory_after = client.status_fields()
    after = {d: brief(client.program_parameters(device=d)) for d in ids}
    assert memory_before["heap"] == memory_after["heap"], "VM heap changed"
    summary = {"duration_s": elapsed, "devices": {}, "vm_heap_bytes": int(memory_after["heap"]),
               "query_ms_median": statistics.median(s["query_ms"] for s in samples),
               "query_ms_max": max(s["query_ms"] for s in samples), "same_boot": True}
    for device in ids:
        obs = [next(d for d in s["devices"] if d["id"] == device) for s in samples]
        assert states[device]["generation"] == after[device]["generation"]
        assert states[device]["settings_values"] == after[device]["settings_values"]
        assert not after[device]["faulted"]
        summary["devices"][device] = {
            "procedure": obs[0]["active_procedure"], "pixels": obs[0]["pixels"],
            "frames": obs[-1]["frames"] - obs[0]["frames"],
            "fps": (obs[-1]["frames"] - obs[0]["frames"]) / elapsed,
            "render_us_median": statistics.median(o["render_us"] for o in obs),
            "show_us_median": statistics.median(o["show_us"] for o in obs)}
    return {"timestamp_utc": datetime.now(timezone.utc).isoformat(), "summary": summary,
            "samples": samples, "states_before": states, "states_after": after}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("capture", "measure", "suite", "restore"))
    parser.add_argument("--port", required=True)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    if args.seconds < 1:
        parser.error("--seconds must be positive")
    c = Client(SerialPort(args.port), timeout=20)
    try:
        if args.action == "capture":
            d = c.describe()
            result = {"description": d, "config": c.get_config(), "saved_config": c.get_config(saved=True),
                      "library": c.procedures(), "states": {x["id"]: c.program_parameters(device=x["id"]) for x in d["devices"]},
                      "status": c.status_fields()}
            args.before.parent.mkdir(parents=True, exist_ok=True)
            args.before.write_text(json.dumps(result, indent=2) + "\n")
            for entry in result["library"]:
                data = c.get_procedure(entry["name"])
                (args.before.parent / (entry["name"] + ".lbc")).write_bytes(data)
            print(json.dumps({"device_id": d["device_id"], "devices": d["devices"]}, indent=2))
        else:
            before = json.loads(args.before.read_text())
            assert c.describe()["device_id"] == before["description"]["device_id"]
            if args.action == "restore":
                restore(c, before)
                result = {"description": c.describe(), "states": {d["id"]: brief(c.program_parameters(device=d["id"])) for d in before["description"]["devices"]}}
            elif args.action == "measure":
                assert c.get_config() == before["config"], "restore active setup before measuring"
                result = measure(c, args.seconds)
            else:
                assert c.get_config() == before["config"], "restore active setup before measuring"
                ids = [d["id"] for d in before["description"]["devices"]]
                rainbows = [d["id"] for d in before["description"]["devices"] if d["active_procedure"] == "rainbow"]
                assert len(rainbows) == 2 and len(ids) == 4, "suite expects two Rainbows and two Twinkles"
                paused = []
                result = {}
                try:
                    result["four"] = measure(c, args.seconds)
                    for device in ids:
                        if device not in rainbows:
                            c.pause_program(device=device)
                            paused.append(device)
                    result["two_rainbows"] = measure(c, args.seconds, rainbows)
                    c.pause_program(device=rainbows[1])
                    paused.append(rainbows[1])
                    result["one_rainbow"] = measure(c, args.seconds, rainbows[:1])
                finally:
                    for device in paused:
                        c.resume_program(device=device)
                assert c.get_config(saved=True) == before["saved_config"]
                assert c.procedures() == before["library"]
                for name, data in result.items():
                    print(json.dumps({"scenario": name, **data["summary"]}), flush=True)
            print(json.dumps(result.get("summary", result), indent=2), flush=True)
            if args.output:
                args.output.write_text(json.dumps(result, indent=2) + "\n")
    finally:
        try:
            if getattr(c, "connection_api", 0):
                c.manage("session_close")
        finally:
            c.close()


if __name__ == "__main__":
    main()
