#!/usr/bin/env python3
"""Freeze numeric workloads, compare scalar/bulk frames and measure the target.

The caller selects and verifies firmware separately. Temporary programs and
output configuration are restored on exit; saved configuration is never changed.
Only the selected output pin is driven. Logical counts above its physical length
test execution and admission, not optical output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from lightvm.portable import PortableCompiler
from client import Client, SerialTransport, ProcessTransport, FRAME, STOP
from benchmark_saved_preset import sample


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(directory):
    directory.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for file in sorted((ROOT / 'tests/fixtures/numeric').glob('*.py')):
        source = file.read_text()
        (directory / file.name).write_text(source)
        for variant, optimize in (('scalar', False), ('bulk', True)):
            compiler = PortableCompiler(source, optimize=optimize)
            data, metadata = compiler.compile()
            maps = sum(row[0] == 39 for row in compiler.code)
            if maps and not optimize: raise ValueError((file.name, variant, maps))
            name = file.stem + '-' + variant
            (directory / (name + '.lbc')).write_bytes(data)
            manifest[name] = {'source_sha256': sha(source.encode()), 'bytecode_sha256': sha(data),
                              'bytes': len(data), 'maps': maps, 'requires': metadata['requires'],
                              'ordered_reasons': [reason for _, admitted, reason in compiler.map_reports if not admitted]}
    (directory / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def configure(client, names, count, pin='GP16'):
    client.configure({'schema': 'lightvm.config', 'outputs': []})
    client.configure({'schema': 'lightvm.config', 'outputs': [{
        'id': 'numeric', 'type': 'ws2812b',
        'config': {'pin': pin, 'count': count, 'brightness_limit': 32}}]})
    client.configure_composition([{'id': 'layer' + str(i), 'program': 'nt_' + name}
                                  for i, name in enumerate(names)], output='numeric')


def replay(client, name, count, pin='GP16'):
    configure(client, [name], count, pin)
    client.require_ok(client.command(STOP, output='numeric'))
    client.reset_program(output='numeric', layer='layer0')
    before = client.program_state(output='numeric', layer='layer0')
    images = []
    for ms in (0, 1, 17, 333, 1000):
        client.require_ok(client.command(FRAME, struct.pack('<I', ms), output='numeric'))
        images.append(bytes(client.snapshot(output='numeric', layer='layer0')[1]))
    after = client.program_state(output='numeric', layer='layer0')
    return images, {'name': name, 'pixels': count, 'frame_sha256': [sha(p) for p in images],
                    'before': before, 'after': after}


def run(args):
    manifest = json.loads((args.packages / 'manifest.json').read_text())
    cases = args.case or sorted({n.rsplit('-', 1)[0] for n in manifest})
    names = [name + '-' + mode for name in cases for mode in ('scalar', 'bulk')]
    pin = args.pin
    client = Client(SerialTransport(args.port, no_reset=args.no_reset)
                    if args.port else ProcessTransport(args.process), timeout=120)
    result = {'label': args.label, 'firmware_sha256': sha(args.firmware.read_bytes()),
              'packages': {n: manifest[n] for n in names}, 'replays': [], 'samples': [],
              'warmup_seconds': args.warmup, 'pin': pin,
              'network_regime': args.network_regime}
    created = []

    def checkpoint():
        args.result.write_text(json.dumps(result, indent=2) + '\n')

    try:
        original = client.get_config()
        result['description_before'] = client.describe()
        existing = {p['name'] for p in client.programs()}
        if existing.intersection('nt_' + n for n in names):
            raise ValueError('temporary numeric program names already exist')
        for name in names:
            data = (args.packages / (name + '.lbc')).read_bytes()
            if sha(data) != manifest[name]['bytecode_sha256']:
                raise ValueError('package hash changed: ' + name)
            client.save_program('nt_' + name, data)
            created.append('nt_' + name)
        if args.correctness:
            for count in args.pixels:
                for name in cases:
                    expected, scalar = replay(client, name + '-scalar', count, pin)
                    actual, bulk = replay(client, name + '-bulk', count, pin)
                    result['replays'].append({'case': name, 'pixels': count,
                                              'equal': actual == expected, 'scalar': scalar, 'bulk': bulk})
                    checkpoint()
                    if actual != expected:
                        raise AssertionError((name, count, scalar['frame_sha256'], bulk['frame_sha256']))
                    print(json.dumps({'label': args.label, 'case': name, 'pixels': count, 'equal': True}), flush=True)
        else:
            for count in args.pixels:
                for trial in range(args.samples):
                    for name in cases:
                        # AB/BA pairs keep both modes on the same firmware/inputs.
                        for variant in (('scalar', 'bulk') if trial % 2 == 0 else ('bulk', 'scalar')):
                            program = name + '-' + variant
                            began = time.monotonic()
                            configure(client, [program], count, pin)
                            setup_ms = (time.monotonic() - began) * 1000
                            memory = client.program_state(output='numeric', layer='layer0')
                            time.sleep(args.warmup)
                            row = sample(client, 'numeric', args.seconds)
                            row.update(case=name, variant=variant, trial=trial + 1, pixels=count,
                                       setup_ms=setup_ms, memory=memory)
                            result['samples'].append(row)
                            checkpoint()
                            print(json.dumps({'label': args.label, 'case': name, 'variant': variant,
                                              'trial': trial + 1, 'pixels': count,
                                              'mean_render_us': row['layers']['A/layer0']['mean_render_us'],
                                              'status_p95_ms': row['status_latency_ms']['p95']}), flush=True)
            if args.mixed:
                selected = [name + '-bulk' for name in cases[:2]]
                configure(client, selected, args.pixels[0], pin)
                time.sleep(args.warmup)
                row = sample(client, 'numeric', args.seconds)
                row.update(case='mixed', programs=selected, pixels=args.pixels[0])
                result['samples'].append(row)
        result['description_after'] = client.describe()
        result['passed'] = True
        checkpoint()
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--port')
    parser.add_argument('--pin', default='GP16', help='output pin; use D1 for XIAO ESP32-C3')
    parser.add_argument('--no-reset', action='store_true', help='keep ESP32 USB control lines deasserted')
    parser.add_argument('--network-regime', default='existing network configuration, USB status polling at 20 Hz')
    parser.add_argument('--process', type=Path)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--label')
    parser.add_argument('--result', type=Path)
    parser.add_argument('--case', action='append')
    parser.add_argument('--pixels', type=int, nargs='+', default=[200])
    parser.add_argument('--correctness', action='store_true')
    parser.add_argument('--mixed', action='store_true')
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--warmup', type=float, default=3)
    args = parser.parse_args()
    if args.prepare:
        prepare(args.packages)
    else:
        if bool(args.port) == bool(args.process) or not all((args.firmware, args.label, args.result)):
            parser.error('supply one transport, firmware identity, label and result')
        if any(n < 1 or n > 1024 for n in args.pixels) or args.samples < 1 or args.seconds <= 0 or args.warmup < 0:
            parser.error('invalid measurement parameters')
        run(args)


if __name__ == '__main__':
    main()
