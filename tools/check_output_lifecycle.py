#!/usr/bin/env python3
"""Check current output isolation on two configured outputs, restoring pause flags.

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
        for output in (args.held, args.peer): states[output] = client.program_state(output=output)
        saved = client.get_config(saved=True)
        catalogue = client.programs()
        client.pause_program(output=args.held)
        before, pixels = client.snapshot(output=args.held)
        client.reset_program(output=args.peer)
        time.sleep(.25)
        after, stable = client.snapshot(output=args.held)
        for field in ('generation', 'program_us', 'output_frame', 'render_count', 'settings_values'):
            assert before['state'][field] == after['state'][field], field
        assert pixels == stable
        client.require_ok(client.command(FRAME, struct.pack('<I', 16), output=args.held))
        assert bytes(client.pixel_snapshot) == client.snapshot(output=args.held)[1]
        assert client.get_config(saved=True) == saved and client.programs() == catalogue
        print(json.dumps({'paused_frame_isolation': True, 'manual_frame_matches_snapshot': True, 'persistent_metadata_unchanged': True}))
    finally:
        try:
            for output, state in states.items():
                current = client.program_state(output=output)
                if current['paused'] != state['paused']:
                    (client.pause_program if state['paused'] else client.resume_program)(output=output)
        finally:
            client.close()


if __name__ == '__main__': main()
