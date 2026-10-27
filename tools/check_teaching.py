#!/usr/bin/env python3
"""Check course source, scalar/bulk parity and ordinary learner changes."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lightvm.compiler import compile_source

cases = json.load(sys.stdin)
runtime = os.environ.get('LIGHTVM_NUMERIC_RUNTIME', 'build/teaching-host/test_platform')
counts = (1, 2, 12, 16, 24, 25, 31, 32, 33, 50, 63, 64, 65, 100)
failures, outputs, records, replay = [], {}, [], []
with tempfile.TemporaryDirectory() as temporary:
    for index, case in enumerate(cases):
        label = case['filename']
        if 'error' in case:
            if case['variant'] != 'bug': failures.append(label + ': ' + case['error'])
            records.append({**case, 'source': None, 'result': 'frontend error'})
            continue
        for optimize in (False, True):
            result = compile_source(case['source'], label, optimize=optimize)
            if not result.success:
                if case['variant'] != 'bug': failures.append(label + ': ' + str(result.diagnostics))
                elif int(case['lesson'][:2]) not in (2, 7, 9): failures.append(label + ': expected this mistake to compile and be visible while running')
                records.append({'file': label, 'optimize': optimize, 'diagnostics': [d.message for d in result.diagnostics]})
                continue
            if case['variant'] == 'bug' and int(case['lesson'][:2]) in (2, 7, 9):
                failures.append(label + ': expected this deliberate mistake to fail code checks')
            replay.append({'file': label, 'lesson': case['lesson'], 'variant': case['variant'], 'optimize': optimize, 'bytecode': list(result.bytecode), 'manifest': result.manifest})
            path = Path(temporary) / f'{index}.lbc'; path.write_bytes(result.bytecode)
            for count in counts:
                run = subprocess.run([runtime, '--numeric-package', str(path), str(count), '0', '1'], text=True, capture_output=True, timeout=10)
                if run.returncode:
                    failures.append(label + ': native driver failed: ' + run.stderr); continue
                value = json.loads(run.stdout)
                if not value.get('load') or case['variant'] != 'bug' and value.get('status') != 1:
                    failures.append(f'{label} ({count}, optimize={optimize}): {value}')
                key = (case['lesson'], case['variant'], count)
                measured = (value.get('status'), value.get('pixels'))
                if case['variant'] != 'bug' and key in outputs and outputs[key] != measured:
                    failures.append(f'{label} ({count}, optimize={optimize}): output differs across languages or compiler modes')
                outputs[key] = measured
                records.append({'file': label, 'count': count, 'optimize': optimize, 'status': value.get('status'), 'bytes': len(result.bytecode), 'plan_bytes': value.get('plan_bytes', 0)})
report = {'sources': len(cases), 'counts': counts, 'records': records, 'failures': failures}
if '--frames' in sys.argv:
    frames = json.loads(subprocess.check_output(['node', '--disable-warning=ExperimentalWarning', 'tools/check_teaching_frames.mjs'], input=json.dumps({'cases': replay, 'counts': counts}), text=True, timeout=240))
    report['frames'] = frames
    failures.extend(frames['failures'])
Path('build/teaching-acceptance.json').write_text(json.dumps(report, indent=2) + '\n')
print(f"Checked {len(cases)} course sources at {len(counts)} pixel counts in both compiler modes; {len(failures)} failures.")
if failures:
    print('\n'.join(failures[:40]))
    raise SystemExit(1)
