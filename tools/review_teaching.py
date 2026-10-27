#!/usr/bin/env python3
"""Compile teaching probes and record host semantics/work, never inferred FPS."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from dataclasses import asdict

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source


def check(cases):
    driver = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT / 'build/teaching-host/test_platform')).resolve()
    if not driver.is_file():
        raise SystemExit('Build test_platform and set LIGHTVM_NUMERIC_RUNTIME to its path.')
    report = {'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
              'driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
              'conditions': {'counts': [25, 50, 100], 'workers': 1, 'tick': 0,
                             'render_time': 1.25, 'render_delta': 0.25,
                             'invocation_work_budget': 100000, 'frames': 1,
                             'note': 'Native test harness with deterministic seed; no hardware FPS measurement. Init also receives time=1.25/delta=0.25 in this harness, unlike production initialization at zero.'},
              'cases': []}
    with tempfile.TemporaryDirectory() as temporary:
        for case in cases:
            row = dict(case)
            report['cases'].append(row)
            if 'frontend_error' in row:
                continue
            row['variants'] = []
            for optimize in (False, True):
                result = compile_source(case['lowered'], case['id'], optimize=optimize)
                variant = {'optimize': optimize, 'diagnostics': [asdict(d) for d in result.diagnostics]}
                row['variants'].append(variant)
                if not result.success:
                    continue
                header = struct.unpack('<4s23I', result.bytecode[:96])
                nregs, ncode = header[2:4]
                offset = 96 + nregs * 8
                code = [struct.unpack_from('<BBBBI', result.bytecode, offset + i * 8) for i in range(ncode)]
                variant.update(bytes=len(result.bytecode), registers=nregs, instructions=ncode,
                               maps=sum(i[0] == 39 for i in code), requires=result.manifest['requires'],
                               advisories=[d.message for d in result.advisories], runs=[])
                path = Path(temporary) / 'probe.lbc'
                path.write_bytes(result.bytecode)
                for count in report['conditions']['counts']:
                    run = subprocess.run([str(driver), '--numeric-package', str(path), str(count), '0', '1', '100000'],
                                         text=True, capture_output=True, timeout=15, check=True)
                    if run.stderr:
                        raise RuntimeError(run.stderr)
                    data = json.loads(run.stdout)
                    if 'pixels' in data:
                        data['pixels_sha256'] = hashlib.sha256(bytes.fromhex(data['pixels'])).hexdigest()
                        data['first_pixel_rgba'] = data['pixels'][:8]
                        del data['pixels']
                    if 'images' in data:
                        data['images_sha256'] = [hashlib.sha256(bytes.fromhex(image)).hexdigest() for image in data.pop('images')]
                    variant['runs'].append({'count': count, **data})
            variants = row['variants']
            if all('runs' in v for v in variants):
                def semantic(run):
                    return {k: v for k, v in run.items() if k not in ('plan_bytes', 'dispatches', 'work')}
                row['scalar_optimized_equal'] = [semantic(r) for r in variants[0]['runs']] == [semantic(r) for r in variants[1]['runs']]
    return report


if __name__ == '__main__':
    result = check(json.load(sys.stdin))
    destination = Path(sys.argv[1])
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(result, indent=2) + '\n')
    for case in result['cases']:
        if 'frontend_error' in case:
            print(case['id'], 'FRONTEND:', case['frontend_error'])
            continue
        optimized = case['variants'][1]
        if optimized['diagnostics']:
            print(case['id'], 'COMPILE:', optimized['diagnostics'][0]['message'])
        else:
            run = optimized['runs'][-1]
            print(case['id'], json.dumps({k: optimized[k] for k in ('maps', 'instructions', 'bytes')}),
                  json.dumps({k: run.get(k) for k in ('load', 'status', 'work', 'dispatches', 'first_pixel_rgba')}),
                  'equivalent=' + str(case['scalar_optimized_equal']))
