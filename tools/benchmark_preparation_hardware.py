#!/usr/bin/env python3
"""Compare synchronous/async preparation and cancellation with fixed bytecode."""
import argparse
import hashlib
import json
from pathlib import Path
import threading
import time

from client import Client, SerialTransport, HttpTransport, MANAGE


def run(args):
    client = Client(SerialTransport(args.port), timeout=120)
    original = client.get_config()
    description = client.describe()
    original_programs = {p['name'] for p in client.programs()}
    names = ('hw_prep_map', 'hw_prep_heavy')
    if not args.reuse_packages and original_programs.intersection(names):
        client.close()
        raise ValueError('preparation test program already exists')
    created, rows = [], []
    result = {'label': args.label, 'firmware_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
              'description_before': description, 'samples': rows, 'cancellations': []}
    try:
        result['reuse_verified_packages'] = args.reuse_packages
        for name, kind in zip(names, ('map', 'heavy')):
            data = (args.packages / (kind + '.lbc')).read_bytes()
            if args.reuse_packages:
                if name not in original_programs or client.get_program(name) != data:
                    raise ValueError('resident package differs or is missing: ' + name)
            else:
                client.save_program(name, data)
                created.append(name)
        client.configure({'schema': 'lightvm.config', 'outputs': []})
        client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id': 'prep', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': 200, 'brightness_limit': 32}}]})
        for trial in range(args.samples):
            client.activate(names[0], output='prep')
            time.sleep(1)
            old = client.program_state(output='prep')
            stop, ready = threading.Event(), threading.Event()
            observations = []

            def observer():
                other = None
                try:
                    other = Client(HttpTransport(args.url, timeout=10), timeout=10)
                    ready.set()
                    while not stop.is_set():
                        start = time.monotonic()
                        state = other.status_fields(output='prep')
                        observations.append({'started': start, 'ended': time.monotonic(), 'state': state})
                        stop.wait(.05)
                except Exception as error:
                    observations.append({'error': str(error)})
                    ready.set()
                finally:
                    if other:
                        other.close()

            thread = threading.Thread(target=observer, daemon=True)
            thread.start()
            if not ready.wait(15):
                raise RuntimeError('HTTP observer did not connect')
            started = time.monotonic()
            client.require_ok(client.command(MANAGE, json.dumps({'op': 'program_activate', 'name': names[1], 'output': 'prep', 'action': 'play'}).encode()))
            accepted = time.monotonic()
            operation = json.loads(client.data_reply)['operation'] if client.data_reply.startswith(b'{"operation":') else None
            stages, usb_latencies, foreground = [], [], []
            if operation:
                while not operation['terminal']:
                    before = time.monotonic()
                    status = client.status_fields(output='prep')
                    if 'fg10' in status:
                        foreground.append({'elapsed_s': time.monotonic() - started, 'over_10ms': int(status['fg10']), 'ingress_over_10ms': int(status['io10']) if 'io10' in status else None})
                    usb_latencies.append((time.monotonic() - before) * 1000)
                    operation = client.operation(operation['boot_id'], operation['id'])
                    if not stages or stages[-1]['stage'] != operation['stage']:
                        stages.append(operation)
                if operation['code']:
                    raise RuntimeError(operation)
            completed = time.monotonic()
            stop.set(); thread.join(130)
            if thread.is_alive():
                raise RuntimeError('HTTP observer did not finish')
            after = client.program_state(output='prep')
            row = {'trial': trial + 1, 'acceptance_ms': (accepted - started) * 1000,
                   'completion_ms': (completed - started) * 1000, 'operation': operation, 'stages': stages,
                   'usb_latency_ms': usb_latencies, 'foreground_gap_samples': foreground, 'before': old, 'after': after,
                   'http_valid': bool(observations) and all('error' not in r for r in observations),
                   'http_observations': [{**r, **({'started': r['started'] - started, 'ended': r['ended'] - started} if 'started' in r else {})} for r in observations]}
            rows.append(row)
            args.result.write_text(json.dumps(result, indent=2) + '\n')
            if not row['http_valid']:
                raise RuntimeError('HTTP observer failed; retain this trial as incomplete')
            print(json.dumps({'trial': trial + 1, 'acceptance_ms': row['acceptance_ms'], 'completion_ms': row['completion_ms'],
                              'http_samples': len(observations), 'max_step_us': operation['max_step_us'] if operation else None}), flush=True)
        if rows[-1]['operation']:
            for trial in range(10):
                client.activate(names[0], output='prep')
                generation = client.program_state(output='prep')['generation']
                client.require_ok(client.command(MANAGE, json.dumps({'op': 'program_activate', 'name': names[1], 'output': 'prep', 'action': 'play'}).encode()))
                operation = json.loads(client.data_reply)['operation']
                while operation['stage'] not in ('initialise', 'first_render') and not operation['terminal']:
                    operation = client.operation(operation['boot_id'], operation['id'])
                started = time.monotonic()
                cancelled = client.operation(operation['boot_id'], operation['id'], cancel=True)
                requested = time.monotonic()
                # Cancellation is accepted synchronously, then cleanup runs at
                # the next safe preparation boundary. Observe its terminal state.
                while not cancelled['terminal'] and time.monotonic() - started < 5:
                    cancelled = client.operation(operation['boot_id'], operation['id'])
                elapsed = (time.monotonic() - started) * 1000
                after = client.program_state(output='prep')
                if not cancelled['terminal'] or cancelled['stage'] != 'cancelled' or after['generation'] != generation:
                    result['cancellation_failure'] = {'operation': cancelled, 'before_generation': generation, 'after': after}
                    args.result.write_text(json.dumps(result, indent=2) + '\n')
                    raise RuntimeError('cancellation did not terminate or changed old scene')
                result['cancellations'].append({'trial': trial + 1, 'acceptance_ms': (requested - started) * 1000,
                                                'latency_ms': elapsed, 'operation': cancelled, 'old_generation_preserved': True})
                args.result.write_text(json.dumps(result, indent=2) + '\n')
        result['description_after'] = client.describe()
        result['complete'] = True
        args.result.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        client.configure(original)
        for name in created:
            client.delete_program(name)
        client.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--url', required=True)
    parser.add_argument('--firmware', required=True, type=Path)
    parser.add_argument('--packages', required=True, type=Path)
    parser.add_argument('--label', required=True)
    parser.add_argument('--result', required=True, type=Path)
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--reuse-packages', action='store_true')
    args = parser.parse_args()
    if args.samples < 1:
        parser.error('at least one sample required')
    run(args)
