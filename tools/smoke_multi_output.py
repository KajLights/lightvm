#!/usr/bin/env python3
"""Run five physical GPIO outputs alongside Rainbow, then leave all six running."""
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

EFFECTS = ('color', 'pattern', 'rainbow_sprite', 'sweep', 'stars')


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(client, reference, count, seconds, output):
    original = client.get_config()
    saved = client.get_config(saved=True)
    library = client.programs()
    info = client.describe()
    check(len(original['outputs']) == 1, 'test requires the original Rainbow output')
    primary = original['outputs'][0]['id']
    check(original['outputs'][0]['config']['pin'] == 'GP16', 'Rainbow must use GP16')
    check(original['outputs'][0]['config']['count'] == count, 'Rainbow pixel count must match')
    check(client.status_fields(output=primary)['mode'] == 'bytecode', 'Rainbow must already be running')
    check(info['slots'] == ['A', 'B'] and info['slot_binding'] == 'output', 'named-output firmware required')
    config = copy.deepcopy(original)
    for pin, effect in enumerate(EFFECTS, 17):
        config['outputs'].append({'id': effect, 'type': 'ws2812b', 'config': {
            'pin': f'GP{pin}', 'count': count, 'order': 'GRB', 'brightness_limit': 100}})
    reference.configure({'schema': 'lightvm.config', 'outputs': [
        {'id': 'reference', 'type': 'ws2812b', 'config': {'pin': 'GP0', 'count': count}}]})
    outputs = [primary, *EFFECTS]
    report = {'controller_id': info['controller_id'], 'board': info['board'],
              'original_configuration': original, 'test_configuration': config,
              'output_type': 'ws2812b', 'seconds': seconds, 'parity': {}, 'samples': [], 'passed': False}

    def save():
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + '\n')

    try:
        baseline = client.program_state(output=primary)
        client.configure(config)
        check(client.program_state(output=primary)['generation'] == baseline['generation'],
              'adding outputs restarted Rainbow')
        for effect in EFFECTS:
            source = ROOT / 'examples/animations' / (effect + '.py')
            artifact = compile_source(source.read_text().replace("random.seed()", "random.seed(1)"), str(source), info)
            check(artifact.success, str(artifact.diagnostics))
            client.upload(artifact.bytecode, output=effect)
            reference.upload(artifact.bytecode)
            parity = []
            for delta in (0, 16, 17, 1000):
                payload = struct.pack('<I', delta)
                client.require_ok(client.command(FRAME, payload, output=effect))
                reference.require_ok(reference.command(FRAME, payload))
                actual, expected = bytes(client.pixel_snapshot), bytes(reference.pixel_snapshot)
                check(actual == expected, f'{effect}: output differs from host at {delta} ms')
                check(len(actual) == count * 3, f'{effect}: wrong strip size')
                parity.append({'delta_ms': delta, 'identical': True, 'sha256': hashlib.sha256(actual).hexdigest()})
            report['parity'][effect] = parity
            client.reset_program(output=effect)
            client.require_ok(client.command(PLAY, output=effect))
            print(f'RUNNING {effect}: slot A, {count} physical pixels; Rainbow continues', flush=True)
            save()
        current = client.program_state(output=primary)
        check(current['generation'] == baseline['generation'] and current['output_frame'] > baseline['output_frame'],
              'Rainbow did not continue during configuration and uploads')
        initial = {output: client.program_state(output=output) for output in outputs}
        initial_status = client.status_fields()
        report['initial'] = initial
        report['initial_status'] = initial_status
        started = time.monotonic()
        previous = initial
        milestone = 0
        while milestone < seconds:
            milestone = min(seconds, milestone + 5)
            time.sleep(max(0, started + milestone - time.monotonic()))
            states = {output: client.program_state(output=output) for output in outputs}
            status = client.status_fields()
            native = client.describe()['outputs']
            report['samples'].append({'elapsed_seconds': round(time.monotonic()-started, 3),
                                      'status': status, 'outputs': native, 'states': states})
            for output, state in states.items():
                check(state['output'] == output and state['slot'] == 'A', f'{output}: incorrect ownership')
                check(not state['faulted'] and state['output_valid'] and not state['paused'],
                      f'{output}: fault, invalid output or stopped playback')
                check(state['generation'] == initial[output]['generation'], f'{output}: unexpectedly restarted')
                check(state['output_frame'] > previous[output]['output_frame'], f'{output}: stopped advancing')
            check(status['heap'] == initial_status['heap'] and status['heap_peak'] == initial_status['heap_peak'],
                  'playback allocated memory')
            check(all(not output['animation_error'] and output['playing'] for output in native), str(native))
            previous = states
            print(f'{milestone:g}s: heap={status["heap"]}; ' + ', '.join(
                f'{output}={state["output_frame"]} frames' for output, state in states.items()), flush=True)
            save()
        elapsed = time.monotonic() - started
        report['average_fps'] = {output: round((previous[output]['output_frame']-state['output_frame'])/elapsed, 2)
                                 for output, state in initial.items()}
        check(previous['sweep']['render_count'] > initial['sweep']['render_count'] + 1, 'Sweep did not continue rendering')
        client.pause_program(output='pattern')
        paused = client.program_state(output='pattern')['program_us']
        rainbow_frame = client.program_state(output=primary)['output_frame']
        time.sleep(.2)
        check(client.program_state(output='pattern')['program_us'] == paused, 'Pattern pause did not hold')
        check(client.program_state(output=primary)['output_frame'] > rainbow_frame, 'pausing Pattern blocked Rainbow')
        client.resume_program(output='pattern')
        report['independent_pause_resume'] = True
        check(client.get_config(saved=True) == saved and client.programs() == library,
              'persistent configuration or library changed')
        report['passed'] = True
        print('PASS: six physical GPIO outputs, 200 LEDs each; stable heap, exact host parity, continuous playback.', flush=True)
    except BaseException as error:
        report['error'] = str(error) or type(error).__name__
        raise
    finally:
        report['final_description'] = client.describe()
        report['left_running'] = [d['id'] for d in report['final_description']['outputs'] if d['playing']]
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
    parser.add_argument('--output', type=Path, default=ROOT/'build/multi-output/results.json')
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
