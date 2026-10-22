#!/usr/bin/env python3
"""Prepare reproducible packages and measure one verified Pico output.

Packages are compiled in a separate interpreter against the explicitly selected
compiler tree. Reuse the resulting directory unchanged across firmware runs.
Raw samples are checkpointed after each case; saved configuration is restored.
Only GP16 is driven. Larger logical pixel counts do not establish optical output.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from benchmark_multicore import SOURCES
from benchmark_saved_preset import sample
from replay_performance import fixed_variants
from client import Client, SerialTransport, ProcessTransport, FRAME, PLAY, STOP


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(destination, compiler):
    destination.mkdir(parents=True, exist_ok=True)
    sources = dict(SOURCES)
    sources['reactive'] = (ROOT / 'examples/animations/color.py').read_text()
    for population in (0, 1, 6, 12, 20):
        for variant, source in fixed_variants(population).items():
            sources[f'fixed{population}_{variant}'] = source
    fixtures = ROOT / 'tests/fixtures/performance'
    sources['animated_original'] = (fixtures / 'stars-original.py').read_text().replace('random.seed()', 'random.seed(7)')
    sources['animated_current'] = (ROOT / 'examples/animations/stars.py').read_text().replace('random.seed()', 'random.seed(7)')
    sources['heavy'] = """from lightvm import profile,init,render,rgb
profile('animation')
@init
def setup(ctx,runtime):
 ctx.value=0.0
 for i in range(20000):
  ctx.value=(ctx.value+0.03125)*0.9375
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(ctx.value*200),17,87))
"""
    spec = {name: {'source_sha256': sha(source.encode()), 'optimize': name not in ('scalar', 'heavy')}
            for name, source in sources.items()}
    for name, source in sources.items():
        (destination / (name + '.py')).write_text(source)
    (destination / 'spec.json').write_text(json.dumps(spec))
    # -c and cwd=/tmp prevent this checkout from shadowing the chosen compiler.
    code = """import sys,json,hashlib
from pathlib import Path
sys.path.insert(0,sys.argv[1])
from lightvm.compiler import compile_source
root=Path(sys.argv[2]);spec=json.loads((root/'spec.json').read_text())
for name,row in spec.items():
 result=compile_source((root/(name+'.py')).read_text(),optimize=row['optimize'])
 if not result.success:raise RuntimeError((name,result.diagnostics))
 data=result.bytecode;(root/(name+'.lbc')).write_bytes(data)
 row.update(bytecode_sha256=hashlib.sha256(data).hexdigest(),bytecode_bytes=len(data))
