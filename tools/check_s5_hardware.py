#!/usr/bin/env python3
"""S5 frontend/emitter package acceptance, rejection retention and restoration.

Materialize frontend packages with numeric-frontends.test.ts and
LIGHTVM_FRONTEND_FIXTURES_OUT. This harness never reads or backs up flash.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests/fixtures')]
from client import Client, SerialTransport, ProcessTransport, FRAME, STOP, ProtocolError
import independent_emitter as emitter


def sha(data): return hashlib.sha256(data).hexdigest()


def reject(action):
    try: action()
    except ProtocolError as error: return str(error)
    raise AssertionError('candidate was unexpectedly accepted')


def inventory(client):
    return sorted([{**p, 'sha256': sha(client.get_program(p['name'], p['type']))}
                   for p in client.programs()], key=lambda p: (p['name'], p['type']))


def run(args):
    c = Client(SerialTransport(args.port) if args.port else ProcessTransport(args.process), timeout=120)
    result = {'firmware_sha256': sha(args.firmware.read_bytes()), 'cases': [], 'rejections': []}
    created = []
    original = None
    try:
        original, saved = c.get_config(), c.get_config(saved=True)
        programs = inventory(c)
        presets = {p['name']: c.preset(p['name']) for p in c.presets()}
        light = c.light_state(output='lights') if any(o['id']=='lights' for o in original['outputs']) else None
        result.update(description=c.describe(), initial_light=light)
        assert not any(p['name'].startswith('s5_') for p in programs), 'test names already exist'
        test_names_clear = True
        packages = {p.stem: p.read_bytes() for p in sorted(args.packages.glob('*.lbc'))}
        packages.update({name: data for name, data in emitter.corpus().items() if not name.startswith('reject-')})
        assert len(packages) == 10, packages.keys()
        names = {name: 's5_'+str(i) for i, name in enumerate(packages)}
        for name, data in packages.items():
            c.save_program(names[name], data); created.append(names[name])
        c.save_program('s5_large', emitter.emit(oversized=True)); created.append('s5_large')
        for count in (33, 200):
            c.configure({'schema': 'lightvm.config', 'outputs': []})
            c.configure({'schema': 'lightvm.config', 'outputs': [{'id': 'numeric', 'type': 'ws2812b',
                         'config': {'pin': 'GP16', 'count': count, 'brightness_limit': 32}}]})
            for name, data in packages.items():
                c.configure_composition([{'id': 'fg', 'program': names[name]}], output='numeric')
                c.require_ok(c.command(STOP, output='numeric'))
                c.reset_program(output='numeric', layer='fg')
                heaps, latency = [], []
                for frame in range(6):
                    c.require_ok(c.command(FRAME, struct.pack('<I', frame*17), output='numeric'))
                    pixels = c.snapshot(output='numeric', layer='fg')[1]
                    assert pixels == emitter.expected_pixels(count), (name, count, pixels.hex())
                    started = time.monotonic(); info = c.describe(); latency.append((time.monotonic()-started)*1000)
                    heaps.append(int(c.status_fields(output='numeric')['heap']))
                assert len(set(heaps)) == 1, heaps
                state = c.program_state(output='numeric', layer='fg')
                row = {'name': name, 'count': count, 'package_sha256': sha(data), 'pixels_sha256': sha(pixels),
                       'heap_samples': heaps, 'describe_ms': latency, 'program_state': state, 'passed': True}
                result['cases'].append(row)
                args.result.write_text(json.dumps(result, indent=2)+'\n')
                print(json.dumps({'name': name, 'count': count, 'passed': True}), flush=True)
        before = c.snapshot(output='numeric', layer='fg')[1]
        generation = c.program_state(output='numeric', layer='fg')['generation']
        heaps = [int(c.status_fields(output='numeric')['heap'])]
        for name, data in emitter.corpus().items():
            if not name.startswith('reject-'): continue
            result['rejections'].append({'name': name, 'package_sha256': sha(data),
                'error': reject(lambda: c.save_program('s5_invalid', data))})
            assert c.program_state(output='numeric', layer='fg')['generation'] == generation
            assert c.snapshot(output='numeric', layer='fg')[1] == before
            heaps.append(int(c.status_fields(output='numeric')['heap']))
        for _ in range(10):
            result['rejections'].append({'name': 'resource-budget', 'error': reject(lambda:
                c.configure_composition([{'id': 'fg', 'program': 's5_large'}], output='numeric'))})
            assert c.program_state(output='numeric', layer='fg')['generation'] == generation
            assert c.snapshot(output='numeric', layer='fg')[1] == before
            heaps.append(int(c.status_fields(output='numeric')['heap']))
        assert len(set(heaps)) == 1, heaps
        result.update(rejection_heap_samples=heaps, frame_and_generation_retained=True, final_description=c.describe(), passed=True)
    finally:
        try:
            if original is not None:
                c.configure({'schema': 'lightvm.config', 'outputs': []})
                cleanup = set(created)
                if 'test_names_clear' in locals(): cleanup.add('s5_invalid')
                for p in c.programs():
                    if p['name'] in cleanup: c.delete_program(p['name'], p['type'])
                c.configure(original)
                if light:
                    c.set_light(power=light['power'], brightness=light['brightness'], preset=light['preset'], output='lights')
                    time.sleep(2)
                    actual = c.light_state(output='lights')
                    assert all(actual[k] == light[k] for k in ('power', 'brightness', 'preset', 'color', 'color_control'))
                    result['restored_light'] = actual
                assert c.get_config() == original and c.get_config(saved=True) == saved
                assert inventory(c) == programs
                assert {p['name']: c.preset(p['name']) for p in c.presets()} == presets
                result['restored'] = {'config_unchanged': True, 'saved_config_unchanged': True,
                                      'programs_unchanged': True, 'presets_unchanged': True}
        finally:
            c.close(); args.result.write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--port'); transport.add_argument('--process', type=Path)
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--result', type=Path, required=True)
    run(parser.parse_args())
