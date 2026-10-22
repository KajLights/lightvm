#!/usr/bin/env python3
"""Measure input-to-readback and distinct dirty-to-display events under sprites."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import time

from benchmark_pico_campaign import setup
from client import Client, SerialTransport


def run(args):
    client = Client(SerialTransport(args.port), timeout=30)
    original = client.get_config()
    created = []
    result = {'label': args.label, 'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'scope': 'host input-to-composite-readback plus candidate firmware output-completion stamps; no optical measurement',
              'events': []}
    try:
        existing = {p['name'] for p in client.programs()}
        for name in ('reactive', 'timed_sprites'):
            if 'hw_' + name in existing:
                raise ValueError('benchmark program already exists')
            client.save_program('hw_' + name, (args.packages / (name + '.lbc')).read_bytes())
            created.append('hw_' + name)
        setup(client, {'programs': ['reactive', 'timed_sprites']}, 200)
        time.sleep(7)
        result['before'] = client.transition(output='bench')
        for index in range(args.events):
            brightness = .25 if index % 2 == 0 else .75
            expected_red = 64 if index % 2 == 0 else 191
            prior = client.transition(output='bench')['A']['layers'][0]
            started = time.monotonic()
            client.set_inputs({'brightness': {'value': brightness}}, output='bench', layer='layer0')
            acknowledged = time.monotonic()
            observed = None
            while time.monotonic() - started < 5:
                metadata, pixels = client.composition_snapshot(output='bench', slot='A')
                # At most ten 3-pixel sprites cover a minority of the output.
                if statistics.median(pixels[0::3]) == expected_red:
                    observed = time.monotonic()
                    break
            if observed is None:
                raise RuntimeError('input did not reach the composite readback')
            layer = client.transition(output='bench')['A']['layers'][0]
            if 'dependency_current' in layer:
                while time.monotonic() - started < 5 and (layer['dependency_revision'] == prior['dependency_revision'] or
                        not layer['dependency_current'] or layer['dirty_age_us'] is not None):
                    layer = client.transition(output='bench')['A']['layers'][0]
                if layer['dependency_revision'] == prior['dependency_revision'] or not layer['dependency_current'] or layer['dirty_age_us'] is not None:
                    raise RuntimeError('new input dependency never became displayed/current')
            row = {'index': index, 'brightness': brightness, 'acceptance_ms': (acknowledged - started) * 1000,
                   'composite_readback_ms': (observed - started) * 1000, 'prior': prior, 'completed': layer}
            result['events'].append(row)
            args.result.write_text(json.dumps(result, indent=2) + '\n')
            time.sleep(.15)
        result['after'] = client.transition(output='bench')
        result['description_after'] = client.describe()
        result['complete'] = True
        args.result.write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps({'events': len(result['events']), 'readback_ms_median': statistics.median(r['composite_readback_ms'] for r in result['events'])}), flush=True)
    finally:
        client.configure(original)
        for name in created:
            client.delete_program(name)
        client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--packages', required=True, type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--events', default=100, type=int)
    args = parser.parse_args()
    if args.events < 1:
        parser.error('at least one event required')
    run(args)
