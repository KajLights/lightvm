#!/usr/bin/env python3
"""Check lazy faults, frame retention, recovery and rejected admission on a controller."""
import argparse
import board_monitor
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, SerialTransport, ProcessTransport, FRAME, STOP, ProtocolError
from lightvm.portable import PortableCompiler

SOURCE = '''import math
from lightvm import profile,init,render,rgb,Input,PixelArray
profile('animation')
BAD=Input('bad',default=False)
@init
def setup(ctx,runtime):ctx.image=PixelArray(runtime.pixel_count)
@render
def draw(ctx,runtime,pixels):
 bad=runtime.input(BAD)
 for i in range(pixels.count):
  x=1.0 if i<31 else math.sqrt(-1.0 if bad else 1.0)
  ctx.image[i]=rgb(int(x*100.0),i%255,17)
 pixels.draw(ctx.image)
'''
LARGE = '''from lightvm import profile,init,render,FixedArray
profile('animation')
@init
def setup(ctx,runtime):ctx.large=FixedArray(65535)
@render
def draw(ctx,runtime,pixels):pass
'''


def reject(action):
    try:
        action()
    except ProtocolError as error:
        return str(error)
    raise AssertionError('invalid operation was accepted')


def run(args):
    client = Client(SerialTransport(args.port, no_reset=args.no_reset) if args.port else ProcessTransport(args.process), timeout=120)
    board_monitor.attach(client, args)
    result = {'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'source': SOURCE, 'oversized_source': LARGE, 'cases': []}
    created = []
    try:
        original = client.get_config()
        result['description'] = client.describe()
        names = {p['name'] for p in client.programs()}
        if names.intersection(('nf_scalar', 'nf_bulk', 'nf_large', 'nf_invalid')):
            raise ValueError('numeric fault test names already exist')
        large, _ = PortableCompiler(LARGE).compile()
        client.save_program('nf_large', large)
        created.append('nf_large')
        for optimize in (False, True):
            name = 'nf_bulk' if optimize else 'nf_scalar'
            compiler = PortableCompiler(SOURCE, optimize=optimize)
            data, _ = compiler.compile()
            assert any(i[0] == 39 for i in compiler.code) == optimize
            client.save_program(name, data)
            created.append(name)
            for count in (33, 129):
                client.configure({'schema': 'lightvm.config', 'outputs': []})
                client.configure({'schema': 'lightvm.config', 'outputs': [{
                    'id': 'numeric', 'type': 'ws2812b',
                    'config': {'pin': args.pin, 'count': count, 'brightness_limit': 32}}]})
                client.configure_composition([{'id': 'fg', 'program': name}], output='numeric')
                client.require_ok(client.command(STOP, output='numeric'))
                client.reset_program(output='numeric', layer='fg')
                client.require_ok(client.command(FRAME, bytes(4), output='numeric'))
                before = bytes(client.snapshot(output='numeric', layer='fg')[1])
                heaps = [int(client.status_fields(output='numeric')['heap'])]
                generation = client.program_state(output='numeric', layer='fg')['generation']
                row = {'bulk': optimize, 'pixels': count, 'package_sha256': hashlib.sha256(data).hexdigest(),
                       'frame_sha256': hashlib.sha256(before).hexdigest(), 'rejections': []}
                for _ in range(10):
                    row['rejections'].append(reject(lambda: client.configure_composition(
                        [{'id': 'fg', 'program': 'nf_large'}], output='numeric')))
                    assert client.program_state(output='numeric', layer='fg')['generation'] == generation
                    assert bytes(client.snapshot(output='numeric', layer='fg')[1]) == before
                    heaps.append(int(client.status_fields(output='numeric')['heap']))
                if optimize:
                    row['floor_rejection'] = reject(lambda: client.save_program('nf_invalid',
                        data.replace(b'"requires":"0.1.0"', b'"requires":"0.0.9"')))
                client.set_inputs({'bad': {'value': True}}, output='numeric', layer='fg')
                row['fault'] = reject(lambda: client.require_ok(client.command(FRAME, struct.pack('<I', 17), output='numeric')))
                assert bytes(client.snapshot(output='numeric', layer='fg')[1]) == before
                row['after_fault'] = client.program_state(output='numeric', layer='fg')
                client.reset_program(output='numeric', layer='fg', inputs={'bad': {'value': False}})
                client.require_ok(client.command(FRAME, bytes(4), output='numeric'))
                assert bytes(client.snapshot(output='numeric', layer='fg')[1]) == before
                heaps.append(int(client.status_fields(output='numeric')['heap']))
                assert len(set(heaps)) == 1, heaps
                row.update(heap_samples=heaps, retained_frame=True, recovered=True)
                result['cases'].append(row)
                args.result.write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps({'bulk': optimize, 'pixels': count, 'passed': True}), flush=True)
        result['passed'] = True
        args.result.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        try:
            if 'original' in locals():
                client.configure({'schema': 'lightvm.config', 'outputs': []})
                client.configure(original)
        finally:
            try:
                for name in created:
                    client.delete_program(name)
            finally:
                client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port')
    parser.add_argument('--pin', default='GP16')
    parser.add_argument('--no-reset', action='store_true')
    parser.add_argument('--process', type=Path)
    parser.add_argument('--firmware', type=Path, required=True)
    parser.add_argument('--result', type=Path, required=True)
    board_monitor.add_arguments(parser)
    args = parser.parse_args()
    if bool(args.port) == bool(args.process):
        parser.error('select exactly one transport')
    run(args)
