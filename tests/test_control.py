"""Control ABI checks against the real wasm3 host, with independent Python replay."""
import json
import os
from pathlib import Path
import struct
import sys
import time
import unittest
import wasmtime

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
from client import Client, ProcessTransport, FRAME, PLAY, RESET, RUN, STOP, MANAGE
from lightvm import Context, Pixels, Parameter, rgb
from lightvm import easing
from lightvm.compiler import compile_source, uleb

PREFIX = 'from lightvm import pixels, parameter, rgb, hsv, on_start, on_frame, RGB\np = pixels("A")\n'


def artifact(source):
    result = compile_source(source)
    if result.diagnostics:
        raise AssertionError(result.diagnostics)
    return result


def with_manifest(wat, manifest=None):
    m = manifest or {'schema': 'lightvm.program/1', 'abi': 1, 'binding': 'A', 'retention': 'fresh', 'parameters': []}
    name = b'lightvm.program'
    section = uleb(len(name)) + name + json.dumps(m).encode()
    return bytes(wasmtime.wat2wasm(wat)) + b'\0' + uleb(len(section)) + section


class Replay:
    """Execute only trusted test/examples; compilation itself never does this."""
    def __init__(self, source, count):
        namespace = {}
        exec(compile(source, '<trusted-reference>', 'exec'), namespace)
        self.pixels = next(v for v in namespace.values() if isinstance(v, Pixels))
        self.parameters = {v.name: v for v in namespace.values() if isinstance(v, Parameter)}
        self.handlers = {v.lightvm_trigger: v for v in namespace.values() if callable(v) and hasattr(v, 'lightvm_trigger')}
        self.pixels.bind(count)
        self.time = 0
        self.pixels.begin()
        if 0 in self.handlers:
            self.handlers[0](Context(0, 0))
        self.pixels.commit()

    def step(self, delta_ms):
        self.time += delta_ms * 1000
        self.pixels.begin()
        self.handlers[1](Context(self.time, delta_ms * 1000))
        self.pixels.commit()
        return b''.join(c.to_bytes(3, 'big') for c in self.pixels.committed)


