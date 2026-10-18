"""A/B seeking, independent compositions, drivers and transactional admission."""
import json
import os
from pathlib import Path
import struct
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP, PLAY, RESET
from lightvm.compiler import compile_source


class TransitionTests(unittest.TestCase):
    def setUp(self):
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', str(ROOT / 'build/ab-host/lightvm'))))
        self.count = 67  # Multiple bounded compositor slices.
        self.client.configure({'schema': 'lightvm.config', 'outputs': [
            {'id': 'lights', 'type': 'ws2812b', 'config': {'pin': 'GP16', 'count': self.count}}]})
        self.save('a', 'pixels.fill(rgb(200,100,20))')
        self.save('b', 'pixels.fill(rgb(20,40,220))')
        self.client.configure_composition([{'id': 'base', 'program': 'a'}], output='lights')
        self.client.configure_composition([{'id': 'base', 'program': 'b'}], output='lights', slot='B')
        self.client.require_ok(self.client.command(STOP, output='lights'))

    def tearDown(self): self.client.close()

    def save(self, name, draw, setup=''):
        source = 'from lightvm import profile,init,render,rgb,setting\nprofile("animation")\n'
        if setup: source += '@init\ndef setup(ctx,runtime):\n' + '\n'.join(' ' + line for line in setup.splitlines()) + '\n'
        source += '@render\ndef draw(ctx,runtime,pixels):\n' + '\n'.join(' ' + line for line in draw.splitlines()) + '\n'
        artifact = compile_source(source)
        self.assertTrue(artifact.success, artifact.diagnostics)
        self.client.save_program(name, artifact.bytecode)

    def frame(self, delta=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', delta), output='lights'))
        return bytes(self.client.pixel_snapshot)

    def set(self, value): return self.client.set_progress(value, output='lights')
    def state(self): return self.client.transition(output='lights')

    def test_direct_seek_endpoints_and_composition_identity(self):
        before = {slot: self.client.program_state(output='lights', slot=slot, layer='base') for slot in ('A', 'B')}
        for progress, rgb in [(0,(200,100,20)), (35,(137,79,90)), (100,(20,40,220)), (35,(137,79,90)), (0,(200,100,20))]:
            self.set(progress)
            self.assertEqual(self.frame(), bytes(rgb) * self.count)
        after = {slot: self.client.program_state(output='lights', slot=slot, layer='base') for slot in ('A', 'B')}
        for slot in before:
            self.assertEqual(after[slot]['generation'], before[slot]['generation'])
            self.assertEqual(after[slot]['program_us'], before[slot]['program_us'])
            self.assertEqual(after[slot]['slot'], slot)

    def test_invalid_manual_set_preserves_driver_and_removed_owner_tokens_stay_stale(self):
        token=self.client.assign_transition_source('sensor',output='lights')['driver']['assignment']
        for value in (-1,101,None,'bad',True,float('inf')):
            with self.assertRaises(ProtocolError): self.set(value)
            self.assertEqual(self.state()['driver']['assignment'],token)
            self.assertEqual(self.state()['driver']['kind'],'source')
        self.client.require_ok(self.client.command(RESET,output='lights'))
        new=self.client.assign_transition_source('sensor',output='lights')['driver']['assignment']
        self.assertNotEqual(token,new)
        self.assertFalse(self.client.update_transition_source('sensor',token,90,output='lights'))
        self.assertEqual(self.state()['progress'],0)

    def test_identical_frames_preserved_with_complementary_weights(self):
        self.client.configure_composition([{'id': 'base', 'program': 'a'}], output='lights', slot='B')
        for p in (0.01,1,35,49.999,50,99.9):
            self.set(p)
            self.assertEqual(self.frame(), bytes([200,100,20]) * self.count)

    def test_effects_seek_deterministically_and_have_exact_endpoints(self):
        for effect in self.client.describe()['transition_effects']:
            if effect == 'custom': continue
            self.client.select_transition_effect(effect, seed=123, output='lights')
            self.set(35); expected = self.frame()
            self.set(80); self.frame()
            self.set(35); self.assertEqual(self.frame(), expected)
            for p, rgb in ((0,(200,100,20)),(100,(20,40,220))):
                self.set(p); self.assertEqual(self.frame(), bytes(rgb) * self.count)
        self.client.select_transition_effect('dip_black', output='lights'); self.set(50)
        self.assertEqual(self.frame(), bytes(self.count*3))
        self.client.select_transition_effect('white_flash', output='lights')
        self.assertEqual(self.frame(), bytes([255])*self.count*3)

    def test_external_assignment_hold_manual_and_replacement(self):
        token = self.client.assign_transition_source('sensor', output='lights')['driver']['assignment']
        for value in (35,80,12.5):
            self.assertTrue(self.client.update_transition_source('sensor', token, value, output='lights'))
            self.assertEqual(self.state()['progress'], value)
        for value in (None,'bad',-1,101,float('nan'),float('inf')):
            self.assertFalse(self.client.update_transition_source('sensor', token, value, output='lights'))
            self.assertEqual(self.state()['progress'], 12.5)
        self.client.hold_transition(output='lights')
        self.assertFalse(self.client.update_transition_source('sensor', token, 99, output='lights'))
        new = self.client.assign_transition_source('sensor', output='lights')['driver']['assignment']
        self.assertNotEqual(new, token)
        self.assertFalse(self.client.update_transition_source('sensor', token, 99, output='lights'))
        self.set(35)
        self.assertFalse(self.client.update_transition_source('sensor', new, 99, output='lights'))
        self.assertEqual(self.state()['progress'],35)

    def test_timed_from_current_reversal_validation_and_hold(self):
        self.set(35)
        self.client.run_transition(100, 250, output='lights')
        time.sleep(.12)
        state = self.state(); self.assertGreater(state['progress'],35); self.assertLess(state['progress'],100)
        for target, duration in ((-1,100),(101,100),(50,-1),(50,float('inf')),(None,10)):
            with self.assertRaises(ProtocolError): self.client.run_transition(target,duration,output='lights')
            self.assertEqual(self.state()['driver']['kind'],'timed')
        time.sleep(.15)
        self.assertEqual(self.state()['progress'],100); self.assertEqual(self.state()['driver']['kind'],'manual')
        self.client.run_transition(0,100,output='lights'); time.sleep(.12)
        self.assertEqual(self.state()['progress'],0)
        self.client.run_transition(100,100,output='lights'); self.set(35); time.sleep(.12)
        self.assertEqual(self.state()['progress'],35)
        self.client.run_transition(100,100,output='lights'); self.client.hold_transition(output='lights')
        held=self.state()['progress']; time.sleep(.12); self.assertEqual(self.state()['progress'],held)
        self.client.run_transition(80,0,output='lights'); self.assertEqual(self.state()['progress'],80)

    def test_repeating_routine_retains_driver_and_validates_interval_before_replacement(self):
        self.set(0)
        state=self.client.run_transition(100,40,output='lights',interval_ms=100)
        self.assertEqual(state['driver']['interval_ms'],100)
        time.sleep(.24)
        state=self.state();self.assertEqual(state['driver']['kind'],'timed')
        token=state['driver']['assignment']
        for interval in (-1,20,None,'bad',float('inf')):
            with self.assertRaises(ProtocolError):self.client.run_transition(100,40,output='lights',interval_ms=interval)
            self.assertEqual(self.state()['driver']['assignment'],token)
        self.client.hold_transition(output='lights');held=self.state()['progress'];time.sleep(.12)
        self.assertEqual(self.state()['progress'],held)

    def test_layered_replacement_failure_retains_peer_driver_and_heap(self):
        self.set(35); expected=self.frame(); state=self.state(); heap=self.client.status_fields(output='lights')['heap']
        self.save('bad','pixels.point(runtime.pixel_count,rgb(0,0,0))')
        heap=self.client.status_fields(output='lights')['heap']
        for layers in ([{'id':'bad','program':'bad'}],[{'id':str(n),'program':'a'} for n in range(9)]):
            with self.assertRaises(ProtocolError): self.client.configure_composition(layers,output='lights',slot='B')
            self.assertEqual(self.frame(),expected)
            self.assertEqual(self.state()['revision'],state['revision'])
            self.assertEqual(self.client.status_fields(output='lights')['heap'],heap)
        a=self.client.program_state(output='lights',slot='A',layer='base')
        self.client.configure_composition([{'id':'lower','program':'b'},{'id':'upper','program':'a','opacity':128}],output='lights',slot='B')
        self.assertEqual(self.client.program_state(output='lights',slot='A',layer='base')['generation'],a['generation'])
        self.assertEqual(self.state()['progress'],35)

    def test_both_compositions_animate_at_fixed_progress_and_pause_independently(self):
        self.save('clock','pixels.fill(rgb(int(runtime.time*100.0)%255,0,0))')
        for slot in ('A','B'): self.client.configure_composition([{'id':'clock','program':'clock'}],output='lights',slot=slot)
        self.set(35); self.client.require_ok(self.client.command(PLAY,output='lights')); time.sleep(.08)
        a=self.client.program_state(output='lights',slot='A',layer='clock')
        b=self.client.program_state(output='lights',slot='B',layer='clock')
        self.assertGreater(a['program_us'],0); self.assertGreater(b['program_us'],0)
        self.client.pause_composition(output='lights',slot='B')
        b=self.client.program_state(output='lights',slot='B',layer='clock'); time.sleep(.08)
        self.assertEqual(self.client.program_state(output='lights',slot='B',layer='clock')['program_us'],b['program_us'])
        self.assertGreater(self.client.program_state(output='lights',slot='A',layer='clock')['program_us'],a['program_us'])
        self.assertEqual(self.state()['progress'],35)

    def test_custom_mask_and_paint_reset_guest_state_before_each_seek(self):
        self.save('mask','ctx.calls+=1\npixels.fill(rgb(int(runtime.time*255.0),0,0))','ctx.calls=0')
        self.save('paint','pixels.fill(rgb(255,255,255),alpha=64)')
        self.client.select_transition_effect('custom',mask={'program':'mask'},paint={'program':'paint'},output='lights')
        self.set(35); expected=self.frame()
        self.set(80); self.frame(); self.set(35); self.assertEqual(self.frame(),expected)
        self.assertEqual(expected[:3],bytes([167,123,131]))
        self.set(0); self.assertEqual(self.frame(),bytes([200,100,20])*self.count)
        self.set(100); self.assertEqual(self.frame(),bytes([20,40,220])*self.count)
        heap=self.client.status_fields(output='lights')['heap']
        for _ in range(10): self.frame()
        self.assertEqual(self.client.status_fields(output='lights')['heap'],heap)

    def test_direct_activation_promotes_a_without_restarting_it(self):
        self.client.configure({'schema':'lightvm.config','outputs':[]})
        self.client.configure({'schema':'lightvm.config','outputs':[{'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':self.count}}]})
        self.client.activate('a',output='lights')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        before=self.client.program_state(output='lights')
        self.client.activate('b',output='lights',slot='B')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        after=self.client.program_state(output='lights',layer='main')
        self.assertEqual(after['generation'],before['generation'])
        self.assertEqual(after['program_us'],before['program_us'])
        self.set(100);self.assertEqual(self.frame(),bytes([20,40,220])*self.count)
        self.client.activate('a',output='lights',slot='A')
        self.assertTrue(self.state()['B']['active'])
        self.assertEqual(self.frame(),bytes([20,40,220])*self.count)
        self.client.clear_composition(output='lights',slot='B')
        self.assertEqual(self.frame(),bytes(self.count*3))
        self.set(0);self.assertEqual(self.frame(),bytes([200,100,20])*self.count)
        self.client.clear_composition(output='lights',slot='A')
        self.assertFalse(self.state()['A']['active'])
        self.assertEqual(self.frame(),bytes(self.count*3))

    def test_custom_seed_is_repeatable_and_failed_selection_keeps_current_effect(self):
        artifact=compile_source('import random\nfrom lightvm import profile,init,render,rgb\nprofile("animation")\n@init\ndef setup(ctx,runtime):\n random.seed()\n@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n  pixels[i]=rgb(random.randrange(256),0,0)\n')
        self.assertTrue(artifact.success,artifact.diagnostics);self.client.save_program('randommask',artifact.bytecode)
        self.client.select_transition_effect('custom',seed=17,mask={'program':'randommask'},output='lights')
        self.set(35);expected=self.frame();self.set(70);self.frame();self.set(35)
        self.assertEqual(self.frame(),expected)
        self.client.select_transition_effect('custom',seed=17,mask={'program':'randommask'},output='lights')
        self.assertEqual(self.frame(),expected)
        self.client.select_transition_effect('custom',seed=18,mask={'program':'randommask'},output='lights')
        self.assertNotEqual(self.frame(),expected)
        expected=self.frame();state=self.state();heap=self.client.status_fields(output='lights')['heap']
        for options in ({'mask':{'program':'missing'}},{'mask':{'program':'randommask','settings':{'missing':1}}}):
            with self.assertRaises(ProtocolError):self.client.select_transition_effect('custom',output='lights',**options)
            self.assertEqual(self.state()['revision'],state['revision']);self.assertEqual(self.frame(),expected)
            self.assertEqual(self.client.status_fields(output='lights')['heap'],heap)

    def test_aggregate_budget_failure_preserves_both_compositions(self):
        source='from array import array\nfrom lightvm import profile,init,render,rgb\nprofile("animation")\n@init\ndef setup(ctx,runtime):\n ctx.data=array("I",[0]*65535)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(255,0,0))\n'
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_program('large',artifact.bytecode)
        self.set(35);expected=self.frame();state=self.state();heap=self.client.status_fields(output='lights')['heap']
        with self.assertRaises(ProtocolError):self.client.configure_composition([{'id':'large','program':'large'}],output='lights',slot='B')
        self.assertEqual(self.frame(),expected);self.assertEqual(self.state()['revision'],state['revision'])
        self.assertEqual(self.client.status_fields(output='lights')['heap'],heap)


if __name__ == '__main__': unittest.main()
