"""Output light state and source-independent animation bindings on the real engine."""
import json
import os
import struct
import tempfile
import time
import unittest
import zlib
from pathlib import Path
from bytecode_fixtures import ROOT, program, sample
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP
from lightvm.compiler import compile_source
from test_configuration import config

SOURCE = '''from lightvm import profile, render, Input, rgb, ColorTransform
profile("animation")
COLOR=Input("color",default=rgb(255,0,0),control="color")
LEVEL=Input("intensity",default=1.0,minimum=0.0,maximum=1.0)
@render
def draw(ctx,runtime,pixels):
 pixels.fill(runtime.input(COLOR))
 pixels.transform(ColorTransform(brightness=runtime.input(LEVEL)))
'''

class LightControlTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.storage=Path(self.temp.name)/'store.bin'
        self.receiver=os.environ.get('LIGHTVM_RECEIVER',ROOT/'build/host/lightvm')
        self.client=Client(ProcessTransport(self.receiver,self.storage))
        self.config=config(20);self.client.configure(self.config)
    def tearDown(self): self.client.close();self.temp.cleanup()
    def frame(self):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',0)))
        return bytes(self.client.pixel_snapshot)
    def preset(self,name='eye'):
        bytecode=program(SOURCE);self.client.save_program('eye',bytecode)
        self.client.save_preset(name,[{'id':'eye','program':'eye','crc32':zlib.crc32(bytecode)}])
    def test_controls_are_relative_latched_and_preserve_generation(self):
        self.client.upload(program(SOURCE));before=self.client.program_state()
        self.client.set_light(color=0x804020,brightness=0.3)
        self.assertEqual(self.frame()[:3],bytes([128,64,32])) # scene snapshot is before master dimming
        state=self.client.program_state();self.assertEqual(state['generation'],before['generation'])
        self.assertEqual(state['inputs']['color']['binding'],{'control':'color'})
        time.sleep(1.05);self.assertEqual(self.frame()[:3],bytes([128,64,32]))
        self.assertFalse(self.client.program_state()['inputs']['color']['fallback'])
        self.client.set_inputs({'color':{'value':0xff00ff}});self.client.set_light(color=0x00ff00)
        self.assertEqual(self.frame()[:3],bytes([255,0,255]))
        self.client.set_inputs({'color':{'default':True}});self.assertEqual(self.frame()[:3],bytes([0,255,0]))
    def test_music_and_light_controls_are_independent_and_palette_is_explicit(self):
        self.client.upload(program(SOURCE));publisher=self.client.claim_inputs({'music.level':'float','music.bass':'float'})
        self.client.set_inputs({'intensity':{'source':'music.level'},'color':{'source':'music.bass','palette':{'input_min':0,'input_max':1,'from':0xff0000,'to':0x0000ff}}})
        self.client.publish_inputs(publisher,1,{'music.level':0.5,'music.bass':0.5})
        self.assertEqual(self.frame()[:3],bytes([64,0,64]))
        self.client.set_light(color=0xffffff,brightness=0.2)
        self.assertEqual(self.frame()[:3],bytes([64,0,64]))
        self.assertFalse(self.client.light_state()['color_control'])
        with self.assertRaises(ProtocolError):self.client.set_inputs({'color':{'source':'music.level'}})
        self.assertEqual(self.frame()[:3],bytes([64,0,64]))
    def test_palette_rejects_a_publisher_reclaimed_with_an_incompatible_type(self):
        self.client.upload(program(SOURCE))
        numeric=self.client.claim_inputs({'sensor':'float'})
        self.client.set_inputs({'color':{'source':'sensor','timeout_ms':10,'palette':{'input_min':0,'input_max':1,'from':0,'to':0xffffff}}})
        self.client.publish_inputs(numeric,1,{'sensor':0.5})
        self.assertEqual(self.frame()[:3],bytes([128,128,128]))
        rgb=self.client.claim_inputs({'sensor':'rgb8'})
        self.client.publish_inputs(rgb,1,{'sensor':0x00ff00})
        time.sleep(.02)
        self.assertEqual(self.frame()[:3],bytes([255,0,0]))
        self.assertTrue(self.client.program_state()['inputs']['color']['fallback'])
    def test_combined_preset_and_controls_reject_atomically(self):
        self.preset();self.client.set_light(preset='eye',color=0x123456,brightness=0.4)
        self.client.require_ok(self.client.command(STOP));before=self.client.light_state()
        for fields in ({'brightness':1.1},{'color':-1},{'preset':'missing','color':0xff0000},{'power':True,'preset':'missing','brightness':0.9}):
            with self.subTest(fields=fields),self.assertRaises(ProtocolError):self.client.set_light(**fields)
            self.assertEqual(self.client.light_state(),before)
        self.client.set_light(preset='eye',color=0x123456,brightness=0.4)
        self.assertEqual(self.frame()[:3],bytes([18,52,86]))
    def test_control_bindings_are_typed_and_not_claimable(self):
        self.client.upload(program(SOURCE))
        for inputs in ({'color':{'control':'brightness'}},{'intensity':{'control':'color'}},{'color':{'control':'color','value':1}}):
            with self.subTest(inputs=inputs),self.assertRaises(ProtocolError): self.client.set_inputs(inputs)
        with self.assertRaises(ProtocolError): self.client.claim_inputs({'light.color':'rgb8'})
        for i in range(16): self.client.claim_inputs({f'signal{i}':'float'})
        self.client.set_light(color=0x345678);self.assertEqual(self.frame()[:3],bytes([52,86,120]))
    def test_off_edits_do_not_start_playback_and_restore_last_keeps_controls(self):
        self.preset();self.config['outputs'][0]['startup']={'mode':'restore_last'};self.client.configure(self.config,save=True)
        self.client.set_light(preset='eye',color=0x123456,brightness=0.25)
        self.client.set_light(power=False)
        self.client.set_light(color=0x456789,brightness=0.5)
        self.assertFalse(self.client.light_state()['power']);time.sleep(0.9)
        self.client.close();self.client=Client(ProcessTransport(self.receiver,self.storage))
        state=self.client.light_state();self.assertFalse(state['power']);self.assertEqual(state['color'],0x456789);self.assertEqual(state['brightness'],0.5)
        self.client.set_light(power=True);self.assertEqual(self.frame()[:3],bytes([69,103,137]))
    def test_pause_and_input_edits_do_not_change_logical_power(self):
        self.client.upload(program(SOURCE))
        self.frame()
        self.client.pause_program()
        self.assertTrue(self.client.light_state()['power'])
        before=self.client.program_state()
        self.client.set_light(brightness=0.3)
        self.assertEqual(self.client.program_state()['program_us'],before['program_us'])
        self.preset()
        self.client.set_light(preset='eye')
        self.client.set_light(power=False)
        self.client.set_inputs({'intensity':{'value':0.0}},slot='A',layer='eye')
        time.sleep(.03)
        self.assertFalse(self.client.light_state()['power'])
    def test_defaults_are_verified_and_have_a_feature_floor(self):
        compiled=compile_source(SOURCE);self.assertTrue(compiled.success,compiled.diagnostics);self.assertEqual(compiled.manifest['requires'],'1.1.0')
        for source in (SOURCE.replace('control="color"','control="brightness"'),SOURCE.replace('control="color"','control="unknown"')):
            self.assertFalse(compile_source(source).success)
        self.assertFalse(compile_source(SOURCE,target={'version':'1.0.0','live_inputs':True}).success)
        self.assertTrue(compile_source(SOURCE,target={'version':'1.1.0','live_inputs':True,'light_controls':True}).success)
