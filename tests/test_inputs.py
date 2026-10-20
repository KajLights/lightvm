"""Live input declarations, drivers and wiring against the actual native engine."""
import json
import os
from pathlib import Path
import struct
import tempfile
import time
import unittest
import zlib

from bytecode_fixtures import ROOT, program, sample
from test_configuration import config
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP
from lightvm.compiler import compile_source
from test_current_contract import manifest_variant

SOURCE = '''from lightvm import profile,init,render,Input,Ramp,rgb
profile("animation")
LEVEL=Input("level",default=0.25,minimum=0.0,maximum=1.0)
PHASE=Input("phase",default=0.0,minimum=0.0,maximum=1.0,driver=Ramp(duration=4.0,mode="repeat"))
@init
def setup(ctx,runtime): ctx.calls=0
@render
def draw(ctx,runtime,pixels):
 ctx.calls+=1
 pixels.fill(rgb(int(runtime.input(LEVEL)*100.0),int(runtime.input(PHASE)*100.0),ctx.calls))
'''


class InputCompilerTests(unittest.TestCase):
    def test_static_declarations_and_target_admission(self):
        compiled=compile_source(SOURCE)
        self.assertTrue(compiled.success,compiled.diagnostics)
        self.assertEqual([item['name'] for item in compiled.manifest['inputs']],['level','phase'])
        self.assertEqual(compiled.bytecode,compile_source(SOURCE).bytecode)
        self.assertFalse(compile_source(SOURCE,target={'version':'1.0.0'}).success)
        self.assertTrue(compile_source(SOURCE,target={'version':'1.0.0','live_inputs':True}).success)
        self.assertTrue(compile_source(SOURCE.replace('default=0.25,minimum=0.0,maximum=1.0','default=0.1,minimum=0.0,maximum=0.1')).success)

    def test_invalid_defaults_domains_and_dynamic_reads(self):
        for source in (
            SOURCE.replace('default=0.25','default="bad"'),
            SOURCE.replace('default=0.25','default=2.0'),
            SOURCE.replace('default=0.25','default=1e100'),
            SOURCE.replace('minimum=0.0','minimum=True'),
            SOURCE.replace('duration=4.0','duration=0.0'),
            SOURCE.replace('mode="repeat"','mode="unknown"'),
            SOURCE.replace('Input("phase"','Input("level"'),
            SOURCE.replace('runtime.input(LEVEL)','runtime.input("level")'),
            SOURCE.replace('ctx.calls=0','ctx.calls=runtime.input(LEVEL)'),
            SOURCE.replace('ctx.calls+=1','LEVEL=0.5'),
        ):
            with self.subTest(source=source): self.assertFalse(compile_source(source).success)


class InputTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.storage=Path(self.temp.name)/'store.bin'
        self.receiver=os.environ.get('LIGHTVM_RECEIVER',ROOT/'build/host/lightvm')
        self.client=Client(ProcessTransport(self.receiver,self.storage));self.client.configure(config(20))

    def tearDown(self): self.client.close();self.temp.cleanup()

    def load(self,source=SOURCE,inputs=None): self.client.upload(program(source),inputs=inputs)

    def frame(self,ms=0):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',ms)))
        return bytes(self.client.pixel_snapshot)

    def test_live_values_keep_generation_clock_and_context(self):
        self.load();self.assertEqual(self.frame(1000)[:3],bytes([25,25,2]))
        before=self.client.program_state()
        self.client.set_inputs({'level':{'value':0.75}})
        self.assertEqual(self.frame()[:3],bytes([75,25,3]))
        after=self.client.program_state()
        self.assertEqual(after['generation'],before['generation']);self.assertEqual(after['program_us'],before['program_us'])
        self.assertEqual(after['settings_values'],{});self.assertEqual(after['inputs_revision'],1)

    def test_ramp_rate_changes_preserve_phase_and_explicit_restart(self):
        self.load();self.frame(1000)
        self.client.set_inputs({'phase':{'driver':{'kind':'ramp','duration':2.0,'mode':'repeat'}}})
        self.assertEqual(self.frame()[:3],bytes([25,25,3]))
        self.assertEqual(self.frame(1000)[1],75)
        self.client.set_inputs({'phase':{'driver':{'kind':'ramp','duration':2.0,'mode':'repeat','restart':True}}})
        self.assertEqual(self.frame()[1],0)
        self.assertEqual(self.frame(1000)[1],50)
        self.client.set_inputs({'phase':{'default':True}});self.assertEqual(self.frame()[1],0)

    def test_scalar_types_and_atomic_rejection(self):
        self.load('''from lightvm import profile,render,Input,rgb,red
profile("animation")
INTEGER=Input("integer",default=2,minimum=0,maximum=100)
BOOLEAN=Input("boolean",default=True)
COLOR=Input("color",default=rgb(3,4,5))
@render
def draw(ctx,runtime,pixels):
 if runtime.input(BOOLEAN):
  pixels.fill(rgb(runtime.input(INTEGER),red(runtime.input(COLOR)),0))
''')
        self.client.set_inputs({'integer':{'value':20},'color':{'value':0x330000}})
        self.assertEqual(self.frame()[:3],bytes([20,51,0]))
        before=self.client.program_state()
        for values in ({'integer':{'value':101}},{'boolean':{'value':1}},{'color':{'value':-1}},
                       {'integer':{'value':30},'color':{'value':True}},{'missing':{'value':0}},
                       {'integer':{'value':20,'source':'x'}},{'integer':{'driver':{'kind':'ramp','duration':1}}}):
            with self.subTest(values=values),self.assertRaises(ProtocolError): self.client.set_inputs(values)
            self.assertEqual(self.client.program_state()['inputs_revision'],before['inputs_revision'])
            self.assertEqual(self.frame()[:3],bytes([20,51,0]))

    def test_stale_generation_and_wiring_revision_reject_delayed_writers(self):
        self.load();before=self.client.program_state();self.client.set_inputs({'level':{'value':0.5}})
        with self.assertRaises(ProtocolError):
            self.client.manage('program_inputs',expected_generation=before['generation'],expected_inputs_revision=before['inputs_revision'],inputs={'level':{'value':0.9}})
        self.client.reset_program()
        with self.assertRaises(ProtocolError):
            self.client.manage('program_inputs',expected_generation=before['generation'],expected_inputs_revision=0,inputs={'level':{'value':0.9}})
        self.assertEqual(self.frame()[0],50)

    def test_sources_publish_atomic_batches_and_reject_stale_packets(self):
        self.load();publisher=self.client.claim_inputs({'test.level':'float','test.phase':'float'})
        self.client.set_inputs({'level':{'source':'test.level'},'phase':{'source':'test.phase'}})
        self.client.publish_inputs(publisher,1,{'test.level':0.6,'test.phase':0.8})
        self.assertEqual(self.frame()[:2],bytes([60,80]))
        for seq,values in ((1,{'test.level':0.1,'test.phase':0.1}),(2,{'test.level':0.1}),
                           (2,{'test.level':0.1,'test.phase':'bad'})):
            with self.assertRaises(ProtocolError): self.client.publish_inputs(publisher,seq,values)
            self.assertEqual(self.frame()[:2],bytes([60,80]))
        replacement=self.client.claim_inputs({'test.level':'float'})
        with self.assertRaises(ProtocolError): self.client.publish_inputs(publisher,3,{'test.level':0.1,'test.phase':0.1})
        with self.assertRaises(ProtocolError): self.client.publish_inputs({**publisher,'generation':0},1,{'test.phase':0.1})
        self.client.publish_inputs(replacement,1,{'test.level':0.2});self.assertEqual(self.frame()[0],20)
        with self.assertRaises(ProtocolError): self.client.publish_inputs({**replacement,'boot_id':'previous-boot'},2,{'test.level':0.9})

    def test_mapping_timeout_and_hold(self):
        self.load();publisher=self.client.claim_inputs({'meter':'int'})
        self.client.set_inputs({'level':{'source':'meter','timeout_ms':100,'map':{'input_min':0,'input_max':100,'output_min':1,'output_max':0}}})
        self.client.publish_inputs(publisher,1,{'meter':20});self.assertEqual(self.frame()[0],80)
        time.sleep(0.15);self.assertEqual(self.frame()[0],25);self.assertTrue(self.client.program_state()['inputs']['level']['fallback'])
        self.client.set_inputs({'level':{'source':'meter','timeout_ms':1,'fallback':'hold','map':{'input_min':0,'input_max':100,'output_min':0,'output_max':1}}})
        self.assertEqual(self.frame()[0],20)
        time.sleep(0.02);self.assertEqual(self.frame()[0],20)

    def test_pause_keeps_clock_and_frame_until_resume_or_explicit_frame(self):
        self.load();self.frame(1000);paused=self.client.pause_program()
        self.client.set_inputs({'level':{'value':0.8}});time.sleep(0.02)
        state=self.client.program_state();self.assertTrue(state['paused'])
        self.assertEqual(state['program_us'],paused['program_us'])
        self.assertEqual(state['render_count'],paused['render_count'])
        self.assertEqual(self.frame()[:2],bytes([80,25]));self.assertTrue(self.client.program_state()['paused'])

    def test_numeric_mapping_preserves_int32_endpoints(self):
        self.load('''from lightvm import profile,render,Input,rgb
profile("animation")
VALUE=Input("value",default=0,minimum=0,maximum=2147483647)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(runtime.input(VALUE)%256,0,0))
''')
        publisher=self.client.claim_inputs({'unit':'float'})
        wiring={'value':{'source':'unit','map':{'input_min':0,'input_max':1,'output_min':0,'output_max':2147483647}}}
        self.client.set_inputs(wiring);self.client.publish_inputs(publisher,1,{'unit':1.0})
        self.assertEqual(self.frame()[0],255)
        state=self.client.program_state();self.assertEqual(state['inputs']['value']['value'],2147483647)
        self.assertEqual(state['inputs']['value']['binding']['map']['output_max'],2147483647)
        self.client.set_inputs({'value':state['inputs']['value']['binding']});self.assertEqual(self.frame()[0],255)

    def test_eye_updates_existing_sprite_and_keeps_resources_fixed(self):
        self.client.upload(sample('eye'),inputs={'position':{'value':0.0}})
        start=self.frame();before=self.client.program_state();heap=self.client.status_fields()['heap']
        self.client.set_inputs({'position':{'value':1.0},'width':{'value':0.3},'color':{'value':0x00ffff},'intensity':{'value':0.5}})
        end=self.frame()
        self.assertNotEqual(start,end);self.assertTrue(any(end[-18:]));self.assertFalse(any(end[:30]));self.assertFalse(any(end[::3]))
        self.assertEqual(self.client.program_state()['generation'],before['generation'])
        self.assertEqual(self.client.status_fields()['heap'],heap)
        self.client.set_inputs({'width':{'value':0.0}});self.assertFalse(any(self.frame()))

    def test_rainbow_phase_wrap_and_spatial_scale_update_live(self):
        self.client.upload(sample('rainbow'),settings={'random_color':False})
        initial=self.frame();generation=self.client.program_state()['generation']
        self.client.set_inputs({'phase':{'value':1.0}});self.assertEqual(self.frame(),initial)
        self.client.set_inputs({'phase':{'value':0.25},'scale':{'value':0.0}});uniform=self.frame()
        self.assertNotEqual(uniform,initial);self.assertEqual(uniform,uniform[:3]*20)
        self.assertEqual(self.client.program_state()['generation'],generation)

    def test_one_source_drives_two_instances_and_transition_progress(self):
        self.client.save_program('inputs',program(SOURCE))
        self.client.configure_composition([{'id':'first','program':'inputs','inputs':{'level':{'source':'shared'}}},
                                           {'id':'second','program':'inputs','inputs':{'level':{'source':'shared'}}}])
        self.client.require_ok(self.client.command(STOP));publisher=self.client.claim_inputs({'shared':'float'})
        state=self.client.transition()
        self.client.manage('transition_bind',expected_assignment=state['driver']['assignment'],binding={'source':'shared'})
        self.client.publish_inputs(publisher,1,{'shared':0.7});self.frame()
        for name in ('first','second'): self.assertAlmostEqual(self.client.program_state(layer=name)['inputs']['level']['value'],0.7,places=6)
        self.assertAlmostEqual(self.client.transition()['progress'],70,places=4)
        self.client.set_inputs({'level':{'value':0.2}},layer='first');self.client.publish_inputs(publisher,2,{'shared':0.4});self.frame()
        self.assertAlmostEqual(self.client.program_state(layer='second')['inputs']['level']['value'],0.4,places=6)
        self.assertAlmostEqual(self.client.program_state(layer='first')['inputs']['level']['value'],0.2,places=6)
        self.client.hold_transition();self.client.publish_inputs(publisher,3,{'shared':0.9})
        self.assertAlmostEqual(self.client.transition()['progress'],40,places=4)

    def test_transition_inputs_survive_baseline_restore_and_seek(self):
        source='''from lightvm import profile,render,Input,rgb
profile("transition")
LEVEL=Input("level",default=0.25,minimum=0.0,maximum=1.0)
@render
def draw(ctx,runtime,pixels): pixels.fill(rgb(int(runtime.input(LEVEL)*100.0),int(runtime.progress*100.0),0))
'''
        self.client.save_program('controlled',program(source));self.client.select_transition('controlled')
        self.client.set_progress(50)
        self.client.set_inputs({'level':{'value':0.8}},target='transition')
        self.assertEqual(self.frame()[:3],bytes([80,50,0]))
        self.assertEqual(self.client.snapshot(target='transition')[1][:4],bytes([80,50,0,255]))
        for progress in (25,75,50): self.client.set_progress(progress);self.assertEqual(self.frame()[:3],bytes([80,progress,0]))
        self.client.set_progress(0);self.assertFalse(any(self.frame()))
        self.client.set_progress(100);self.assertFalse(any(self.frame()))

    def test_tampered_manifest_and_read_sites_are_independently_rejected(self):
        self.load();generation=self.client.program_state()['generation'];raw=program(SOURCE)
        candidates=[manifest_variant(raw,inputs=[]),manifest_variant(raw,inputs=[{'name':'level','type':'int','default':1}]),
                    manifest_variant(raw,inputs=[{'name':'level','type':'float','default':2,'maximum':1}])]
        regs,code,colors,functions,resources,expressions,calls=struct.unpack_from('<7I',raw,8)
        offset=96+regs*8+code*8+colors*4+functions*24+resources*24+expressions*16
        for n in range(calls):
            if struct.unpack_from('<I',raw,offset+n*16)[0]==56:
                tampered=bytearray(raw);tampered[offset+n*16+8]=0;candidates.append(tampered);break
        for candidate in candidates:
            with self.assertRaises(ProtocolError): self.client.save_program('tampered',candidate)
            self.assertEqual(self.client.program_state()['generation'],generation)

    def test_preset_recipes_survive_reboot_and_do_not_save_live_samples(self):
        self.client.configure(config(20),save=True)
        self.client.save_program('eye',sample('eye'))
        wiring={'position':{'value':0.5},'intensity':{'source':'future.level'}}
        self.client.save_preset('wired',[{'id':'eye','program':'eye','crc32':zlib.crc32(sample('eye')),'inputs':wiring}])
        self.client.close();self.client=Client(ProcessTransport(self.receiver,self.storage))
        self.client.activate_preset('wired');self.client.require_ok(self.client.command(STOP));self.frame()
        state=self.client.program_state(layer='eye')
        self.assertEqual(state['inputs']['position']['binding'],{'value':0.5})
        self.assertEqual(state['inputs']['intensity']['binding']['source'],'future.level')
        self.assertTrue(state['inputs']['intensity']['fallback'])
        self.client.manage('input_list');self.assertEqual(json.loads(self.client.data_reply),[])
