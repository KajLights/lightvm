"""Managed ctx/runtime callbacks, frozen settings, arrays and random contracts."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
from bytecode_fixtures import ROOT, program, sample
sys.path.insert(0,str(ROOT/'tools'))
from client import Client, ProcessTransport, FRAME, PLAY, STOP, RESET, BEGIN, CHUNK, FINISH, ProtocolError
from lightvm.compiler import compile_source
from managed_replay import Example, Random
from test_configuration import config

PREFIX='from lightvm import profile,setting,rgb,init,render\nprofile("animation/1")\n'
NAMES=tuple(path.stem for path in sorted((ROOT/'examples/animations').glob('*.py')))
REPLAY_NAMES=('color','pattern','rainbow')
def source(name):
    path=ROOT/'examples/animations'/f'{name}.py'
    return path.read_text().replace('random.seed()', 'random.seed(1)')

class PortableCompilerTests(unittest.TestCase):
    def test_target_negotiation_and_deterministic_compilation(self):
        target={'module_limit':32768,'program_abis':[4], 'bytecode_formats':['LBC1/4'],
                'program_schemas':['lightvm.program/10'],'native_drawing_api':4,'program_profiles':['animation/1']}
        for name in NAMES:
            result=compile_source(source(name))
            self.assertTrue(result.success,result.diagnostics)
            self.assertEqual(result.bytecode[:8],b'LBC1\x04\0\0\0')
            self.assertEqual(result.bytecode,compile_source(source(name),target=target).bytecode)
            self.assertLess(len(result.bytecode),8192)
            self.assertLessEqual(struct.unpack_from('<I',result.bytecode,8)[0],128)
        for key in ('program_abis','bytecode_formats','program_schemas','program_profiles'):
            self.assertFalse(compile_source(source('color'),target={**target,key:[]}).success)
        self.assertFalse(compile_source(source('color'),target={**target,'module_limit':100}).success)

    def test_invalid_callback_signatures_and_bindings_are_rejected(self):
        cases=[
            '@render()\ndef draw(ctx,runtime,pixels): pass',
            '@render\ndef draw(): pass',
            '@init\ndef setup(ctx,runtime,pixels): pass\n@render\ndef draw(ctx,runtime,pixels): pass',
            '@render\ndef draw(ctx,runtime,pixels): pixels.fill(rgb(count,0,0))',
            '@render\ndef draw(ctx,runtime,pixels): x=time+delta',
            'x=0\n@render\ndef draw(ctx,runtime,pixels): pass',
            '@render\ndef draw(ctx,runtime,pixels): ctx.x=1',
            '@init\ndef setup(ctx,runtime):\n if True: ctx.x=1\n@render\ndef draw(ctx,runtime,pixels): pass',
            '@init\ndef setup(ctx,runtime): return\n@render\ndef draw(ctx,runtime,pixels): pass',
            '@init\ndef setup(ctx,runtime): ctx.x=setting(1)\n@render\ndef draw(ctx,runtime,pixels): ctx.x=2',
            '@render\ndef draw(ctx,runtime,pixels): runtime.time=0.0',
            '@render\ndef draw(ctx,runtime,pixels):\n for runtime in range(2): pass',
            '@render\ndef draw(ctx,runtime,pixels): ctx.x=setting(1)',
            '@init\ndef setup(ctx,runtime): runtime.on_change(handler)\n@render\ndef draw(ctx,runtime,pixels): pass',
        ]
        for body in cases:
            with self.subTest(body=body): self.assertFalse(compile_source(PREFIX+body).success)

    def test_invalid_standard_library_signatures(self):
        for expr in ('random.next(359)','random.randint(2)','random.randint(1, b=2, a=1)',
                     'random.randrange(1.0)','random.random(2)','random.seed("foo")','random.seed(version=3)',
                     'array("z", [0])','array("i", [0], 2)'):
            result=compile_source('import random\nfrom array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.x='+expr+'\n@render\ndef draw(ctx,runtime,pixels): pass')
            self.assertFalse(result.success,expr)

class AnimationProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.transport=ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/host/lightvm')),Path(self.temp.name)/'store.bin')
        self.client=Client(self.transport)
        self.client.configure(config(5))
    def tearDown(self): self.transport.close();self.temp.cleanup()
    def frame(self,ms=0):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',ms)))
        return bytes(self.client.pixel_snapshot)
    def reset(self,settings=None,values=None):
        state=self.client.program_parameters()
        return self.client.manage('program_reset',expected_generation=state['generation'],**({'settings':settings} if settings is not None else {}),**({'values':values} if values is not None else {}))

    def test_examples_match_python_caller_contract(self):
        for count in (1,5,175):
            self.client.configure(config(count))
            for name in REPLAY_NAMES:
                with self.subTest(name=name,count=count):
                    self.client.upload(program(source(name))); reference=Example(name,count)
                    for ms in (0,7,33,407,1000,0,1000,1000):
                        actual,expected=self.frame(ms),reference.frame(ms)
                        self.assertEqual(len(actual),count*3)
                        self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)),1 if name=='rainbow' else 0)
                    state=self.client.program_parameters()
                    self.assertEqual(state['render_count'],9)
                    self.assertNotIn('logical_ticks',state);self.assertNotIn('cycle_count',state)

    def test_supplied_delta_and_state_once_per_call(self):
        self.client.upload(program(PREFIX+'''@init
def setup(state, clock):
 state.calls=0
 state.total=0.0
@render
def draw(state, clock, output):
 state.calls+=1
 state.total+=clock.delta
 output.fill(rgb(state.calls,int(clock.delta*10.0),int(state.total*10.0)))
'''))
        self.assertEqual(self.frame(33),bytes([2,0,0])*5)
        self.assertEqual(self.frame(1000),bytes([3,10,10])*5)
        self.assertEqual(self.frame(0),bytes([4,0,10])*5)
        self.assertEqual(self.client.program_parameters()['program_us'],1_033_000)

    def test_settings_are_frozen_until_explicit_reinitialization(self):
        self.client.upload(program('from array import array\n'+PREFIX+'''@init
def setup(ctx,runtime):
 ctx.size=setting(2,minimum=1,maximum=runtime.pixel_count)
 ctx.data=array("I",[0]*ctx.size)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(ctx.size,len(ctx.data),0))
'''))
        before=self.client.program_parameters()
        for values in ({'size':4},{}):
            with self.assertRaisesRegex(ProtocolError,'frozen'): self.client.set_parameters(values)
        self.assertEqual(self.client.program_parameters()['settings_values'],{'size':2})
        self.assertEqual(self.client.program_parameters()['parameter_revision'],before['parameter_revision'])
        self.assertEqual(self.frame(),bytes([2,2,0])*5)
        with self.assertRaises(ProtocolError): self.reset({'size':6})
        self.assertEqual(self.client.program_parameters()['generation'],before['generation'])
        self.assertEqual(self.frame(),bytes([2,2,0])*5)
        self.reset({'size':4})
        self.assertEqual(self.frame(),bytes([4,4,0])*5)

    def test_standard_random_ranges_seed_reproducibility_and_float(self):
        self.client.upload(program('import random\n'+PREFIX+'''@init
def setup(ctx,runtime): random.seed(a=17,version=2)
@render
def draw(ctx,runtime,pixels):
 pixels[0]=rgb(random.randint(a=0,b=255),random.randrange(255,-1,-2),int(random.random()*256.0))
 pixels[1]=rgb(random.randrange(256),random.randint(3,3),0)
'''))
        expected=Random();expected.seed(17)
        def next_expected(): return bytes([expected.randint(0,255),expected.randrange(255,-1,-2),int(expected.random()*256),expected.randrange(256),expected.randint(3,3),0])+bytes(9)
        next_expected() # initial preparation render
        frames=[]
        for _ in range(30):
            frame=self.frame();self.assertEqual(frame,next_expected());frames.append(frame)
        self.reset()
        self.assertEqual([self.frame() for _ in range(30)],frames)
        for expression in ('random.randrange(0)','random.randrange(1,2,0)','random.randint(3,2)'):
            self.client.upload(program('import random\n'+PREFIX+f'''@render
def draw(ctx,runtime,pixels):
 if runtime.delta>0.0: x={expression}
'''))
            self.assertNotEqual(self.client.command(FRAME,struct.pack('<I',1)).code,0)

    def test_array_augmented_index_evaluated_once_and_helper_arguments(self):
        self.client.upload(program('from array import array\n'+PREFIX+'''@init
def setup(ctx,runtime):
 ctx.data=array("i",[4,10])
 ctx.index=0
def index(ctx):
 result=ctx.index
 ctx.index+=1
 return result
def increment(value):
 value+=1
 return value
@render
def draw(ctx,runtime,pixels):
 ctx.index=0
 ctx.data[index(ctx)]+=1
 value=4
 result=increment(value)
 pixels.fill(rgb(ctx.data[0],ctx.index,value+result))
'''))
        self.assertEqual(self.frame(),bytes([6,1,9])*5)

    def test_fault_preserves_committed_pixels_and_checked_arrays(self):
        for code,value in (('B',256),('H',-1),('I',-1),('i',0)):
            self.client.upload(program('from array import array\n'+PREFIX+f'''@init
def setup(ctx,runtime): ctx.data=array("{code}",[0])
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(1,2,3))
 if runtime.delta>0.0: ctx.data[{1 if code=='i' else 0}]={value}
'''))
            before=self.frame()
            self.assertNotEqual(self.client.command(FRAME,struct.pack('<I',1)).code,0)
            self.assertTrue(self.client.program_parameters()['faulted'])
            self.assertEqual(self.client.snapshot()[1],before)

    def test_long_uptime_and_no_render_allocation(self):
        self.client.configure(config(17))
        for name in NAMES:
            self.client.upload(program(source(name)))
            initial=self.client.program_parameters()
            for _ in range(80): self.frame(1000)
            state=self.client.program_parameters()
            self.assertEqual(state['program_us'],80_000_000)
            self.assertEqual(state['render_count'],initial['render_count']+80)
            self.assertEqual(state['heap_used'],initial['heap_used'])
            self.assertFalse(state['faulted'])

    def test_old_binary_version_and_forged_entry_permissions_rejected(self):
        for patch in ('version','rights','reserved'):
            data=bytearray(sample('pattern'))
            regs,code,colors=struct.unpack_from('<3I',data,8)
            if patch=='version': struct.pack_into('<I',data,4,3)
            elif patch=='reserved': struct.pack_into('<I',data,48,1)
            else: struct.pack_into('<I',data,96+regs*8+code*8+colors*4+8,13)
            with self.assertRaises(ProtocolError): self.client.upload(data)
        self.client.upload(sample('pattern'));self.assertEqual(len(self.frame()),15)
    def test_immutable_package_growth_does_not_grow_resident_ram(self):
        data = sample('color')
        self.client.upload(data)
        small = self.client.program_parameters()
        self.assertLess(small['instance_context_bytes'], 2048)
        self.assertLessEqual(self.client.describe()['upload_workspace_bytes'], 512)
        self.assertEqual(self.client.describe()['upload_page_bytes'], 256)
        # Pad the manifest and duplicate unused native binding records. Both
        # are validated, but neither acquires a resident copy on activation.
        large = bytearray(data)
        regs, code, colors, functions, resources, expressions, calls = struct.unpack_from('<7I', large, 8)
        at = 96 + regs*8 + code*8 + colors*4 + functions*24 + resources*24 + expressions*16
        self.assertGreater(calls, 0)
        # Retain the original indices by appending identical records.
        large = bytearray(data[:at+calls*16] + data[at:at+16]*(256-calls) + data[at+calls*16:])
        struct.pack_into('<I', large, 32, 256)
        manifest_size = struct.unpack_from('<I', large, 76)[0]
        large.extend(b' ' * (8192-manifest_size))
        struct.pack_into('<I', large, 76, 8192)
        self.assertGreater(len(large)-len(data), 10000)
        self.client.upload(large)
        big = self.client.program_parameters()
        for field in ('heap_used', 'metadata_bytes', 'register_bytes', 'state_bytes', 'instance_context_bytes'):
            self.assertEqual(big[field], small[field], field)
        self.assertLess(big['register_bytes'], regs*8)
        self.assertEqual(self.client.status_fields()['code_storage'], 'flash')
        self.reset()
        self.assertEqual(self.client.status_fields()['code_storage'], 'flash')

    def test_live_reset_preserves_shared_upload_transaction(self):
        self.client.upload(sample('color'))
        data=sample('pattern')
        self.client.require_ok(self.client.command(BEGIN,struct.pack('<II',len(data),zlib.crc32(data))))
        self.client.require_ok(self.client.command(CHUNK,bytes(4)+data[:100]))
        self.reset()
        self.assertEqual(self.client.status_fields()['uploading'],'1')
        for at in range(100,len(data),508):
            self.client.require_ok(self.client.command(CHUNK,struct.pack('<I',at)+data[at:at+508]))
        self.client.require_ok(self.client.command(FINISH))
        self.assertEqual(self.client.status_fields()['uploading'],'0')
        self.reset()
        self.assertEqual(self.client.status_fields()['code_storage'], 'flash')

    def test_stored_activation_settings_and_materialization(self):
        self.client.save_procedure('paint',sample('color'))
        settings={'random_color':False,'color':0x123456}
        self.client.activate('paint',settings=settings)
        before=self.client.snapshot()[1];self.assertEqual(before,bytes([0x12,0x34,0x56])*5)
        generation=self.client.program_parameters()['generation']
        with self.assertRaises(ProtocolError):self.client.activate('paint',settings={'seed':-1})
        self.assertEqual(self.client.program_parameters()['generation'],generation)
        self.assertEqual(self.client.snapshot()[1],before)
        self.client.save_procedure('other',sample('pattern'))
        self.client.delete_procedure('other')
        self.reset()
        self.assertEqual(self.frame(),before)
