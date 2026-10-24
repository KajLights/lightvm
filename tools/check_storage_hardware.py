#!/usr/bin/env python3
"""Check flash read coherence and pinned programs during repeated replacement.

Exercises reclaimed flash extents, readback, rejected deletion while live, and activation
through the normal API. The temporary program and active config are restored.
"""
import argparse
import board_monitor
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source
from client import Client, SerialTransport, ProcessTransport, ProtocolError, FRAME, STOP
from benchmark_controller import DEFAULT_PINS


def run(args):
    client = Client(SerialTransport(args.port, no_reset=args.board.startswith('xiao_esp32'))
                    if args.port else ProcessTransport(args.process), timeout=30)
    board_monitor.attach(client, args)
    name = 'storage_coherence_probe'
    result = {'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'cycles': [], 'package_sha256': []}
    packages = []
    colors = [(17, 51, 93), (231, 128, 7)]
    for color in colors:
        source = ('from lightvm import profile,render,PixelArray\nprofile("animation")\n'
                  f'IMAGE=PixelArray({list(color) * 1000!r},format="rgb8",readonly=True)\n'
                  '@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(IMAGE)\n')
        artifact = compile_source(source)
        if not artifact.success:
            raise ValueError(artifact.diagnostics)
        packages.append(artifact.bytecode)
        result['package_sha256'].append(hashlib.sha256(artifact.bytecode).hexdigest())
    created = False
    try:
        result['before'] = client.describe()
        if result['before']['board'] != args.board:
            raise ValueError('unexpected board')
        original = client.get_config()
        if name in {p['name'] for p in client.programs()}:
            raise ValueError('temporary test name already exists')
        client.configure({'schema': 'lightvm.config', 'outputs': []})
        client.configure({'schema': 'lightvm.config', 'outputs': [{
            'id': 'probe', 'type': 'ws2812b', 'config': {
                'pin': args.pin or DEFAULT_PINS[args.board], 'count': 200, 'brightness_limit': 32}}]})
        client.save_program(name, packages[0])
        created = True
        client.configure_composition([{'id': 'image', 'program': name}], output='probe')
        client.require_ok(client.command(STOP, output='probe'))
        active = 0

        def check_frame(index):
            client.require_ok(client.command(FRAME, bytes(4), output='probe'))
            assert bytes(client.pixel_snapshot) == bytes(colors[index]) * 200, 'stale or corrupted image'

        check_frame(active)
        for cycle in range(args.cycles):
            candidate = 1 - active
            generation = client.program_state(output='probe', layer='image')['generation']
            client.save_program(name, packages[candidate])
            assert client.get_program(name) == packages[candidate], 'flash readback differs'
            check_frame(active)
            assert client.program_state(output='probe', layer='image')['generation'] == generation
            try:
                client.delete_program(name)
            except ProtocolError as error:
                assert 'referenced' in str(error), str(error)
            else:
                raise AssertionError('live program deletion was accepted')
            check_frame(active)
            client.clear_composition(output='probe')
            client.delete_program(name)
            client.save_program(name, packages[candidate])
            assert client.get_program(name) == packages[candidate]
            client.configure_composition([{'id': 'image', 'program': name}], output='probe')
            client.require_ok(client.command(STOP, output='probe'))
            active = candidate
            check_frame(active)
            status = client.status_fields(output='probe')
            result['cycles'].append({'cycle': cycle + 1, 'heap': int(status['heap']),
                                     'catalog_revision': client.describe()['catalog_revision']})
            args.result.write_text(json.dumps(result, indent=2) + '\n')
        assert len({row['heap'] for row in result['cycles']}) == 1, 'replacement heap grew'
        result['after'] = client.describe()
        assert result['after']['boot_id'] == result['before']['boot_id'], 'controller rebooted'
        result['passed'] = True
        args.result.write_text(json.dumps(result, indent=2) + '\n')
        print(f'{args.cycles} cycles: readback, pinned frames, deletion, activation and stable heap passed.')
    finally:
        try:
            if 'original' in locals():
                client.configure({'schema': 'lightvm.config', 'outputs': []})
                client.configure(original)
            if created and name in {p['name'] for p in client.programs()}:
                client.delete_program(name)
        finally:
            client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    transport = parser.add_mutually_exclusive_group(required=True)
    transport.add_argument('--port')
    transport.add_argument('--process', type=Path)
    parser.add_argument('--board', required=True, choices=DEFAULT_PINS)
    parser.add_argument('--pin')
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--cycles', type=int, default=32)
    board_monitor.add_arguments(parser)
    args = parser.parse_args()
    if args.cycles < 1:
        parser.error('cycles must be positive')
    run(args)