class CompilerTests(unittest.TestCase):
    def test_memoryless_artifacts_and_target_admission(self):
        source = PREFIX + '@on_frame\ndef draw(ctx):\n p.fill(rgb(1,2,3))\n'
        result = artifact(source)
        module = wasmtime.Module(wasmtime.Engine(), result.wasm)
        self.assertEqual([e.name for e in module.exports], ['control_step'])
        self.assertTrue(all(isinstance(i.type, wasmtime.FuncType) for i in module.imports))
        self.assertLess(len(result.wasm), 4096)
        self.assertFalse(compile_source(source, target={'module_limit': 100, 'control_abi': 1}).success)
        self.assertFalse(compile_source(source, target={'module_limit': 32768}).success)
        self.assertTrue(compile_source(source, target={'module_limit': 32768, 'control_abi': 1}).success)

    def test_rejects_unsupported_or_ambiguous_python_with_locations(self):
        bad = [
            'import os', 'from os import system', 'from lightvm import *',
            'class Effect: pass', 'x = []', 'x = open("/tmp/no-code-execution", "w")',
            '@on_frame\ndef draw(ctx):\n while True: pass',
            '@on_frame\nasync def draw(ctx): pass',
            '@on_frame\ndef draw(ctx):\n raise ValueError()',
            'def f(x: int) -> int:\n return f(x)\n@on_frame\ndef draw(ctx):\n f(1)',
            'def f(x: int) -> int:\n if x > 0: return x\n@on_frame\ndef draw(ctx): pass',
            '@on_frame\ndef draw(ctx):\n if ctx.delta > 0.0: x = 1\n p.fill(rgb(x,0,0))',
            '@on_frame\ndef draw(ctx):\n a = 1 and 2',
            '@on_frame\ndef draw(ctx):\n for i in range(0): pass\n p.fill(rgb(i,0,0))',
            'x = 9223372036854775808', 'x = 1e400', 'min = 3',
            '@on_frame\ndef draw(ctx):\n min = 3',
            'def f(p: int) -> int:\n return p\n@on_frame\ndef draw(ctx): pass',
        ]
        for fragment in bad:
            with self.subTest(fragment=fragment):
                result = compile_source(PREFIX + fragment, 'bad.py')
                self.assertFalse(result.success)
                self.assertGreater(result.diagnostics[0].line, 0)
                self.assertEqual(result.diagnostics[0].filename, 'bad.py')

    def test_parameter_and_palette_declaration_limits(self):
        source = PREFIX + ''.join(f'p{i} = parameter("p{i}", {i})\n' for i in range(9)) + '@on_frame\ndef draw(ctx): pass\n'
        self.assertFalse(compile_source(source).success)
        for decl in ('v = parameter("x", 1, minimum=2)', 'v = parameter("x", True, minimum=0)',
                     'v = parameter("x", -1, kind="rgb8")', 'v = parameter("bad name", 1)',
                     'v = parameter("x", 1e309)'):
            self.assertFalse(compile_source(PREFIX + decl + '\n@on_frame\ndef draw(ctx): pass\n').success)
        source = PREFIX + 'palette = (' + ','.join('rgb(1,2,3)' for _ in range(17)) + ')\n@on_frame\ndef draw(ctx): pass\n'
        self.assertFalse(compile_source(source).success)

    def test_easing_equations_endpoints_monotonicity_and_symmetry(self):
        for name in ('linear', 'quadratic_in', 'quadratic_out', 'quadratic_in_out',
                     'cubic_in', 'cubic_out', 'cubic_in_out', 'smoothstep'):
            fn = getattr(easing, name)
            self.assertEqual([fn(t) for t in (-10, 0, 1, 10)], [0, 0, 1, 1])
            values = [fn(i / 1000) for i in range(1001)]
            self.assertEqual(values, sorted(values))
        for family in ('quadratic', 'cubic'):
            for i in range(101):
                t = i / 100
                self.assertAlmostEqual(getattr(easing, family + '_in')(t), 1-getattr(easing, family + '_out')(1-t))
                self.assertAlmostEqual(getattr(easing, family + '_in_out')(t), 1-getattr(easing, family + '_in_out')(1-t))
        self.assertEqual(easing.quadratic_in(0.5), 0.25)
        self.assertEqual(easing.cubic_in(0.5), 0.125)
        self.assertEqual(easing.smoothstep(0.5), 0.5)


