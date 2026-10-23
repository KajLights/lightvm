"""Independent oracle through actual loader/scalar dispatch and SDK emission."""
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm import numeric
from lightvm.compiler import compile_source
from lightvm.compiler_core import CompileError
from lightvm.portable import PortableCompiler, P_OP
import numeric_oracle as oracle
import test_numeric as standalone

PREFIX = '''from lightvm import profile,init,render,setting,rgb,Input,fixed,fixed_bits,fixed_from_bits,fixed_sqrt,FixedArray
profile("animation")
'''


def driver():
    path = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT / 'build/host/test_platform'))
    if not path.exists(): raise unittest.SkipTest('build the native numeric runtime contract first')
    return path


class NumericRuntimeTests(standalone.NumericContractTests):
    """Replay the entire standalone corpus through typed VM invocation."""
    driver_args = ['--numeric-driver']

    @classmethod
    def setUpClass(cls):
        cls.driver = driver()
        cls.vectors = json.loads(standalone.VECTORS.read_text())


class NumericCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver = driver()
        cls.vectors = json.loads(standalone.VECTORS.read_text())

    def run_package(self, package):
        with tempfile.NamedTemporaryFile(suffix='.lbc') as file:
            file.write(package); file.flush()
            result = subprocess.run([str(self.driver), '--numeric-package', file.name],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, '')
        return json.loads(result.stdout)

    def compile(self, source, optimize=True):
        compiler = PortableCompiler(PREFIX + source, optimize=optimize)
        package, manifest = compiler.compile()
        self.assertEqual(manifest['requires'], '1.2.0')
        return compiler, package

    def expression(self, expression):
        return f'@init\ndef setup(ctx,runtime): ctx.result={expression}\n@render\ndef draw(ctx,runtime,pixels): pass\n'

    def source_operation(self, op, args):
        def integer(word): return str(oracle.signed(word))
        def fixed(word): return f'fixed_from_bits({integer(word)})'
        a, b = args[0], args[1] if len(args) > 1 else 0
        suffix = op.split('.')[1]
        if op.startswith('i32.'):
            if suffix == 'not': return '~(' + integer(a) + ')'
            return f'({integer(a)}) ' + {'and':'&', 'or':'|', 'xor':'^', 'shl':'<<', 'shr':'>>'}[suffix] + f' ({integer(b)})'
        if suffix == 'from_i32': return f'fixed(int({integer(a)}))'
        if suffix == 'from_f32':
            value = struct.unpack('<f', struct.pack('<I', a))[0]
            if not math.isfinite(value): return None  # Invalid invocation inputs are covered above.
            return f'fixed(float({value!r}))'
        if suffix == 'from_bits': return fixed(a)
        if suffix == 'neg': return '-' + fixed(a)
        if suffix in ('min', 'max'): return f'{suffix}({fixed(a)}, {fixed(b)})'
        if suffix in ('to_i32', 'to_f32', 'bits', 'sqrt'):
            name = {'to_i32':'int', 'to_f32':'float', 'bits':'fixed_bits', 'sqrt':'fixed_sqrt'}[suffix]
            return f'{name}({fixed(a)})'
        return fixed(a) + ' ' + {'add':'+', 'sub':'-', 'mul':'*', 'div':'/', 'mod':'%', 'lt':'<', 'eq':'=='}[suffix] + ' ' + fixed(b)

    def test_reviewed_vectors_through_folding_and_scalar_emission(self):
        for case in self.vectors['exact']:
            args = tuple(int(word, 16) for word in case['args'])
            expression = self.source_operation(case['op'], args)
            if expression is None: continue
            for optimize in (False, True):
                with self.subTest(operation=case['op'], args=args, optimize=optimize):
                    _, package = self.compile(self.expression(expression), optimize)
                    result = self.run_package(package)
                    self.assertTrue(result['load'] and result['prepare'])
                    if case.get('fault'): self.assertEqual(result['status'], -1)
                    else:
                        self.assertEqual(result['status'], 1)
                        self.assertEqual(result['state'], [int(case['result'], 16)])

    def test_exact_literal_spelling(self):
        for case in self.vectors['literals']:
            source = self.expression('fixed(' + case['source'] + ')')
            if case.get('fault'):
                with self.assertRaises(CompileError): self.compile(source)
            else:
                _, package = self.compile(source)
                self.assertEqual(self.run_package(package)['state'], [int(case['result'], 16)])
        for spelling in ('- 1.5', '- (1.5)', '+ 1.5', '0x7fff', '1_000.25'):
            _, package = self.compile(self.expression('fixed(' + spelling + ')'))
            value = {'- 1.5':-98304, '- (1.5)':-98304, '+ 1.5':98304,
                     '0x7fff':2147418112, '1_000.25':65552384}[spelling]
            self.assertEqual(self.run_package(package)['state'], [value & oracle.MASK])

    def test_complete_metadata_matches_native_and_python(self):
        run = subprocess.run([str(self.driver), '--numeric-metadata'], capture_output=True, text=True, check=True)
        self.assertFalse(run.stderr)
        native = {r.pop('name'):r for r in json.loads(run.stdout)}
        catalogue = json.loads(standalone.CATALOGUE.read_text())
        entries = [r for r in catalogue['operations'] if r['implementation']=='runtime']
        self.assertEqual(set(native), {r['name'] for r in entries})
        types = {'i32':1, 'f32':2, 'bool':4, 'q16':7}
        for row in entries:
            expected = {'id':row['wire_id']['id'], 'signature':[types[t] for t in [row['result'],*row['arguments']]],
                        'release':row['feature_floor']['release'], 'kernel_release':row['kernel_feature_floor'],
                        'work':row['work_cost'], 'effects':0, 'kernel':row['kernel_admitted']}
            self.assertEqual(native[row['name']], expected)
            self.assertEqual(numeric.OPERATIONS[row['wire_name']], expected)
            if 'existing_binding' in row:
                binding = row['existing_binding']
                self.assertEqual(numeric.NATIVE_OPERATIONS[binding['id']], row['wire_name'])
                self.assertEqual(binding['feature_floor'], expected['release'])
                self.assertEqual(binding['kernel_feature_floor'], expected['kernel_release'])

    def test_randomized_fold_matches_independent_oracle(self):
        rng = random.Random(0x51f01d)
        catalogue = json.loads(standalone.CATALOGUE.read_text())
        by_id = {value:name for name,value in numeric.OPCODES.items()}
        names = {}
        types = {'q16':7, 'i32':1, 'f32':2, 'bool':4}
        for operation in catalogue['operations']:
            if operation['implementation'] != 'runtime' or operation['name'] == 'q16.eq' or operation['name'].startswith('f32.'): continue
            name = by_id[operation['wire_id']['id']]
            self.assertEqual(numeric.SIGNATURES[name], tuple(types[t] for t in [operation['result'], *operation['arguments']]))
            self.assertEqual(operation['feature_floor']['release'], numeric.RELEASE)
            names[operation['name']] = name
        self.assertEqual(set(names.values()), set(numeric.SIGNATURES)-set(numeric.MATH_SIGNATURES))
        for _ in range(1024):
            args = rng.getrandbits(32), rng.getrandbits(32)
            for operation, name in names.items():
                try: expected = oracle.evaluate(operation, *args)
                except oracle.NumericFault:
                    with self.assertRaises(ArithmeticError): numeric.evaluate(name, *args)
                else: self.assertEqual(numeric.evaluate(name, *args), expected, (operation, args))

    def test_arrays_declarations_and_scalar_loops(self):
        source = '''@init
def setup(ctx,runtime):
 ctx.level=setting(3.0)
 ctx.a=FixedArray(runtime.pixel_count, fill=fixed(ctx.level)+fixed(0.5))
 ctx.b=FixedArray(runtime.pixel_count)
 ctx.result=fixed(0)
@render
def draw(ctx,runtime,pixels):
 ctx.a.fill(fixed(-1.5), start=1, stop=66)
 ctx.b.copy_from(ctx.a)
 for i in range(pixels.count):
  ctx.result=ctx.b[i]
  pixels[i]=rgb(int(ctx.result+fixed(2)),0,0)
'''
        for optimize in (False, True):
            compiler, package = self.compile(source, optimize)
            self.assertNotIn(P_OP['PIXEL_MAP'], [ins[0] for ins in compiler.code])
            result = self.run_package(package)
            self.assertEqual(result['status'], 1)
            expected = [229376] + [0xfffe8000] * 65 + [229376]
            self.assertEqual(result['arrays'], [expected, expected])
            self.assertEqual(result['state'], [229376])

    def test_dead_faults_stay_dormant_and_taken_faults_keep_prior_writes(self):
        for operation in ('fixed(1)/fixed(0)', 'fixed_sqrt(fixed(-1))', '1 << 32'):
            for taken in (False, True):
                source = '''@init
def setup(ctx,runtime): ctx.result=1
@render
def draw(ctx,runtime,pixels):
 ctx.result=2
 if %s:
  bad=%s
 ctx.result=3
''' % (taken, operation)
                for optimize in (False, True):
                    _, package = self.compile(source, optimize)
                    result = self.run_package(package)
                    self.assertEqual(result['status'], -1 if taken else 1)
                    self.assertEqual(result['state'], [2 if taken else 3])

    def test_invalid_authoring_and_target_floors(self):
        expressions = ['fixed(1)+1', 'fixed(1)<1.0', 'fixed(1)&fixed(2)',
                       'fixed(1)//fixed(2)', 'fixed(True)', 'fixed_bits(1)',
                       'fixed_from_bits(1.0)', 'fixed_sqrt(4)', 'rgb(fixed(1),0,0)']
        for expression in expressions:
            with self.assertRaises(CompileError, msg=expression): self.compile(self.expression(expression))
        for declaration in ('setting(fixed(1))', 'FixedArray(2,fill=1)', 'FixedArray(fixed(2))'):
            with self.assertRaises(CompileError, msg=declaration): self.compile(self.expression(declaration))
        source = PREFIX + self.expression('fixed(1)')
        result = compile_source(source, target={'version':'1.1.0','module_limit':32768})
        self.assertFalse(result.success)
        self.assertTrue(compile_source(source, target={'version':'1.2.0','module_limit':32768}).success)
        self.assertFalse(compile_source(PREFIX + 'level=Input("level",default=fixed(1))\n@render\ndef draw(ctx,runtime,pixels): pass').success)
        with self.assertRaises(CompileError):
            PortableCompiler(source.replace('profile("animation")', 'requires("1.1")\nprofile("animation")').replace('profile,', 'requires,profile,')).compile()

    def test_forged_types_encodings_permissions_and_floor(self):
        compiler, package = self.compile(self.expression('fixed(1) + fixed(2)'), False)
        code_start = 96 + len(compiler.regs) * 8
        function_start = code_start + len(compiler.code) * 8 + len(compiler.table) * 4
        pc = next(i for i, ins in enumerate(compiler.code) if ins[0] == P_OP['QADD'])
        mutations = [
            (code_start + pc * 8, b'\x3e'),  # Unknown opcode 62.
            (code_start + pc * 8, b'\x03'),  # IADD with fixed operands.
            (code_start + pc * 8 + 4, struct.pack('<I', 1)),
            (function_start + 8, struct.pack('<I', 0)),  # State write permission.
        ]
        fixed = next(i for i, reg in enumerate(compiler.regs) if reg[0] == 7)
        mutations += [(96 + fixed * 8, bytes([t])) for t in (1, 2, 4, 5, 6, 8, 255)]
        for offset, value in mutations:
            forged = bytearray(package); forged[offset:offset+len(value)] = value
            self.assertFalse(self.run_package(forged)['load'], (offset, value))
        self.assertFalse(self.run_package(package.replace(b'"requires":"1.2.0"', b'"requires":"1.1.0"'))['load'])
        # The shorthand floor remains valid for independent emitters.
        manifest_size = struct.unpack_from('<I', package, 76)[0]
        metadata = package[-manifest_size:].replace(b'"requires":"1.2.0"', b'"requires":"1.2"')
        shorthand = bytearray(package[:-manifest_size] + metadata)
        struct.pack_into('<I', shorthand, 76, len(metadata))
        self.assertEqual(self.run_package(shorthand)['status'], 1)
        _, bitwise = self.compile(self.expression('runtime.pixel_count & 255'), False)
        self.assertFalse(self.run_package(bitwise.replace(b'"requires":"1.2.0"', b'"requires":"1.1.0"'))['load'])

    def test_forged_fixed_resources_and_declaration_expressions(self):
        source = '''@init
def setup(ctx,runtime):
 ctx.level=setting(1.0)
 ctx.a=FixedArray(runtime.pixel_count,fill=fixed(ctx.level))
 ctx.b=FixedArray(runtime.pixel_count)
@render
def draw(ctx,runtime,pixels): ctx.b.copy_from(ctx.a)
'''
        compiler, package = self.compile(source, False)
        spec_start = 96 + len(compiler.regs)*8 + len(compiler.code)*8 + len(compiler.table)*4 + 48
        expr_start = spec_start + len(compiler.resources)*24
        convert = next(i for i,e in enumerate(compiler.expressions) if e[0] == P_OP['FTOQ'])
        reference = next(i for i,r in enumerate(compiler.regs) if r[:2] == (6,2) and r[2] == 1)
        mutations = [(spec_start+24, 1), (spec_start+24+20, 1), (spec_start+24+4, 999),
                     (96+reference*8+4, 999), (expr_start+convert*16+8, 1),
                     (expr_start+convert*16+4, convert)]
        for offset, value in mutations:
            forged = bytearray(package); struct.pack_into('<I',forged,offset,value)
            self.assertFalse(self.run_package(forged)['load'], (offset,value))
        for count in ('-1', '65536'):
            _, invalid = self.compile(self.expression(f'FixedArray({count})'))
            self.assertFalse(self.run_package(invalid)['prepare'])


