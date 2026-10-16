#!/usr/bin/env python3
"""Exercise async output through the application API, restoring playback afterward."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import time

from measure import Client, SerialPort, brief, restore
from client import FRAME, PLAY


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    before = json.loads(args.before.read_text())
    c = Client(SerialPort(args.port), timeout=20)
    report = {"passed": False, "reconfigure_cycles": 0}
    try:
        initial = c.describe()
        assert initial["device_id"] == before["description"]["device_id"]
        assert c.get_config() == before["config"]
        baseline_heap = int(c.status_fields()["heap"])
        held, changed = "lights", "twinkle_cool"
        c.pause_program(device=held)
        meta, pixels = c.snapshot(device=held)
        state = brief(meta["state"])
        c.reset_program(device=changed, settings=before["states"][changed]["settings_values"])
        time.sleep(0.25)
        after_meta, after_pixels = c.snapshot(device=held)
        for field in ("generation", "program_us", "output_frame", "render_count", "settings_values"):
            assert state[field] == after_meta["state"][field], field
        assert pixels == after_pixels and len(pixels) == 600
        report["held_frame_sha256"] = hashlib.sha256(pixels).hexdigest()
        report["pause_peer_reset_isolation"] = True
        c.require_ok(c.command(FRAME, struct.pack("<I", 16), device=held))
        frame = bytes(c.pixel_snapshot)
        _, snapshot = c.snapshot(device=held)
        assert len(frame) == 600 and frame == snapshot
        report["synchronous_manual_frame"] = True
        c.resume_program(device=held)

        # Exercise native flash erase/program checkpoints while peer DMA output
        # continues. Install the same captured program transiently, then return
        # to the named instance in restore(). No named procedure is overwritten.
        peers = {d: c.program_parameters(device=d) for d in ("lights", "lights2")}
        c.upload((args.before.parent / "twinkle.lbc").read_bytes(), device=changed)
        c.reset_program(device=changed, settings=before["states"][changed]["settings_values"])
        c.require_ok(c.command(PLAY, device=changed))
        for device, previous in peers.items():
            current = c.program_parameters(device=device)
            assert current["generation"] == previous["generation"]
            assert current["render_count"] > previous["render_count"]
        report["flash_upload_with_live_peer_output"] = True

        # Remove/recreate both Twinkle drivers while the Rainbows retain their
        # VM instances. This exercises real DMA cleanup with ongoing playback.
        rainbows = copy.deepcopy(before["config"])
        rainbows["devices"] = rainbows["devices"][:2]
        for n in range(8):
            previous = {d: c.program_parameters(device=d) for d in ("lights", "lights2")}
            c.configure(rainbows, save=False)
            c.configure(copy.deepcopy(before["config"]), save=False)
            for device in ("twinkle_warm", "twinkle_cool"):
                c.activate("twinkle", device=device, settings=before["states"][device]["settings_values"])
            current = {d: c.program_parameters(device=d) for d in previous}
            assert all(current[d]["generation"] == previous[d]["generation"] and
                       current[d]["render_count"] > previous[d]["render_count"] for d in previous)
            assert int(c.status_fields()["heap"]) == baseline_heap
            assert c.describe()["boot_id"] == initial["boot_id"]
            report["reconfigure_cycles"] = n + 1
        assert c.get_config(saved=True) == before["saved_config"]
        assert c.procedures() == before["library"]
        for entry in before["library"]:
            assert c.get_procedure(entry["name"]) == (args.before.parent / (entry["name"] + ".lbc")).read_bytes()
        report.update(passed=True, vm_heap_bytes=baseline_heap, same_boot=True,
                      saved_configuration_and_program_bytes_unchanged=True)
    finally:
        try:
            restore(c, before)
        finally:
            args.output.write_text(json.dumps(report, indent=2) + "\n")
            try:
                if getattr(c, "connection_api", 0):
                    c.manage("session_close")
            finally:
                c.close()
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
