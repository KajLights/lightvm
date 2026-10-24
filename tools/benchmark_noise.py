#!/usr/bin/env python3
"""Freeze noise packages and measure rendering and live INPUTS on a real engine.

Run --prepare with --process pointing at a verified host to freeze expected RGB
hashes. Hardware uses the ESP32 campaign's board_monitor on the same connection.
Frame readback and firmware output-completion stamps are not optical timings.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.portable import PortableCompiler
from client import Client, ProcessTransport, SerialTransport, STOP, FRAME
from benchmark_controller import DEFAULT_PINS
from benchmark_saved_preset import sample, percentile

CASES = ('perlin2', 'perlin3', 'simplex2', 'simplex3')
EMPTY = {'schema': 'lightvm.config', 'outputs': []}
STATES = [
    {'scale': 4.0, 'phase': .125, 'field': 0},
    {'scale': 4.0, 'phase': .375, 'field': 0},
    {'scale': 9.25, 'phase': .375, 'field': 0},
    {'scale': 9.25, 'phase': .375, 'field': 1},
    {'scale': 9.25, 'phase': .375, 'field': 7},
    {'scale': 9.25, 'phase': .375, 'field': 7},  # repeated latched trigger count
    {'scale': 9.25, 'phase': .375, 'field': -2147483648},
    {'scale': 4.0, 'phase': .125, 'field': 0},
]
SIGNALS = {'noise.scale': 'float', 'noise.phase': 'float', 'noise.field': 'int'}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def source(name):
    coordinates = 'i / pixels.count * scale, phase * 4.0'
    if name.endswith('3'):
        coordinates += ', 0.625'
    return f'''from lightvm import profile, Input, Ramp, render, rgb, {name}
profile("animation")
SCALE = Input("scale", default=4.0, minimum=0.1, maximum=32.0)
PHASE = Input("phase", default=0.0, minimum=0.0, maximum=1.0,
              driver=Ramp(duration=30.0, mode="pingpong"))
FIELD = Input("field", default=0)
@render
def draw(ctx, runtime, pixels):
    scale = runtime.input(SCALE)
    phase = runtime.input(PHASE)
    field = runtime.input(FIELD)
    for i in range(pixels.count):
        n = {name}({coordinates}, field)
        value = int(127.0 + 127.0 * n)
        pixels[i] = rgb(value, value, value)
'''


def configure(client, name, count, pin):
    client.configure(EMPTY)
    client.configure({'schema': 'lightvm.config', 'outputs': [{
        'id': 'noise', 'type': 'ws2812b', 'config': {
            'pin': pin, 'count': count, 'order': 'GRB', 'brightness_limit': 32}}]})
    client.configure_composition([{'id': 'field', 'program': 'nb_' + name}], output='noise')


def prepare(args):
    args.packages.mkdir(parents=True, exist_ok=False)
    manifest = {}
    for case in CASES:
        text = source(case)
        (args.packages / (case + '.py')).write_text(text)
        for variant, optimize in (('scalar', False), ('bulk', True)):
            compiler = PortableCompiler(text, optimize=optimize)
            data, metadata = compiler.compile()
            maps = sum(instruction[0] == 39 for instruction in compiler.code)
            if maps != int(optimize):
                raise RuntimeError((case, variant, maps, compiler.map_reports))
            name = case + '-' + variant
            (args.packages / (name + '.lbc')).write_bytes(data)
            manifest[name] = {'source_sha256': sha(text.encode()), 'bytecode_sha256': sha(data),
                              'bytes': len(data), 'maps': maps, 'requires': metadata['requires'],
                              'noise_calls_per_pixel': 1}
    (args.packages / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    client = Client(ProcessTransport(args.process), timeout=30)
    reference = {'host_sha256': sha(args.process.read_bytes()), 'states': STATES,
                 'packages': manifest, 'frames': {}}
    try:
        for name in manifest:
            client.save_program('nb_' + name, (args.packages / (name + '.lbc')).read_bytes())
            for count in args.pixels:
                configure(client, name, count, 'GP16')
                client.require_ok(client.command(STOP, output='noise'))
                client.reset_program(output='noise', layer='field')
                frames = []
                for values in STATES:
                    client.set_inputs({key: {'value': value} for key, value in values.items()},
                                      output='noise', layer='field')
                    client.require_ok(client.command(FRAME, struct.pack('<I', 0), output='noise'))
                    frames.append(sha(client.composition_snapshot(output='noise')[1]))
                reference['frames'][f'{name}/{count}'] = frames
            client.configure(EMPTY)
            client.delete_program('nb_' + name)
        for case in CASES:
            for count in args.pixels:
                a = reference['frames'][f'{case}-scalar/{count}']
                b = reference['frames'][f'{case}-bulk/{count}']
                if a != b or a[4] != a[5] or a[0] != a[7] or len(set(a[:5])) != 5:
                    raise RuntimeError('host reference disagrees or fails to exercise inputs: ' + case)
        (args.packages / 'reference.json').write_text(json.dumps(reference, indent=2) + '\n')
    finally:
        client.close()
    print(json.dumps({'packages': len(manifest), 'reference_frames': len(manifest) * len(args.pixels) * len(STATES)}))


def summary(values):
    return {'count': len(values), 'median': statistics.median(values) if values else None,
            'p95': percentile(values, .95) if values else None,
            'max': max(values) if values else None}


def layer(client):
    return client.composition(output='noise')['layers'][0]


def measure_live(client, publisher, sequence, seconds, input_hz, poll_hz, check):
    before = client.transition(output='noise')
    started = time.monotonic()
    deadline, next_input, next_status = started + seconds, started, started
    events, statuses = [], []
    while time.monotonic() < deadline:
        now = time.monotonic()
        if now >= next_input:
            index = len(events)
            values = {'scale': 4.0 + (index % 11) * .5,
                      'phase': (index % 97) / 96.0, 'field': index // 5}
            tick = time.monotonic()
            sequence += 1
            client.publish_inputs(publisher, sequence, {'noise.' + k: v for k, v in values.items()})
            events.append({'elapsed_s': tick - started, 'values': values,
                           'acceptance_ms': (time.monotonic() - tick) * 1000})
            # Never conceal missed requested cadence by bursting old values.
            next_input = max(next_input + 1 / input_hz, time.monotonic())
        if time.monotonic() >= next_status:
            tick = time.monotonic()
            status = client.status_fields(output='noise')
            if status.get('animation_error') != '-':
                raise RuntimeError(status)
            statuses.append({'elapsed_s': tick - started, 'latency_ms': (time.monotonic() - tick) * 1000,
                             'status': status})
            next_status = max(next_status + 1 / poll_hz, time.monotonic())
        check()
        time.sleep(min(.005, max(0, min(next_input, next_status, deadline) - time.monotonic())))
    after = client.transition(output='noise')
    elapsed = time.monotonic() - started
    old, new = before['A']['layers'][0], after['A']['layers'][0]
    if old['generation'] != new['generation']:
        raise RuntimeError('live inputs replaced the animation instance')
    frames = new['frames'] - old['frames']
    if not frames:
        raise RuntimeError('live inputs produced no completed renders')
    return sequence, {'seconds': elapsed, 'events': events, 'statuses': statuses,
        'before': before, 'after': after, 'updates_per_second': frames / elapsed,
        'actual_publish_hz': len(events) / elapsed, 'requested_publish_hz': input_hz,
        'mean_render_us': (new['render_total_us'] - old['render_total_us']) / frames,
        'acceptance_ms': summary([event['acceptance_ms'] for event in events]),
        'status_latency_ms': summary([row['latency_ms'] for row in statuses])}


def run(args):
    manifest = json.loads((args.packages / 'manifest.json').read_text())
    reference = json.loads((args.packages / 'reference.json').read_text())
    if reference['packages'] != manifest or reference['states'] != STATES:
        raise ValueError('reference/package contract changed')
    cases = args.case or CASES
    names = [case + '-' + variant for case in cases for variant in ('scalar', 'bulk')]
    packages = {name: (args.packages / (name + '.lbc')).read_bytes() for name in names}
    for name, data in packages.items():
        if sha(data) != manifest[name]['bytecode_sha256']:
            raise ValueError('package hash changed: ' + name)
        for count in args.pixels:
            if f'{name}/{count}' not in reference['frames']:
                raise ValueError('pixel count missing from frozen reference')
    if args.port:
        # Supplied by the ESP32 board optimisation branch; merge it before hardware runs.
        from board_monitor import BoardMonitor
    client = Client(SerialTransport(args.port, no_reset=True) if args.port else ProcessTransport(args.process), timeout=30)
    monitor = None
    if args.port:
        monitor = BoardMonitor(client, args.temperature_log, args.temperature_limit)
    check = monitor.check if monitor else lambda: None
    sleep = monitor.sleep if monitor else time.sleep
    result = {'schema': 'lightvm.noise-benchmark.v1', 'started_utc': datetime.now(timezone.utc).isoformat(),
        'label': args.label, 'board': args.board, 'pin': args.pin or DEFAULT_PINS[args.board],
        'firmware_sha256': sha(args.firmware.read_bytes()), 'packages': manifest,
        'reference_sha256': sha((args.packages / 'reference.json').read_bytes()),
        'network_regime': args.network_regime, 'warmup_seconds': args.warmup,
        'temperature_log': str(args.temperature_log) if args.temperature_log else None,
        'temperature_limit_c': args.temperature_limit if args.port else None,
        'poll_hz': args.poll_hz, 'input_hz': args.input_hz,
        'scope': 'one noise sample/pixel plus VM and RGB work; firmware output and host RGB readback, no optical measurement',
        'timing': [], 'live': [], 'replays': [], 'passed': False}
    created = []

    def checkpoint():
        args.result.write_text(json.dumps(result, indent=2) + '\n')

    try:
        result['description_before'] = client.describe()
        if result['description_before']['board'] != args.board:
            raise ValueError('unexpected board')
        original = client.get_config()
        existing = {row['name'] for row in client.programs()}
        if existing.intersection('nb_' + name for name in names):
            raise ValueError('temporary noise program names already exist')
        for name, data in packages.items():
            client.save_program('nb_' + name, data)
            created.append('nb_' + name)
            if client.get_program('nb_' + name) != data:
                raise ValueError('package readback differs')
        publisher = client.claim_inputs(SIGNALS)
        sequence = 0
        for count in args.pixels:
            for trial in range(args.samples):
                for name in names if trial % 2 == 0 else reversed(names):
                    configure(client, name, count, result['pin'])
                    sleep(args.warmup)
                    tags = {'program': name, 'pixels': count, 'trial': trial + 1}
                    if args.lane in ('all', 'timing'):
                        row = sample(client, 'noise', args.seconds, poll_hz=args.poll_hz)
                        if not row['layers']['A/field']['updates']:
                            raise RuntimeError('phase Ramp did not produce completed renders')
                        result['timing'].append({**tags, **row})
                        checkpoint()
                    client.set_inputs({key: {'source': 'noise.' + key} for key in STATES[0]},
                                      output='noise', layer='field')
                    generation = layer(client)['generation']
                    if args.lane in ('all', 'live'):
                        sequence, row = measure_live(client, publisher, sequence, args.seconds,
                                                     args.input_hz, args.poll_hz, check)
                        result['live'].append({**tags, **row})
                        checkpoint()
                    if args.lane in ('all', 'live', 'correctness'):
                        for index in range(args.events):
                            state_index = index % len(STATES)
                            values = STATES[state_index]
                            previous = layer(client)
                            started = time.monotonic()
                            sequence += 1
                            client.publish_inputs(publisher, sequence, {'noise.' + k: v for k, v in values.items()})
                            accepted = time.monotonic()
                            expected = reference['frames'][f'{name}/{count}'][state_index]
                            seen = None
                            while time.monotonic() - started < 10:
                                actual = sha(client.composition_snapshot(output='noise')[1])
                                if actual == expected:
                                    seen = time.monotonic()
                                    break
                            if seen is None:
                                raise RuntimeError(f'input RGB mismatch: {name}/{count}, state {state_index}: {actual} != {expected}')
                            completed = layer(client)
                            while completed.get('dirty_age_us') is not None and time.monotonic() - started < 10:
                                completed = layer(client)
                            if (completed['generation'] != generation or completed.get('dirty_age_us') is not None
                                    or completed.get('dependency_current') is False):
                                raise RuntimeError('input changed generation or never reached firmware output')
                            result['replays'].append({**tags, 'index': index, 'values': values,
                                'sha256': actual, 'equal': True, 'acceptance_ms': (accepted - started) * 1000,
                                'composite_readback_ms': (seen - started) * 1000,
                                'dependency_changed': completed['dependency_revision'] != previous['dependency_revision'],
                                'previous': previous, 'completed': completed})
                            checkpoint()
                    print(json.dumps({**tags, 'complete': True}), flush=True)
        result['description_after'] = client.describe()
        if result['description_after']['boot_id'] != result['description_before']['boot_id']:
            raise RuntimeError('board rebooted during noise tests')
        result['passed'] = True
    except BaseException as error:
        result['error'] = str(error)
        raise
    finally:
        try:
            if monitor and monitor.failure:
                result['cleanup'] = 'thermal monitor stopped outputs; original playback not restored'
            else:
                if 'original' in locals():
                    client.configure(EMPTY)
                    client.configure(original)
                for name in created:
                    client.delete_program(name)
                result['cleanup'] = 'configuration restored and temporary programs removed'
        except BaseException as error:
            result['cleanup_error'] = str(error)
            result['passed'] = False
            raise
        finally:
            client.close()
            checkpoint()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', action='store_true')
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--process', type=Path)
    parser.add_argument('--port')
    parser.add_argument('--board', choices=DEFAULT_PINS, default='simulator')
    parser.add_argument('--pin')
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--label')
    parser.add_argument('--result', type=Path)
    parser.add_argument('--network-regime', default='unprovisioned; USB only')
    parser.add_argument('--temperature-log', type=Path)
    parser.add_argument('--temperature-limit', type=float, default=80)
    parser.add_argument('--case', action='append', choices=CASES)
    parser.add_argument('--lane', choices=('all', 'timing', 'live', 'correctness'), default='all')
    parser.add_argument('--pixels', type=int, nargs='+', default=[200, 500, 1024])
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--warmup', type=float, default=2)
    parser.add_argument('--events', type=int, default=8)
    parser.add_argument('--input-hz', type=float, default=10)
    parser.add_argument('--poll-hz', type=float, default=20)
    args = parser.parse_args()
    if any(n < 1 or n > 1024 for n in args.pixels) or args.samples < 1 or args.seconds <= 0 or args.warmup < 0 or args.events < 8 or args.input_hz <= 0 or args.poll_hz <= 0:
        parser.error('invalid count, duration or polling frequency')
    if args.prepare:
        if not args.process or args.port:
            parser.error('--prepare requires a host --process')
        prepare(args)
    else:
        if bool(args.port) == bool(args.process) or not all((args.firmware, args.label, args.result)):
            parser.error('supply one transport, firmware, label and result')
        if args.result.exists():
            parser.error('keep earlier evidence; choose a new result path')
        if args.port and (not args.temperature_log or not args.board.startswith('xiao_esp32')):
            parser.error('hardware runs require an ESP32 board identity and --temperature-log')
        run(args)


if __name__ == '__main__':
    main()
