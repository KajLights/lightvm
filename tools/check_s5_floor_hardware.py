#!/usr/bin/env python3
"""Verify newer packages cannot replace live state on an older application."""
import argparse
import json
from pathlib import Path
import sys
from check_s5_hardware import sha, reject, inventory
from client import Client, SerialTransport, STOP, FRAME

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests/fixtures'))
import independent_emitter as emitter


def run(args):
    c = Client(SerialTransport(args.port), timeout=120)
    result = {'firmware_sha256': sha(args.firmware.read_bytes()), 'cases': []}
    try:
        description = c.describe()
        assert description['version'] == '1.1.0', description['version']
        original, saved, programs = c.get_config(), c.get_config(saved=True), inventory(c)
        assert not any(p['name']=='s5_floor' for p in programs), 'test name already exists'
        test_name_clear = True
        light = c.light_state(output='lights')
        c.configure_composition([{'id': 'guard', 'program': 'rainbow'}], output='lights')
        c.require_ok(c.command(STOP, output='lights'))
        c.reset_program(output='lights', layer='guard')
        c.require_ok(c.command(FRAME, bytes(4), output='lights'))
        state = c.composition(output='lights')
        pixels = c.snapshot(output='lights', layer='guard')[1]
        generation = [(p['id'], p['generation']) for p in state['layers']]
        heaps = [int(c.status_fields(output='lights')['heap'])]
        packages = {p.stem: p.read_bytes() for p in sorted(args.packages.glob('*.lbc'))}
        packages.update({'independent-scalar': emitter.emit(False), 'independent-bulk': emitter.emit()})
        for name, data in packages.items():
            result['cases'].append({'name': name, 'package_sha256': sha(data),
                'error': reject(lambda: c.save_program('s5_floor', data))})
            assert [(p['id'], p['generation']) for p in c.composition(output='lights')['layers']] == generation
            assert c.snapshot(output='lights', layer='guard')[1] == pixels
            heaps.append(int(c.status_fields(output='lights')['heap']))
        assert len(set(heaps)) == 1, heaps
        assert c.get_config() == original and c.get_config(saved=True) == saved
        assert inventory(c) == programs
        result.update(description=description, heap_samples=heaps, frame_and_generation_retained=True,
                      programs_and_config_unchanged=True, passed=True)
    finally:
        try:
            if 'test_name_clear' in locals():
                for p in c.programs():
                    if p['name']=='s5_floor': c.delete_program(p['name'], p['type'])
            if 'light' in locals():
                c.set_light(power=light['power'], brightness=light['brightness'], preset=light['preset'], output='lights')
        finally:
            c.close(); args.result.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--result', type=Path, required=True)
    run(parser.parse_args())
