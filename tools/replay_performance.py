#!/usr/bin/env python3
"""Compare sprite implementations with fixed clocks, seeds and input changes.

Uses a fresh host process per variant. Host timings are diagnostic only; they
are not estimates of Pico throughput. Original fixtures never follow examples.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, FRAME, STOP
from lightvm.compiler import compile_source

FIXTURES = ROOT / 'tests/fixtures/performance'
COUNTERS = ('dispatch_count', 'slice_count', 'render_total_us', 'native_draw_us')


def fixed_variants(population=12):
    source = (FIXTURES / 'fixed-sprites.py').read_text()
    source = source.replace('capacity=12', f'capacity={max(1, population)}').replace('range(12)', f'range({population})')
    geometry = ' s.transform=Transform(translate=s.position,origin=1.0)\n'
    style = ' s.color=ColorTransform(alpha=0.75)\n'
    geometry_source = source.replace(geometry, '').replace(' s.position=position\n', ' s.position=position\n' + geometry)
    style_source = geometry_source.replace(style, '').replace(' s.position=position\n', ' s.position=position\n' + style)
    return {'original': source, 'geometry_at_spawn': geometry_source, 'style_at_spawn': style_source}


def replay(executable, source, frames, *, optimize=True, animated=False):
    artifact = compile_source(source, optimize=optimize)
    if not artifact.success:
        raise ValueError(artifact.diagnostics)
    client = Client(ProcessTransport(str(Path(executable).resolve())))
    images = []
    try:
        pixel_count = min(200, client.describe()['pixel_capacity'])
        client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': pixel_count}}]})
        client.save_program('replay', artifact.bytecode)
        client.configure_composition([{'id': 'fg', 'program': 'replay'}], output='lights')
        client.require_ok(client.command(STOP, output='lights'))
        settings = {'capacity': 20, 'duration': 2.0, 'probability': 5.0,
                    'color': 16777215, 'random_colors': False} if animated else {}
        client.reset_program(output='lights', layer='fg', settings=settings)
        before = client.program_state(output='lights', layer='fg')
        for index in range(frames):
            if animated and index in (100, 200):
                client.set_inputs({'brightness': {'value': 0.4 if index == 100 else 1.0}}, output='lights', layer='fg')
            client.require_ok(client.command(FRAME, struct.pack('<I', 16), output='lights'))
            images.append(bytes(client.pixel_snapshot))
        after = client.program_state(output='lights', layer='fg')
        return {
            'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
            'bytecode_sha256': hashlib.sha256(artifact.bytecode).hexdigest(),
            'bytecode_bytes': len(artifact.bytecode), 'frames': frames, 'delta_ms': 16,
            'pixel_count': pixel_count,
            'optimize': optimize, 'settings': settings,
            'sha256_all_frames': hashlib.sha256(b''.join(images)).hexdigest(),
            'counters': {key: after[key] - before[key] for key in COUNTERS},
        }, images
    finally:
        client.close()


def measure(executable, frames=300, populations=(0, 1, 6, 12, 20)):
    result = {'scope': 'fixed-step pixel equivalence and dispatches; host timings only',
              'executable_sha256': hashlib.sha256(Path(executable).read_bytes()).hexdigest(),
              'fixed': {}, 'animated': {}}
    for population in populations:
        rows = {}; reference = None
        for name, source in fixed_variants(population).items():
            row, images = replay(executable, source, 10)
            if reference is None: reference = images
            if reference != images: raise AssertionError(f'fixed {population}: {name} differs')
            rows[name] = row
        result['fixed'][str(population)] = rows
    original = (FIXTURES / 'stars-original.py').read_text().replace('random.seed()', 'random.seed(7)')
    current = (ROOT / 'examples/animations/stars.py').read_text().replace('random.seed()', 'random.seed(7)')
    reference = None
    for name, source, optimize in [('original_reference', original, False),
                                    ('original_optimized', original, True), ('current', current, True)]:
        row, images = replay(executable, source, frames, optimize=optimize, animated=True)
        if reference is None: reference = images
        if reference != images: raise AssertionError(f'animated: {name} differs')
        result['animated'][name] = row
    result['all_frames_equal'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--process', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=300)
    args = parser.parse_args()
    if args.frames < 300: parser.error('at least 300 frames exercise brightness and expiry')
    result = measure(args.process, args.frames)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'all_frames_equal': True, 'animated_dispatches': {
        name: row['counters']['dispatch_count'] for name, row in result['animated'].items()}}))


if __name__ == '__main__': main()
