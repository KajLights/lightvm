#!/usr/bin/env python3
"""Run five physical GPIO devices alongside Rainbow, then leave all six running."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, SerialTransport, FRAME, PLAY

EFFECTS = ('color', 'pattern', 'chase', 'wipe', 'twinkle')


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(client, reference, count, seconds, output):
    original = client.get_config()
    saved = client.get_config(saved=True)
    library = client.procedures()
    info = client.describe()
    check(len(original['devices']) == 1, 'test requires the original Rainbow device')
    primary = original['devices'][0]['id']
    check(original['devices'][0]['config']['pin'] == 'GP16', 'Rainbow must use GP16')
    check(original['devices'][0]['config']['count'] == count, 'Rainbow pixel count must match')
    check(client.status_fields(device=primary)['mode'] == 'bytecode', 'Rainbow must already be running')
    check(info['slots'] == ['A'] and info['slot_binding'] == 'device', 'named-device firmware required')
    config = copy.deepcopy(original)
    for pin, effect in enumerate(EFFECTS, 17):
        config['devices'].append({'id': effect, 'type': 'ws2812b', 'config': {
            'pin': f'GP{pin}', 'count': count, 'order': 'GRB', 'brightness_limit': 100}})
    reference.configure({'schema': 'lightvm.config/1', 'devices': [
        {'id': 'reference', 'type': 'ws2812b', 'config': {'pin': 'GP0', 'count': count}}]})
    devices = [primary, *EFFECTS]
    report = {'device_id': info['device_id'], 'board': info['board'],
              'original_configuration': original, 'test_configuration': config,
              'output_type': 'ws2812b', 'seconds': seconds, 'parity': {}, 'samples': [], 'passed': False}

    def save():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')

    try:
        baseline = client.program_parameters(device=primary)
        client.configure(config)
        check(client.program_parameters(device=primary)['generation'] == baseline['generation'],
              'adding devices restarted Rainbow')
        for effect in EFFECTS:
            source = ROOT / 'samples' / (effect + '.py')
            artifact = compile_source(source.read_text().replace("random.seed()", "random.seed(1)"), str(source), info)
            check(artifact.success, str(artifact.diagnostics))
            client.upload(artifact.bytecode, device=effect)
            reference.upload(artifact.bytecode)
            parity = []
            for delta in (0, 16, 17, 1000):
                payload = struct.pack('<I', delta)
                client.require_ok(client.command(FRAME, payload, device=effect))
                reference.require_ok(reference.command(FRAME, payload))
                actual, expected = bytes(client.pixel_snapshot), bytes(reference.pixel_snapshot)
                check(actual == expected, f'{effect}: output differs from host at {delta} ms')
                check(len(actual) == count * 3, f'{effect}: wrong strip size')
                parity.append({'delta_ms': delta, 'identical': True, 'sha256': hashlib.sha256(actual).hexdigest()})
            report['parity'][effect] = parity
            client.reset_program(device=effect)
            client.require_ok(client.command(PLAY, device=effect))
            print(f'RUNNING {effect}: slot A, {count} physical pixels; Rainbow continues', flush=True)
            save()
        current = client.program_parameters(device=primary)
        check(current['generation'] == baseline['generation'] and current['output_frame'] > baseline['output_frame'],
              'Rainbow did not continue during configuration and uploads')
        initial = {device: client.program_parameters(device=device) for device in devices}
        initial_status = client.status_fields()
        report['initial'] = initial
        report['initial_status'] = initial_status
        started = time.monotonic()
        previous = initial
        milestone = 0
        while milestone < seconds:
            milestone = min(seconds, milestone + 5)
            time.sleep(max(0, started + milestone - time.monotonic()))
            states = {device: client.program_parameters(device=device) for device in devices}
            status = client.status_fields()
            native = client.describe()['devices']
            report['samples'].append({'elapsed_seconds': round(time.monotonic()-started, 3),
                                      'status': status, 'devices': native, 'states': states})
            for device, state in states.items():
                check(state['device'] == device and state['slot'] == 'A', f'{device}: incorrect ownership')
                check(not state['faulted'] and state['output_valid'] and not state['paused'],
                      f'{device}: fault, invalid output or stopped playback')
                check(state['generation'] == initial[device]['generation'], f'{device}: unexpectedly restarted')
                check(state['output_frame'] > previous[device]['output_frame'], f'{device}: stopped advancing')
            check(status['heap'] == initial_status['heap'] and status['heap_peak'] == initial_status['heap_peak'],
                  'playback allocated memory')
            check(all(not device['animation_error'] and device['playing'] for device in native), str(native))
            previous = states
            print(f'{milestone:g}s: heap={status["heap"]}; ' + ', '.join(
                f'{device}={state["output_frame"]} frames' for device, state in states.items()), flush=True)
            save()
        elapsed = time.monotonic() - started
        report['average_fps'] = {device: round((previous[device]['output_frame']-state['output_frame'])/elapsed, 2)
                                 for device, state in initial.items()}
        check(previous['wipe']['render_count'] > initial['wipe']['render_count'] + 1, 'Wipe did not continue rendering')
        client.pause_program(device='pattern')
        paused = client.program_parameters(device='pattern')['program_us']
        rainbow_frame = client.program_parameters(device=primary)['output_frame']
        time.sleep(.2)
        check(client.program_parameters(device='pattern')['program_us'] == paused, 'Pattern pause did not hold')
        check(client.program_parameters(device=primary)['output_frame'] > rainbow_frame, 'pausing Pattern blocked Rainbow')
        client.resume_program(device='pattern')
        report['independent_pause_resume'] = True
        check(client.get_config(saved=True) == saved and client.procedures() == library,
              'persistent configuration or library changed')
        report['passed'] = True
        print('PASS: six physical GPIO outputs, 200 LEDs each; stable heap, exact host parity, continuous playback.', flush=True)
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        raise
    finally:
        report['final_description'] = client.describe()
        report['left_running'] = [d['id'] for d in report['final_description']['devices'] if d['playing']]
        save()
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--port')
    target.add_argument('--process', type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--pixels', type=int, default=200)
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--output', type=Path, default=ROOT/'build/multi-device/results.json')
    args = parser.parse_args()
    if args.pixels < 1 or args.seconds < 10:
        parser.error('pixels must be positive and the test must run at least ten seconds')
    client = Client(SerialTransport(args.port) if args.port else ProcessTransport(args.process), timeout=15)
    reference = Client(ProcessTransport(args.reference), timeout=15)
    try:
        run(client, reference, args.pixels, args.seconds, args.output)
    finally:
        reference.close()
        client.close()


if __name__ == '__main__':
    main()