(root/'manifest.json').write_text(json.dumps(spec,indent=2)+'\\n')
"""
    subprocess.run([sys.executable, '-c', code, str(compiler.resolve()), str(destination.resolve())], cwd='/tmp', check=True)


def cases():
    rows = {f'fixed{n}': {'programs': ['solid', f'fixed{n}_original']} for n in (0, 1, 6, 12, 20)}
    rows.update({
        'geometry12': {'programs': ['solid', 'fixed12_geometry_at_spawn']},
        'style12': {'programs': ['solid', 'fixed12_style_at_spawn']},
        'timed': {'programs': ['solid', 'timed_sprites']},
        'animated_original': {'programs': ['solid', 'animated_original'], 'animated': True},
        'animated_current': {'programs': ['solid', 'animated_current'], 'animated': True},
        'map': {'programs': ['map']},
        'maps2': {'programs': ['map', 'map']},
        'images2': {'programs': ['draw', 'draw']},
        'scalars4': {'programs': ['scalar'] * 4},
        'asymmetric': {'programs': ['map', 'scalar']},
        'ab': {'programs': ['scalar', 'scalar'], 'ab': True},
        'layers8': {'programs': ['map'] * 8},
        'reactive': {'programs': ['reactive', 'timed_sprites']},
    })
    return rows


ANIMATED_SETTINGS = {'capacity': 20, 'duration': 2.0, 'probability': 5.0,
                     'color': 16777215, 'random_colors': False}


def release_test_scene(client):
    if any(o['id'] == 'bench' for o in client.describe()['outputs']):
        state = client.transition(output='bench')
        for slot in ('A', 'B'):
            if state[slot]['active']:
                client.clear_composition(output='bench', slot=slot)


def setup(client, case, pixels, clear_before_configure=False):
    if clear_before_configure:
        release_test_scene(client)
    client.configure({'schema': 'lightvm.config', 'outputs': []})
    client.configure({'schema': 'lightvm.config', 'outputs': [
        {'id': 'bench', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': pixels, 'brightness_limit': 32}}]})
    slots = ('A', 'B') if case.get('ab') else ('A',)
    for slot in slots:
        client.configure_composition([
            {'id': 'layer' + str(i), 'program': 'hw_' + name,
             **({'settings': ANIMATED_SETTINGS} if case.get('animated') and i == 1 else {})}
            for i, name in enumerate(case['programs'])], output='bench', slot=slot)
    if case.get('ab'):
        client.set_progress(37.5, output='bench')
    return slots


def replay(client, name, frames):
    setup(client, {'programs': [name]}, 200)
    client.require_ok(client.command(STOP, output='bench'))
    animated = name.startswith('animated_')
    client.reset_program(output='bench', layer='layer0', settings=ANIMATED_SETTINGS if animated else {})
    before = client.program_state(output='bench', layer='layer0')
    hashes = []
    aggregate = hashlib.sha256()
    for index in range(frames):
        if animated and index in (100, 200):
            client.set_inputs({'brightness': {'value': 0.4 if index == 100 else 1.0}}, output='bench', layer='layer0')
        client.require_ok(client.command(FRAME, struct.pack('<I', 16), output='bench'))
        image = bytes(client.pixel_snapshot)
        aggregate.update(image)
        hashes.append(sha(image))
    after = client.program_state(output='bench', layer='layer0')
    return {'name': name, 'frames': frames, 'sha256_all_frames': aggregate.hexdigest(), 'frame_sha256': hashes,
            'before': before, 'after': after}


def run(args):
    manifest = json.loads((args.packages / 'manifest.json').read_text())
    selected = args.case or ['fixed12', 'timed', 'images2', 'asymmetric', 'scalars4', 'ab']
    replay_names = ([f'fixed{n}_{v}' for n in (0, 1, 6, 12, 20)
                     for v in ('original', 'geometry_at_spawn', 'style_at_spawn')]
                    + ['animated_original', 'animated_current', 'timed_sprites']) if args.replay else []
    used = set(replay_names)
    if not args.replay:
        used.update(p for name in selected for p in cases()[name]['programs'])
    client = Client(SerialTransport(args.port) if args.port else ProcessTransport(args.process), timeout=120)
    created = []
    result = {'label': args.label, 'firmware_sha256': sha(args.firmware.read_bytes()),
              'packages': {name: manifest[name] for name in sorted(used)},
              'pixels': args.pixels, 'warmup_seconds': args.warmup,
              'explicit_release_before_configure': args.clear_before_configure,
              'network_regime': 'original Wi-Fi and authenticated MQTT connection; no intentional background commands',
              'samples': [], 'replays': []}
    try:
        original = client.get_config()
        result['description_before'] = client.describe()
        existing = {p['name'] for p in client.programs()}
        if existing.intersection('hw_' + p for p in used):
            raise ValueError('test program name already exists')
        for name in sorted(used):
            data = (args.packages / (name + '.lbc')).read_bytes()
            if sha(data) != manifest[name]['bytecode_sha256']:
                raise ValueError('package hash mismatch: ' + name)
            client.save_program('hw_' + name, data)
            created.append('hw_' + name)
        if args.replay:
            for name in replay_names:
                row = replay(client, name, 300 if name.startswith('animated_') else 650 if name == 'timed_sprites' else 10)
                result['replays'].append(row)
                args.result.write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps({'label': args.label, 'name': name, 'frames': row['frames'], 'sha256': row['sha256_all_frames']}), flush=True)
        else:
            for trial in range(args.samples):
                for name in selected if trial % 2 == 0 else reversed(selected):
                    case = cases()[name]
                    preparation_started = time.monotonic()
                    setup(client, case, args.pixels, args.clear_before_configure)
                    preparation_ms = (time.monotonic() - preparation_started) * 1000
                    time.sleep(args.warmup)
                    row = sample(client, 'bench', args.seconds, diagnostics=args.diagnostics, poll_hz=args.poll_hz)
                    row.update(name=name, trial=trial + 1, case=case, setup_ms=preparation_ms)
                    result['samples'].append(row)
                    args.result.write_text(json.dumps(result, indent=2) + '\n')
                    print(json.dumps({'label': args.label, 'name': name, 'trial': trial + 1,
                                      'layers': {k: v['updates_per_second'] for k, v in row['layers'].items()},
                                      'p95_ms': row['status_latency_ms']['p95']}), flush=True)
        result['description_after'] = client.describe()
        args.result.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        try:
            if 'original' in locals():
                if args.clear_before_configure:
                    release_test_scene(client)
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
    parser.add_argument('--compiler', type=Path, default=ROOT)
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--port')
    parser.add_argument('--process', type=Path)
    parser.add_argument('--firmware', type=Path)
    parser.add_argument('--label')
    parser.add_argument('--result', type=Path)
    parser.add_argument('--case', action='append', choices=list(cases()))
    parser.add_argument('--samples', type=int, default=5)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--warmup', type=float, default=7)
    parser.add_argument('--pixels', type=int, default=200)
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--poll-hz', type=float, default=20)
    parser.add_argument('--replay', action='store_true')
    parser.add_argument('--clear-before-configure', action='store_true',
                        help='explicitly release each test scene before replacement; records a distinct admission regime')
    args = parser.parse_args()
    if args.prepare:
        prepare(args.packages, args.compiler)
    else:
        if bool(args.port) == bool(args.process) or not all((args.result, args.firmware, args.label)):
            parser.error('supply a port or process, firmware identity, label and result path')
        if not 1 <= args.pixels <= 1024 or args.samples < 1 or args.seconds <= 0 or args.warmup < 0 or args.poll_hz < 0 or (args.diagnostics and not args.poll_hz):
            parser.error('invalid duration, sample count or pixel count')
        run(args)


if __name__ == '__main__':
    main()
