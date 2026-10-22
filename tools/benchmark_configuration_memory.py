#!/usr/bin/env python3
"""Exercise atomic config validation with four live 1,024-pixel scalar layers.

Uses a preinstalled hw_scalar package, verified against --packages. Never clears
the composition to make validation fit. Saved configuration is not modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import time

from benchmark_pico_campaign import setup
from client import Client, ProtocolError, SerialTransport


EMPTY = {'schema': 'lightvm.config', 'outputs': []}


def run(args):
    client = Client(SerialTransport(args.port), timeout=120)
    result = {'label': args.label, 'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'pixels': 1024, 'layers': 4, 'explicit_composition_release': False, 'samples': []}
    original = None
    changed = False
    try:
        data = (args.packages / 'scalar.lbc').read_bytes()
        if client.get_program('hw_scalar') != data:
            raise ValueError('resident hw_scalar differs from archived package')
        result['package_sha256'] = hashlib.sha256(data).hexdigest()
        original = client.get_config()
        boot = client.describe()['boot_id']
        for trial in range(args.samples):
            changed = True
            setup(client, {'programs': ['scalar'] * 4}, 1024)
            time.sleep(1)
            before = client.transition(output='bench')['A']
            heap_before = int(client.status_fields(output='bench')['heap'])
            try:
                client.configure({'schema': 'invalid', 'outputs': []})
            except ProtocolError as error:
                rejected = str(error)
                if 'schema' not in rejected:
                    raise RuntimeError('invalid config failed for an unexpected reason: ' + rejected)
            else:
                raise RuntimeError('invalid configuration was accepted')
            time.sleep(1)
            after = client.transition(output='bench')['A']
            heap_after = int(client.status_fields(output='bench')['heap'])
            if heap_after != heap_before:
                raise RuntimeError('rejected validation retained VM heap allocations')
            if len(before['layers']) != 4 or len(after['layers']) != 4:
                raise RuntimeError('large scene lost a layer')
            for old, new in zip(before['layers'], after['layers']):
                if new['generation'] != old['generation'] or new['frames'] <= old['frames'] or new['faulted']:
                    raise RuntimeError('rejected validation disrupted the live scene')
            started = time.monotonic()
            client.configure(EMPTY)
            elapsed = (time.monotonic() - started) * 1000
            description = client.describe()
            if description['boot_id'] != boot or description['outputs']:
                raise RuntimeError('empty replacement reset the controller or left outputs active')
            result['samples'].append({'trial': trial + 1, 'boot_id': boot, 'heap_before': heap_before,
                                      'heap_after_rejected': heap_after, 'invalid_error': rejected,
                                      'before': before, 'after_rejected': after,
                                      'empty_replacement_ms': elapsed, 'empty_replacement_succeeded': True})
            args.result.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps({'trial': trial + 1, 'live_heap': heap_before, 'replacement_ms': elapsed}), flush=True)
        result['complete'] = True
        args.result.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        try:
            if changed and original is not None:
                client.configure(EMPTY)
                client.configure(original)
        finally:
            client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--packages', required=True, type=Path)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--samples', type=int, default=3)
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('at least one sample required')
    run(args)
