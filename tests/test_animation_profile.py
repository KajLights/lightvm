"""animation/1 contracts and replay of the supplied original picolights methods."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from bytecode_fixtures import ROOT, program, sample, legacy_sample
sys.path.insert(0,str(ROOT/'tools'))
from client import Client, ProcessTransport, FRAME, PLAY, STOP, RESET, ProtocolError
from lightvm.compiler import compile_source
from original_replay import Original, Random
from test_configuration import config

class PortableCompilerTests(unittest.TestCase):
    def test_negotiated_profile_preserves_unmarked_source(self):
        source=(ROOT/'samples/python/twinkle.py').read_text()
        result=compile_source(source)
        self.assertTrue(result.success,result.diagnostics)
        self.assertEqual(result.bytecode[:8],b'LBC1\x03\0\0\0')
        target={'module_limit':32768,'control_abi':2,'program_abis':[2,3],
                'bytecode_formats':['LBC1/2','LBC1/3'], 'program_schemas':['lightvm.program/3','lightvm.program/4'],'program_profiles':['animation/1']}
        self.assertEqual(result.bytecode,compile_source(source,target=target).bytecode)
        for key in ('program_abis','bytecode_formats','program_schemas','program_profiles'):
            self.assertFalse(compile_source(source,target={**target,key:[]}).success)
        self.assertEqual(legacy_sample('rainbow')[:8],b'LBC1\x02\0\0\0')

    def test_stage_permissions_and_invalid_source(self):
        prefix='from lightvm import profile,pixel_buffer,array,u8,rng,init,step,prepare,kernel,shade,rgb,complete\nprofile("animation/1")\npaint=pixel_buffer(length=count)\nrandom=rng(seed=1)\n'
        cases=[
            '@kernel\ndef draw():\n paint[0]=rgb(1,2,3)',
            '@kernel\ndef draw():\n random.uniform()',
            '@kernel\ndef draw():\n complete()',
            '@shade\ndef draw():\n return pixels[0]',
            '@step(period_us=20000)\ndef update():\n x=pixels[0]\n@kernel\ndef draw(): pass',
            '@init\ndef setup():\n complete()\n@kernel\ndef draw(): pass',
            '@step(period_us=20000)\ndef update():\n x=complete()\n@kernel\ndef draw(): pass',
            '@step\ndef update(): pass\n@kernel\ndef draw(): pass',
            '@step(period_us=0)\ndef update(): pass\n@kernel\ndef draw(): pass',
            'def mutate():\n paint[0]=rgb(1,2,3)\n return 1\n@kernel\ndef draw():\n x=mutate()',
            'def partial(x):\n if x: return 1\n@kernel\ndef draw():\n x=partial(True)',
        ]
        for body in cases:
            with self.subTest(body=body): self.assertFalse(compile_source(prefix+body).success)

    def test_all_six_portable_samples_compile(self):
        for name in ('color','pattern','chase','wipe','rainbow','twinkle'):
            with self.subTest(name=name):
                self.assertEqual(sample(name),sample(name))
                self.assertLess(len(sample(name)),8192)
                self.assertLessEqual(struct.unpack_from('<I',sample(name),8)[0],128)


class AnimationProfileTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.transport=ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/bytecode-host/lightvm')),Path(self.temp.name)/'store.bin')
        self.client=Client(self.transport)
        self.client.configure(config(5))

    def tearDown(self):
        self.transport.close();self.temp.cleanup()

    def frame(self,ms=0):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',ms)))
        return bytes(self.client.pixel_snapshot)

    def reset(self,settings=None,values=None):
        state=self.client.program_parameters()
        return self.client.manage('program_reset',expected_generation=state['generation'],**({'settings':settings} if settings is not None else {}),**({'values':values} if values is not None else {}))

    def test_original_methods_at_controlled_times_and_random_inputs(self):
        for count in (1,5,175):
            self.client.configure(config(count))
            for name in ('color','pattern','chase','wipe','rainbow','twinkle'):
                with self.subTest(name=name,count=count):
                    self.client.upload(sample(name));reference=Original(name,count)
                    for ms in (0,16,33,500,1000,0,1000,1000,1000):
                        actual,expected=self.frame(ms),reference.frame(ms)
                        self.assertEqual(len(actual),count*3)
                        self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)),1 if name in ('twinkle','rainbow') else 0)
                    self.assertEqual(self.client.program_parameters()['completed'],reference.completed)

    def test_constructor_settings_and_bounded_full_star_pool(self):
        cases=[('color',{'random_color':False,'color':0x123456}),
               ('pattern',{'random_pattern':False,'to_fit':False}),
               ('pattern',{'random_pattern':False,'to_fit':True}),
               ('rainbow',{'random_color':False,'color':0x123456,'duration':.3,'scale':2.0}),
               ('wipe',{'step_width':3,'color':0x123456}),
               ('twinkle',{'random_background':False,'color':0x123456,'random_stars':False,'twinkle_color':0xa4e17f,'duration':.04,'probability':100.0,'max_stars':5})]
        for name,settings in cases:
            with self.subTest(name=name,settings=settings):
                self.client.upload(sample(name));self.reset(settings);reference=Original(name,5,settings)
                for _ in range(15):
                    actual,expected=self.frame(20),reference.frame(20)
                    self.assertLessEqual(max(abs(a-b) for a,b in zip(actual,expected)),1 if name in ('rainbow','twinkle') else 0)

    def test_render_frequency_is_independent_of_simulation(self):
        for name in ('chase','rainbow','twinkle'):
            frames=[]
            for schedule in ([1000]*3,[20]*150,[7,8,9,16]*75):
                self.client.upload(sample(name))
                for ms in schedule:frame=self.frame(ms)
                frames.append(frame)
                state=self.client.program_parameters()
                self.assertEqual(state['program_us'],3_000_000)
                self.assertEqual(state['logical_ticks'],151)
                before=self.frame(0)
                self.assertEqual(self.frame(0),before)
                self.assertEqual(self.client.program_parameters()['logical_ticks'],151)
            self.assertEqual(frames[0],frames[1]);self.assertEqual(frames[1],frames[2])

    def test_completion_holds_and_reset_is_transactional(self):
        self.client.upload(sample('wipe'));final=self.frame(1000)
        state=self.client.program_parameters()
        self.assertTrue(state['completed']);self.assertEqual(state['cycle_count'],1)
        self.assertEqual(state['completion_time_us'],60_000)
        self.assertEqual(self.frame(1000),final)
        self.client.command(STOP);self.assertEqual(self.client.snapshot()[1],bytes(15))
        self.client.command(PLAY);self.assertEqual(self.client.snapshot()[1],final)
        with self.assertRaises(ProtocolError):self.reset({'step_width':6})
        self.assertEqual(self.client.program_parameters()['generation'],state['generation'])
        self.assertEqual(self.client.snapshot()[1],final)
        self.reset({'step_width':1})
        current=self.client.program_parameters()
        self.assertFalse(current['completed']);self.assertGreater(current['generation'],state['generation'])
        self.assertEqual(current['logical_ticks'],1)

    def test_rejected_manual_advance_does_not_mutate_state(self):
        self.client.upload(sample('chase'));self.reset({'update_us':1000})
        before=self.client.program_parameters();pixels=self.frame(0)
        result=self.client.command(FRAME,struct.pack('<I',1000))
        self.assertNotEqual(result.code,0)
        after=self.client.program_parameters()
        self.assertEqual(after['program_us'],before['program_us']);self.assertEqual(after['logical_ticks'],before['logical_ticks'])
        self.assertFalse(after['faulted']);self.assertEqual(self.frame(0),pixels)

    def test_fresh_manual_advance_is_rejected_before_init(self):
        source=(ROOT/'samples/python/chase.py').read_text().replace('setting(20_000,', 'setting(1_000,')
        self.client.upload(program(source))
        before=self.client.program_parameters()
        self.assertFalse(before['contribution_valid'])
        self.assertNotEqual(self.client.command(FRAME,struct.pack('<I',1000)).code,0)
        after=self.client.program_parameters()
        self.assertFalse(after['contribution_valid']);self.assertFalse(after['faulted'])
        self.assertEqual(after['logical_ticks'],0);self.assertEqual(after['program_us'],0)
        self.assertEqual(after['generation'],before['generation'])
        self.assertEqual(len(self.frame()),15)

    def test_after_period_and_scalar_helper_arguments(self):
        self.client.upload(program('''from lightvm import profile,init,step,kernel,rgb
profile("animation/1")
x=4
y=0
def increment(x):
 x+=1
 return x
@init
def setup():
 global y
 y=increment(x)
@step(period_us=20000,first="after_period")
def update():
 global x
 x+=1
@kernel
def draw(): pixels.fill(rgb(x,y,0))
'''))
        self.assertEqual(self.frame(0),bytes([4,5,0])*5)
        self.assertEqual(self.client.program_parameters()['logical_ticks'],0)
        self.assertEqual(self.frame(19),bytes([4,5,0])*5)
        self.assertEqual(self.frame(1),bytes([5,5,0])*5)
        self.assertEqual(self.frame(0),bytes([5,5,0])*5)

    def test_init_parameters_and_completed_reset_values(self):
        self.client.upload(program('''from lightvm import profile,parameter,pixel_buffer,rgb,init,step,kernel,complete
profile("animation/1")
color=parameter(rgb(1,2,3))
paint=pixel_buffer(length=count)
@init
def setup(): paint.fill(color)
@step(period_us=20000,first="after_period")
def update(): complete()
@kernel
def draw(): pixels.copy_from(paint)
'''))
        self.assertEqual(self.frame(),bytes([1,2,3])*5)
        self.client.set_parameters({'color':0x040506})
        self.assertEqual(self.frame(),bytes([1,2,3])*5)
        self.frame(20)
        with self.assertRaisesRegex(ProtocolError,'requires_reset'):
            self.client.set_parameters({'color':0x070809})
        self.client.reset_program(values={'color':0x070809})
        self.assertFalse(self.client.program_parameters()['completed'])
        self.assertEqual(self.frame(),bytes([7,8,9])*5)

    def test_checked_arrays_and_failed_render_keep_last_image(self):
        for declaration,statement in [('array(u8,length=1)','data[0]=256'),
                                      ('array(u16,length=1)','data[0]=-1'),
                                      ('array(u8,length=0)','data[0]=1'),
                                      ('array(u8,length=1)','data.fill(1,start=0,stop=2)')]:
            with self.subTest(statement=statement):
                self.client.upload(program(f'''from lightvm import profile,array,u8,u16,step,kernel,rgb
profile("animation/1")
data={declaration}
@step(period_us=20000,first="after_period")
def update():
 {statement}
@kernel
def draw(): pixels.fill(rgb(1,2,3))
'''))
                before=self.frame(0)
                self.assertNotEqual(self.client.command(FRAME,struct.pack('<I',20)).code,0)
                self.assertTrue(self.client.program_parameters()['faulted'])
                self.assertEqual(self.client.snapshot()[1],before)
        self.client.upload(program('''from lightvm import profile,array,u8,init,step,kernel,rgb,complete
profile("animation/1")
data=array(u8,length=0)
fail=False
@init
def setup():
 data.fill(0)
 data.copy_from(data)
@step(period_us=20000,first="after_period")
def update():
 global fail
 fail=True
 complete()
@kernel
def draw():
 pixels.fill(rgb(1,2,3))
 if fail: pixels[count]=rgb(4,5,6)
'''))
        before=self.frame(0)
        self.assertNotEqual(self.client.command(FRAME,struct.pack('<I',20)).code,0)
        state=self.client.program_parameters()
        self.assertTrue(state['faulted']);self.assertFalse(state['completed'])
        self.assertEqual(self.client.snapshot()[1],before)

    def test_stored_activation_settings_and_materialization(self):
        self.client.save_procedure('paint',sample('color'))
        settings={'random_color':False,'color':0x123456}
        self.client.activate('paint',settings=settings)
        before=self.client.snapshot()[1];self.assertEqual(before,bytes([0x12,0x34,0x56])*5)
        generation=self.client.program_parameters()['generation']
        with self.assertRaises(ProtocolError):self.client.activate('paint',settings={'seed':-1})
        self.assertEqual(self.client.program_parameters()['generation'],generation)
        self.assertEqual(self.client.snapshot()[1],before)
        self.client.save_procedure('other',sample('wipe'))
        self.client.delete_procedure('other')
        self.reset()
        self.assertEqual(self.frame(),before)

    def test_bytecode_permissions_are_independently_validated(self):
        raw=bytearray(sample('chase'));regs=struct.unpack_from('<I',raw,8)[0]
        code=96+regs*8
        # Move its persistent STORE_ELEM into render, which has only output permission.
        count=struct.unpack_from('<I',raw,12)[0]
        store=next(bytes(raw[code+i*8:code+(i+1)*8]) for i in range(count) if raw[code+i*8]==34)
        copy=next(i for i in range(count) if raw[code+i*8]==37)
        raw[code+copy*8:code+(copy+1)*8]=store
        with self.assertRaises(ProtocolError):self.client.upload(bytes(raw))
        self.client.upload(sample('chase'));self.assertEqual(len(self.frame()),15)

    def test_zero_time_stamp_and_long_elapsed_difference(self):
        source='''from lightvm import profile,array,stamp,init,step,kernel,rgb,now,elapsed
profile("animation/1")
birth=array(stamp,length=1)
@init
def setup():
 birth[0]=now()
@step(period_us=20000)
def update(): pass
@kernel
def draw():
 pixels.fill(rgb(int(elapsed(birth[0])*100),0,0))
'''
        self.client.upload(program(source))
        self.assertEqual(self.frame(),bytes(15))
        self.assertEqual(self.frame(100),bytes([10,0,0])*5)

if __name__=='__main__':unittest.main()
