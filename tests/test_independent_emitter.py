"""Public wire contract fixture: no Python compiler is involved in this test."""
import ast
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tests/fixtures'))
import independent_emitter as emitter


class IndependentEmitterTests(unittest.TestCase):
    def test_emitter_imports_only_standard_library(self):
        tree = ast.parse(Path(emitter.__file__).read_text())
        imports = [a.name.split('.')[0] for n in ast.walk(tree) if isinstance(n, ast.Import) for a in n.names]
        imports += [n.module.split('.')[0] for n in ast.walk(tree) if isinstance(n, ast.ImportFrom)]
        self.assertTrue(set(imports) <= {'hashlib', 'json', 'pathlib', 'struct', 'sys'})

    def test_independent_rgb_boundaries(self):
        driver = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT/'build/host/test_platform'))
        if not driver.exists(): self.skipTest('native test port not built')
        for channel in range(3):
            for value in (-2147483648,-1,0,255,256,2147483647):
                for bulk in (False,True):
                    with self.subTest(channel=channel,value=value,bulk=bulk), tempfile.NamedTemporaryFile() as file:
                        file.write(emitter.emit(bulk=bulk,rgb=(channel,value)));file.flush()
                        run=subprocess.run([str(driver),'--numeric-package',file.name,'33','1000','2'],capture_output=True,text=True,check=True)
                        self.assertFalse(run.stderr);result=json.loads(run.stdout)
                        self.assertTrue(result['load'] and result['prepare'])
                        self.assertEqual(result['status'],1 if 0<=value<=255 else -1)
                        if 0<=value<=255:
                            expected=bytearray(emitter.expected_pixels(33));expected[channel::4]=bytes([value])*33
                            self.assertEqual(result['pixels'],expected.hex())
                        else:self.assertEqual(result['pixels'],'00'*(33*4))

    def test_independent_packages_and_rejections(self):
        driver = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT/'build/host/test_platform'))
        if not driver.exists(): self.skipTest('native test port not built')
        with tempfile.TemporaryDirectory() as directory:
            for name, package in emitter.corpus().items():
                path = Path(directory)/'fixture.lbc'; path.write_bytes(package)
                for count, workers, tick in ((1, 1, 0), (33, 2, 1000), (129, 1, 0), (129, 2, 0), (129, 1, 1000), (129, 2, 1000), (200, 2, 0)):
                    with self.subTest(name=name, count=count, workers=workers, tick=tick):
                        run = subprocess.run([str(driver), '--numeric-package', str(path), str(count), str(tick), str(workers)], capture_output=True, text=True, check=True)
                        self.assertFalse(run.stderr)
                        result = json.loads(run.stdout)
                        if name.startswith('reject-'):
                            self.assertFalse(result['load']); continue
                        self.assertEqual(result['status'], 1)
                        self.assertEqual(result['state'], [196608])
                        self.assertEqual(result['arrays'], [[98304]*count])
                        self.assertEqual(result['pixels'], emitter.expected_pixels(count).hex())
                        self.assertEqual(result['plan_bytes'] > 0, name.endswith('-bulk'))


if __name__ == '__main__': unittest.main()