class ControlTests(unittest.TestCase):
    def setUp(self):
        self.transport = ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/host/lightvm')))
        self.client = Client(self.transport)
        self.normal = 175 if self.client.describe()['board'] == 'test_175' else 100
        self.configure(5)

    def configure(self, count):
        self.count = count
        self.client.configure({'schema': 'lightvm.config/1', 'devices': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': count}}
        ], 'targets': {'A': {'device': 'lights'}}})

    def tearDown(self):
        self.transport.close()

    def upload(self, source):
        return self.client.upload(artifact(source).wasm)

    def step(self, delta=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', delta)))
        return bytes(self.client.pixel_snapshot)

    def test_six_effects_match_reference_at_one_odd_and_normal_counts(self):
        for count in (1, 5, self.normal):
            self.configure(count)
            for file in sorted((ROOT / 'samples/python').glob('*.py')):
                with self.subTest(count=count, effect=file.stem):
                    source = file.read_text()
                    self.upload(source)
                    replay = Replay(source, count)
                    for delta in (0, 16, 33, 500, 1000, 0, 1000):
                        self.assertEqual(self.step(delta), replay.step(delta))
                    state = self.client.program_parameters()
                    self.assertEqual(state['program_us'], replay.time)
                    self.assertLess(int(self.client.status_fields()['heap']), 65536)

    def test_integer_float_arithmetic_loops_and_function_state(self):
        source = PREFIX + '''counter = 0

def bump() -> int:
 global counter
 counter += 1
 return counter

@on_frame
def draw(ctx):
 p[0] = rgb(-7 // 3 + 3, -7 % 3, 7 % -3 + 3)
 p[1] = rgb(int(-7.0 // 3.0) + 3, int(-7.0 % 3.0), abs(bump()))
 total = 0
 i = 99
 for i in range(5, 0, -1):
  if i == 4: continue
  total += i
  i = 255
 p[2] = rgb(total, i, int(True))
 p[3] = rgb(int(9223372036854775807 + 1 == -9223372036854775808), 0, 0)
 for j in range(10):
  if j == 2: break
  p[4] = rgb(j, 0, 0)
'''
        self.upload(source)
        replay = Replay(source, self.count)
        # Python's arbitrary-width arithmetic differs only at the documented wrap.
        expected = bytearray(replay.step(0)); expected[9] = 1
        self.assertEqual(self.step(), expected)
        self.assertEqual(self.step()[5], 2)  # abs evaluates a side-effecting function once.

    def test_compiled_easing_agrees_with_python(self):
        for name in ('linear', 'quadratic_in', 'quadratic_out', 'quadratic_in_out',
                     'cubic_in', 'cubic_out', 'cubic_in_out', 'smoothstep'):
            source = PREFIX + f'from lightvm.easing import {name}\n@on_frame\ndef draw(ctx):\n p.fill(rgb(int(255.0 * {name}(ctx.time - 0.5)), 0, 0))\n'
            self.upload(source)
            replay = Replay(source, self.count)
            for delta in (0, 500, 500, 500, 500):
                self.assertEqual(self.step(delta), replay.step(delta))

    def test_argument_and_assignment_evaluation_order(self):
        source = PREFIX + '''value = 0
def next_value() -> int:
 global value
 value += 1
 return value
@on_frame
def draw(ctx):
 for i in range(next_value(), next_value(), next_value()):
  p[0] = rgb(i, 0, 0)
 p[next_value() - 4] = rgb(next_value(), 0, 0)
'''
        self.upload(source)
        self.assertEqual(self.step(), Replay(source, self.count).step(0))

    def test_stored_control_startup_embeds_defaults(self):
        self.transport.close()
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            storage = Path(folder) / 'store.bin'
            executable = os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/host/lightvm'))
            self.transport = ProcessTransport(executable, storage=storage)
            self.client = Client(self.transport)
            source = (ROOT / 'samples/python/color.py').read_text()
            self.client.save_procedure('color-control', artifact(source).wasm)
            config = {'schema':'lightvm.config/1','devices':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':5}}],
                      'targets':{'A':{'device':'lights'}},'startup':{'procedure':'color-control','target':'A'}}
            self.client.configure(config, save=True)
            self.client.activate('color-control')
            self.client.set_parameters({'color':rgb(1,2,3)})
            self.transport.close()
            self.transport = ProcessTransport(executable, storage=storage)
            self.client = Client(self.transport)
            self.client.pause_program()
            self.assertEqual(self.client.program_parameters()['values']['color'], rgb(255,64,0))
            self.assertEqual(self.step(), bytes([255,64,0])*5)

    def test_finite_subnormal_parameter_and_palette_bounds(self):
        source = PREFIX + 'tiny=parameter("tiny", 1e-320)\npalette=(rgb(1,2,3),)\n@on_frame\ndef draw(ctx):\n p[0]=palette[int(ctx.time)]\n'
        self.upload(source)
        self.assertEqual(self.client.program_parameters()['values']['tiny'], 1e-320)
        first = self.step()
        self.assertNotEqual(self.client.command(FRAME, struct.pack('<I',1000)).code, 0)
        self.assertEqual(self.client.snapshot()[1], first)

    def test_presence_clear_erase_readback_and_retention(self):
        for retention in ('fresh', 'retained'):
            source = PREFIX.replace('pixels("A")', f'pixels("A", retention="{retention}")') + '''phase = 0
@on_frame
def draw(ctx):
 global phase
 if phase == 0:
  p.fill(rgb(10, 20, 30))
 else:
  p[0] = rgb(0, 0, 0)
  p.erase(1)
  p[2] = p[3]
  if p.present(0): p[4] = rgb(1, 0, 0)
 phase += 1
'''
            self.upload(source)
            self.step()
            before = self.client.snapshot()
            self.assertEqual(before[0]['presence'], '1f')
            second = self.step()
            self.assertEqual(second[:6], bytes(6))
            self.assertEqual(second[6:12], bytes([10,20,30])*2 if retention == 'retained' else bytes(6))
            metadata, snapshot = self.client.snapshot()
            self.assertEqual(snapshot, second)
            self.assertEqual(metadata['presence'], '1d' if retention == 'retained' else '15')
            source = PREFIX + '@on_frame\ndef draw(ctx):\n p.clear()\n'
            self.upload(source); self.step()
            self.assertEqual(self.client.snapshot()[0]['presence'], '1f')

    def test_live_parameters_atomic_validation_generations_and_defaults(self):
        source = PREFIX + '''r = parameter("r", 1, minimum=0, maximum=255)
level = parameter("level", 2.0, minimum=0.0, maximum=255.0)
enabled = parameter("enabled", True)
color = parameter("color", rgb(1, 2, 3), kind="rgb8")
phase = 0
@on_frame
def draw(ctx):
 global phase
 phase += 1
 p.fill(color.value)
 if enabled.value: p[0] = rgb(r.value, int(level.value), phase)
'''
        self.upload(source)
        self.assertEqual(self.step()[:3], bytes([1,2,1]))
        state = self.client.set_parameters({'r': 7, 'level': 9.5, 'color': rgb(5,6,7), 'enabled': True})
        self.assertEqual(self.step()[:6], bytes([7,9,2,5,6,7]))
        old = self.client.program_parameters()
        for patch in ({'r': 11, 'level': 999.0}, {'r': 1.0}, {'enabled': 1}, {'color': -1}, {'unknown': 1}, {'level': float('nan')}):
            reply = self.client.command(MANAGE, json.dumps({'op': 'program_parameters_set', 'expected_generation': old['generation'], 'expected_parameter_revision': old['parameter_revision'], 'values': patch}).encode())
            self.assertNotEqual(reply.code, 0)
            self.assertEqual(self.client.program_parameters()['values'], old['values'])
        self.assertEqual(state['parameter_revision'], 1)
        reply = self.client.command(MANAGE, json.dumps({'op':'program_parameters_set','expected_generation':old['generation'],'expected_parameter_revision':0,'values':{'r':8}}).encode())
        self.assertNotEqual(reply.code, 0)
        self.upload(source)
        self.assertNotEqual(self.client.program_parameters()['generation'], old['generation'])
        reply = self.client.command(MANAGE, json.dumps({'op':'program_pause','expected_generation':old['generation']}).encode())
        self.assertNotEqual(reply.code, 0)
        self.assertEqual(self.step()[:3], bytes([1,2,1]))

    def test_pause_stop_resume_start_once_and_passive_snapshot(self):
        source = PREFIX.replace('pixels("A")', 'pixels("A", retention="retained")') + '''starts = 0
@on_start
def start(ctx):
 global starts
 starts += 1
 p.fill(rgb(10, 20, 30))
@on_frame
def draw(ctx):
 p[0] = rgb(starts, int(ctx.time * 10.0), 0)
'''
        self.upload(source)
        first = self.step(100)
        self.client.require_ok(self.client.command(PLAY))
        self.client.pause_program()
        before, image = self.client.snapshot()
        time.sleep(0.03)
        after, repeated = self.client.snapshot()
        self.assertEqual(before, after)
        self.assertEqual(image, repeated)
        frozen = after['state']['program_us']
        self.client.require_ok(self.client.command(STOP))
        stopped, blank = self.client.snapshot()
        self.assertEqual(blank, bytes(self.count*3))
        self.assertEqual(stopped['state']['program_us'], frozen)
        self.client.resume_program()
        self.client.pause_program()
        resumed = self.step()
        self.assertEqual(resumed[0], 1)
        self.assertEqual(resumed[3:], first[3:])
        self.assertLess(self.client.program_parameters()['program_us'] - frozen, 20000)

    def test_trap_discards_pending_paint_preserves_output_and_fault_requires_reactivation(self):
        source = PREFIX + '''phase = 0
@on_frame
def draw(ctx):
 global phase
 p.fill(rgb(30, 40, 50))
 if phase > 0:
  p.fill(rgb(255, 0, 0))
  p[p.count] = rgb(0, 0, 0)
 phase += 1
'''
        self.upload(source)
        first = self.step()
        before, _ = self.client.snapshot()
        reply = self.client.command(FRAME, struct.pack('<I', 10))
        self.assertNotEqual(reply.code, 0)
        after, output = self.client.snapshot()
        self.assertEqual(output, first)
        self.assertEqual(after['state']['output_frame'], before['state']['output_frame'])
        self.assertTrue(after['state']['faulted'])
        self.assertEqual(self.client.program_parameters()['program_us'], 10000)
        reply = self.client.command(MANAGE, json.dumps({'op':'program_resume','expected_generation':after['state']['generation']}).encode())
        self.assertNotEqual(reply.code, 0)
        self.client.require_ok(self.client.command(RESET))
        self.assertEqual(self.client.status_fields()['heap'], '0')

    def test_metadata_import_signature_memory_and_namespace_rejections(self):
        base = '(func (export "control_step") (param i32 i64 i64) (result i32) i32.const 0)'
        for wat in ('(module (memory 1) '+base+')', '(module (table 1 funcref) '+base+')',
                    '(module (import "other" "x" (func)) '+base+')',
                    '(module (import "lightvm.control.v1" "count" (func (result i32))) '+base+')',
                    '(module (func (export "control_step") (param i32) (result i32) i32.const 0))'):
            with self.subTest(wat=wat):
                try:
                    self.client.upload(with_manifest(wat))
                    reply = self.client.command(FRAME, struct.pack('<I', 0))
                    self.assertNotEqual(reply.code, 0)
                except RuntimeError:
                    pass
                self.client.require_ok(self.client.command(RESET))
        bad = {'schema':'lightvm.program/1','abi':1,'binding':'B','retention':'fresh','parameters':[]}
        with self.assertRaises(RuntimeError): self.client.upload(with_manifest('(module '+base+')', bad))
        duplicate = with_manifest('(module '+base+')')
        # Append its custom section a second time.
        plain = bytes(wasmtime.wat2wasm('(module '+base+')'))
        with self.assertRaises(RuntimeError): self.client.upload(duplicate + duplicate[len(plain):])

    def test_control_calls_outside_step_and_nonzero_step_result_fail(self):
        wasm = with_manifest('''(module
          (import "lightvm.control.v1" "resource" (func $r (param i32) (result i32)))
          (func (export "run") i32.const 0 call $r drop)
          (func (export "control_step") (param i32 i64 i64) (result i32) i32.const 1))''')
        self.client.upload(wasm)
        self.assertNotEqual(self.client.command(RUN).code, 0)
        self.assertNotEqual(self.client.command(FRAME, struct.pack('<I',0)).code, 0)
        self.assertTrue(self.client.program_parameters()['faulted'])

    def test_control_deadline_and_native_demo_after_control_playback(self):
        self.upload((ROOT / 'samples/python/rainbow.py').read_text())
        self.step()
        from client import NATIVE_DEMO
        self.client.require_ok(self.client.command(NATIVE_DEMO))
        self.assertEqual(len(self.step(16)), self.count * 3)
        wasm = with_manifest('(module (func (export "control_step") (param i32 i64 i64) (result i32) (loop $forever br $forever) i32.const 0))')
        self.client.upload(wasm)
        reply = self.client.command(FRAME, struct.pack('<I',0))
        self.assertEqual(reply.code, 5)
        self.assertTrue(self.client.program_parameters()['faulted'])
        self.client.require_ok(self.client.command(RESET))
        self.assertEqual(self.client.status_fields()['heap'], '0')

    def test_repeated_load_and_full_parameter_capacity(self):
        source = PREFIX + ''.join(f'v{i}=parameter("v{i}", {i}, minimum=-100, maximum=255)\n' for i in range(8)) + '@on_frame\ndef draw(ctx):\n p.fill(rgb(v0.value,v1.value,v7.value))\n'
        for _ in range(5):
            self.upload(source)
            self.assertEqual(self.step()[:3], bytes([0,1,7]))
            self.client.require_ok(self.client.command(RESET))
            self.assertEqual(self.client.status_fields()['heap'], '0')


if __name__ == '__main__':
    unittest.main()
