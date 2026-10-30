"""Semantic floors apply to reused encodings, declarations and typed callbacks."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source

PREFIX = '''import math
from lightvm import profile,init,render,requires,setting,rgb
profile("animation")
'''


class SemanticFloors(unittest.TestCase):
    def run_package(self, package):
        driver = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT/'build/host/test_platform'))
        if not driver.exists(): self.skipTest('native test port not built')
        with tempfile.NamedTemporaryFile() as file:
            file.write(package); file.flush()
            run = subprocess.run([str(driver), '--numeric-package', file.name], capture_output=True, text=True, check=True)
        self.assertFalse(run.stderr)
        return json.loads(run.stdout)

    def test_reused_operations_and_declarations(self):
        for expression in ('min(0.0,-0.0)', 'max(-0.0,0.0)', 'min(0,-0.0)', 'max(-0.0,0)', 'math.sin(-0.0)'):
            for declaration in (False, True):
                if declaration and expression.startswith('math.'): continue
                value = 'setting('+expression+')' if declaration else expression
                source = PREFIX+'@init\ndef setup(ctx,runtime): ctx.value='+value+'\n@render\ndef draw(ctx,runtime,pixels): pass\n'
                if declaration:
                    source = PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.parameter='+value+'\n ctx.value=0.0\n@render\ndef draw(ctx,runtime,pixels):ctx.value=ctx.parameter\n'
                for optimize in (False, True):
                    with self.subTest(expression=expression, declaration=declaration, optimize=optimize):
                        result = compile_source(source, optimize=optimize)
                        self.assertTrue(result.success, result.diagnostics)
                        self.assertEqual(result.manifest['requires'], '0.1.0')
                        self.assertFalse(compile_source(source, target={'version':'0.0.9'}, optimize=optimize).success)
                        self.assertFalse(compile_source(source.replace('profile("animation")', 'profile("animation")\nrequires("0.0.9")'), optimize=optimize).success)
                        forged = result.bytecode.replace(b'"requires":"0.1.0"', b'"requires":"0.0.9"')
                        self.assertNotEqual(forged, result.bytecode)
                        self.assertFalse(self.run_package(forged)['load'])
                        run = self.run_package(result.bytecode)
                        self.assertEqual(run['status'], 1)
                        self.assertEqual(run['state'], [0 if expression.startswith('max') else 0x80000000])

    def test_overflowing_intermediate_faults(self):
        for operation in ('min', 'max'):
            source = PREFIX+'@init\ndef setup(ctx,runtime): ctx.value=0.0\n@render\ndef draw(ctx,runtime,pixels): ctx.value='+operation+'(1e38*1e38,1.0)\n'
            result = compile_source(source)
            self.assertTrue(result.success, result.diagnostics)
            self.assertEqual(self.run_package(result.bytecode)['status'], -1)

    def test_independent_typed_callback_floors(self):
        # Explicit wire records: no compiler or generated fixture creates these.
        for op in (25, 26, 38):
            for release in ('0.0.0', '0.0.9', '0.1.0'):
                native = op == 38
                manifest = json.dumps({'requires':release}, separators=(',', ':')).encode()
                header = [4,3,2,0,1,0,1,int(native),0,0,0,0,0,0,0,0,0,0,len(manifest),0,0,0,0]
                package = b'LBC1'+struct.pack('<23I', *header)
                package += b''.join(struct.pack('<BBHI', 2, role, 0, bits) for role,bits in ((1,0),(1,0),(0,0)))
                package += struct.pack('<4BI4BI', op,2,0,0 if native else 1,0,1,2,0,0,0)
                package += struct.pack('<6I',0,2,0,2,0,1 if native else 2)
                package += struct.pack('<BBHIII',0,1,0,0,0,0)
                if native: package += struct.pack('<II8B',7,1,*([0]*8))
                package += manifest
                with self.subTest(op=op, release=release):
                    self.assertEqual(self.run_package(package)['load'], release == '0.1.0')


if __name__ == '__main__': unittest.main()
