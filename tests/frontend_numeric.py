"""Compile lowered frontend source and execute raw numeric state in the native VM."""
import json
import base64
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from dataclasses import asdict

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source


def check(request):
    compilation = compile_source(request['source'], target=request.get('target'),
                                 optimize=request.get('optimize', True))
    result = {'diagnostics': [asdict(d) for d in compilation.diagnostics],
              'advisories': [asdict(d) for d in compilation.advisories],
              'manifest': compilation.manifest}
    if compilation.success and request.get('package'):
        result['package'] = base64.b64encode(compilation.bytecode).decode()
    if compilation.success and request.get('execute', True):
        with tempfile.NamedTemporaryFile(suffix='.lbc') as file:
            file.write(compilation.bytecode)
            file.flush()
            run = subprocess.run([os.environ['LIGHTVM_NUMERIC_RUNTIME'], '--numeric-package',
                                  file.name, str(request.get('count', 33)),
                                  str(request.get('tick', 0)), str(request.get('workers', 1))],
                                 capture_output=True, text=True, check=True)
        assert not run.stderr, run.stderr
        result['runtime'] = json.loads(run.stdout)
    return result


if __name__ == '__main__':
    request = json.load(sys.stdin)
    print(json.dumps([check(r) for r in request] if isinstance(request, list) else check(request)))