class NumericHostTests(unittest.TestCase):
    def test_fixed_frames_and_rejected_replacement_preserve_live_state(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from client import Client, ProcessTransport, ProtocolError, FRAME
        from test_configuration import config
        from test_current_contract import manifest_variant
        receiver = Path(os.environ.get('LIGHTVM_RECEIVER', driver().with_name('lightvm')))
        source = PREFIX + '''@init
def setup(ctx,runtime): ctx.value=fixed(0)
@render
def draw(ctx,runtime,pixels):
 ctx.value=ctx.value+fixed(runtime.delta)
 pixels.fill(rgb(int(ctx.value*fixed(100)),0,0))
'''
        package, _ = PortableCompiler(source).compile()
        with tempfile.TemporaryDirectory() as temp:
            transport = ProcessTransport(receiver, Path(temp)/'store.bin')
            try:
                client = Client(transport); client.configure(config(5)); client.upload(package)
                for milliseconds, expected in ((250,25),(500,75)):
                    client.require_ok(client.command(FRAME,struct.pack('<I',milliseconds)))
                    self.assertEqual(bytes(client.pixel_snapshot), bytes([expected,0,0])*5)
                generation = client.program_state()['generation']
                for required in ('1.1.0','1.4.0'):
                    with self.assertRaises(ProtocolError):
                        client.save_program('invalid', manifest_variant(package,requires=required))
                    self.assertEqual(client.program_state()['generation'], generation)
                client.require_ok(client.command(FRAME,struct.pack('<I',250)))
                self.assertEqual(bytes(client.pixel_snapshot), bytes([100,0,0])*5)
            finally: transport.close()


if __name__ == '__main__': unittest.main()
