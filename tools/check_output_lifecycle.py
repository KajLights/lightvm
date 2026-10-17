#!/usr/bin/env python3
"""Check current output isolation on two configured devices, restoring pause flags.

Run explicitly against a test setup. Resets the peer's transient state and clocks;
it does not save, flash, erase or change persistent configuration/programs.
"""
import argparse
import json
from pathlib import Path
import struct
import time
from client import Client, FRAME, SerialTransport, ProcessTransport


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--port')
    target.add_argument('--process')
    parser.add_argument('--storage', type=Path, help='host store containing the configured startup programs')
    parser.add_argument('--held', required=True, help='output whose paused frame must remain stable')
    parser.add_argument('--peer', required=True, help='different output whose program is reset')
    args = parser.parse_args()
    if args.storage and not args.process: parser.error('--storage requires --process')
    if args.held == args.peer: parser.error('choose two different outputs')
    client = Client(SerialTransport(args.port) if args.port else ProcessTransport(args.process, args.storage))
    states = {}
    try:
        for device in (args.held, args.peer): states[device] = client.program_state(device=device)
        saved = client.get_config(saved=True)
        catalogue = client.procedures()
        client.pause_program(device=args.held)
        before, pixels = client.snapshot(device=args.held)
        client.reset_program(device=args.peer)
        time.sleep(.25)
        after, stable = client.snapshot(device=args.held)
        for field in ('generation', 'program_us', 'output_frame', 'render_count', 'settings_values'):
            assert before['state'][field] == after['state'][field], field
        assert pixels == stable
        client.require_ok(client.command(FRAME, struct.pack('<I', 16), device=args.held))
        assert bytes(client.pixel_snapshot) == client.snapshot(device=args.held)[1]
        assert client.get_config(saved=True) == saved and client.procedures() == catalogue
        print(json.dumps({'paused_frame_isolation': True, 'manual_frame_matches_snapshot': True, 'persistent_metadata_unchanged': True}))
    finally:
        try:
            for device, state in states.items():
                current = client.program_state(device=device)
                if current['paused'] != state['paused']:
                    (client.pause_program if state['paused'] else client.resume_program)(device=device)
        finally:
            client.close()


if __name__ == '__main__': main()
