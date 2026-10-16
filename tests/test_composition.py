"""Native drawing, alpha semantics and independent scene instances."""
import os
from pathlib import Path
import struct
import sys
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
from client import Client,ProcessTransport,ProtocolError,FRAME,STOP,PLAY,RESET
from lightvm.compiler import compile_source

PREFIX='from lightvm import profile,setting,rgb,init,render,SpriteBatch\nprofile("layer/1")\n'

class CompositionTests(unittest.TestCase):
    def setUp(self):
        self.client=Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/composition-host/lightvm'))))
        self.count=17
        self.client.configure({'schema':'lightvm.config/1','devices':[
            {'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':self.count}},
            {'id':'second','type':'ws2812b','config':{'pin':'GP17','count':self.count}}]})
    def tearDown(self): self.client.close()
    def save(self,name,draw,setup=''):
        source=PREFIX+('@init\ndef setup(ctx,runtime):\n'+setup if setup else '')+'@render\ndef draw(ctx,runtime,pixels):\n'+draw
        artifact=compile_source(source)
        self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_procedure(name,artifact.bytecode)
    def scene(self,layers,device='lights'):
        self.client.configure_scene(layers,device=device)
        self.client.require_ok(self.client.command(STOP,device=device))
    def frame(self,delta=0,device='lights'):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',delta),device=device))
        return bytes(self.client.pixel_snapshot)
    def state(self,layer,device='lights'): return self.client.program_parameters(device=device,layer=layer)
    def layer(self,layer,device='lights'): return self.client.snapshot(device=device,layer=layer)[1]

    def test_transparency_opaque_black_and_exact_endpoints(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('paint',' pixels.fill(ctx.color,alpha=ctx.alpha)\n',' ctx.color=setting(rgb(255,255,255))\n ctx.alpha=setting(0,minimum=0,maximum=255)\n')
        for color,alpha,expected in [(0xffffff,0,(0,0,255)),(0xffffff,128,(128,128,255)),(0,255,(0,0,0)),(0xffffff,255,(255,255,255))]:
            self.scene([{'id':'bg','procedure':'blue'},{'id':'fg','procedure':'paint','settings':{'color':color,'alpha':alpha}}])
            self.assertEqual(self.frame(),bytes(expected)*self.count)

    def test_overlapping_sprites_apply_group_opacity_once(self):
        self.save('points',' pixels.point(0,rgb(255,0,0),128)\n pixels.point(0,rgb(255,0,0),128)\n')
        self.scene([{'id':'fg','procedure':'points','opacity':128}])
        self.assertEqual(self.frame(),bytes([96,0,0])+bytes((self.count-1)*3))
        self.assertEqual(self.layer('fg')[:4],bytes([192,0,0,192]))
        self.client.layer_opacity('fg',0,device='lights')
        self.assertEqual(self.frame(),bytes(self.count*3))
        self.client.layer_opacity('fg',255,device='lights')
        self.assertEqual(self.frame()[:3],bytes([192,0,0]))

    def test_native_gradients_palette_and_colorwheel(self):
        self.save('gradient',' pixels.gradient(rgb(255,0,0),rgb(0,0,255))\n')
        self.scene([{'id':'bg','procedure':'gradient'}])
        values=self.frame()
        self.assertEqual(values[:3],bytes([255,0,0]));self.assertEqual(values[-3:],bytes([0,0,255]))
        for i in range(self.count):
            t=(i*255+(self.count-1)//2)//(self.count-1)
            self.assertEqual(values[i*3:i*3+3],bytes([255-t,0,t]))
        source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.palette=array("I",[int(rgb(255,0,0)),int(rgb(0,0,255))])\n@render\ndef draw(ctx,runtime,pixels):\n pixels.palette(ctx.palette,-0.5,0.5)\n'
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_procedure('palette',artifact.bytecode)
        self.scene([{'id':'bg','procedure':'palette'}]);values=self.frame()
        self.assertEqual(values[:12],bytes([128,0,127,255,0,0,127,0,128,0,0,255]))
        self.save('wheel',' pixels.colorwheel(0.0,1.0)\n')
        self.scene([{'id':'bg','procedure':'wheel'}]);self.assertEqual(self.frame(),b''.join(bytes([255-i*3,i*3,0]) for i in range(self.count)))

    def test_layer_replacement_pause_and_removal_preserve_peer_state(self):
        self.save('clock',' pixels.fill(rgb(int(runtime.time*10.0)%255,0,0))\n')
        self.save('counter',' ctx.calls+=1\n pixels.point(0,rgb(255,255,255),128)\n',' ctx.calls=0\n')
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.scene([{'id':'bg','procedure':'clock'},{'id':'fg','procedure':'counter'}])
        self.frame(100)
        before=self.state('fg');pixels=self.layer('fg')
        self.client.activate('blue',device='lights',layer='bg')
        after=self.state('fg')
        for key in ('generation','program_us','render_count'):self.assertEqual(after[key],before[key])
        self.assertEqual(self.layer('fg'),pixels)
        self.client.require_ok(self.client.command(PLAY,device='lights'))
        self.client.pause_program(device='lights',layer='fg');held=self.state('fg');bg=self.state('bg')
        time.sleep(.12)
        self.assertEqual(self.state('fg')['program_us'],held['program_us'])
        self.assertGreater(self.state('bg')['render_count'],bg['render_count'])
        self.assertEqual(self.layer('fg'),pixels)
        self.client.remove_layer('fg',device='lights')
        self.client.require_ok(self.client.command(STOP,device='lights'))
        self.assertEqual(self.frame(),bytes([0,0,255])*self.count)

    def test_failed_replacement_and_failed_scene_keep_output_and_heap(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('invalid',' pixels.fill(rgb(1,2,3),alpha=256)\n')
        self.scene([{'id':'bg','procedure':'blue'}]);expected=self.frame();before=self.state('bg');heap=self.client.status_fields()['heap']
        with self.assertRaises(ProtocolError):self.client.activate('invalid',device='lights',layer='bg')
        self.assertEqual(self.state('bg')['generation'],before['generation']);self.assertEqual(self.frame(),expected)
        self.assertEqual(self.client.status_fields()['heap'],heap)
        for layers in ([{'id':'bg','procedure':'blue'},{'id':'bg','procedure':'blue'}],[{'id':'bad','procedure':'invalid'}]):
            with self.assertRaises(ProtocolError):self.client.configure_scene(layers,device='lights')
            self.assertEqual(self.state('bg')['generation'],before['generation']);self.assertEqual(self.frame(),expected)
            self.assertEqual(self.client.status_fields()['heap'],heap)

    def test_fault_preserves_last_layer_contribution(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('fault',' pixels.point(0,rgb(255,0,0),128)\n if runtime.delta>0.0: pixels.point(runtime.pixel_count,rgb(0,0,0),255)\n')
        self.scene([{'id':'bg','procedure':'blue'},{'id':'fg','procedure':'fault'}]);before=self.frame();layer=self.layer('fg')
        with self.assertRaises(ProtocolError):self.frame(10)
        self.assertTrue(self.state('fg')['faulted']);self.assertEqual(self.layer('fg'),layer)
        # A faulted foreground remains a valid retained layer while its peer renders.
        self.client.require_ok(self.client.command(PLAY,device='lights'));time.sleep(.08)
        self.assertEqual(self.client.scene_snapshot(device='lights')[1],before)

    def test_seeded_sprite_pool_births_expiry_and_native_alpha(self):
        self.save('sprites',' ctx.stars.advance(runtime.delta)\n if runtime.delta>0.0: ctx.stars.spawn(rgb(255,255,255),1.0)\n pixels.sprites(ctx.stars)\n',
                  ' ctx.stars=SpriteBatch(3)\n')
        self.scene([{'id':'fg','procedure':'sprites'}])
        self.frame(100);first=self.layer('fg')
        self.assertEqual(sum(first[i+3]>0 for i in range(0,len(first),4)),1)
        self.frame(100);second=self.layer('fg')
        self.assertEqual(sum(second[i+3]>0 for i in range(0,len(second),4)),2)
        for i in range(0,len(first),4):
            if first[i+3]:
                self.assertLess(second[i+3],first[i+3])
                self.assertEqual(second[i:i+4],bytes([second[i+3]])*4)
        for _ in range(20):self.frame(100)
        self.assertLessEqual(sum(self.layer('fg')[i+3]>0 for i in range(0,self.count*4,4)),3)
        initial=self.client.status_fields()['heap']
        for _ in range(30):self.frame(1000)
        self.assertEqual(self.client.status_fields()['heap'],initial)
        self.assertGreater(self.state('fg')['dispatch_count'],0)

    def test_identical_sprite_program_has_independent_state_on_two_devices(self):
        source=(ROOT/'examples/layers/stars.py').read_text().replace('random.seed()','random.seed(17)')
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_procedure('stars',artifact.bytecode)
        for device in ('lights','second'):self.scene([{'id':'fg','procedure':'stars','settings':{'capacity':3,'probability':20.0}}],device)
        for delta in (100,100,250):
            self.frame(delta,'lights');self.frame(delta,'second')
            self.assertEqual(self.layer('fg','lights'),self.layer('fg','second'))
        self.frame(100,'lights')
        self.assertNotEqual(self.state('fg','lights')['program_us'],self.state('fg','second')['program_us'])
        self.assertNotEqual(self.layer('fg','lights'),self.layer('fg','second'))

    def test_rgb_programs_remain_opaque_and_transparent_profile_is_negotiated(self):
        source='from lightvm import profile,rgb,render\nprofile("animation/1")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(0,0,255))\n'
        artifact=compile_source(source);self.client.save_procedure('legacy',artifact.bytecode)
        self.save('point',' pixels.point(0,rgb(255,255,255),128)\n')
        self.scene([{'id':'bg','procedure':'legacy'},{'id':'fg','procedure':'point'}])
        self.assertEqual(self.frame(),bytes([128,128,255])+bytes([0,0,255])*(self.count-1))
        source=PREFIX+'@render\ndef draw(ctx,runtime,pixels): pass\n'
        target=self.client.describe();self.assertTrue(compile_source(source,target=target).success)
        self.assertFalse(compile_source(source,target={**target,'program_profiles':['animation/1']}).success)
        self.client.clear_scene(device='lights')
        self.client.activate('legacy',device='lights');self.assertEqual(self.client.status_fields()['mode'],'bytecode')

    def test_budget_rejection_and_stale_requests_are_atomic(self):
        self.save('sprite',' pixels.sprites(ctx.stars)\n',' ctx.capacity=setting(3,minimum=1)\n ctx.stars=SpriteBatch(ctx.capacity)\n')
        self.scene([{'id':'fg','procedure':'sprite'}]);before=self.state('fg');heap=self.client.status_fields()['heap']
        with self.assertRaises(ProtocolError):self.client.activate('sprite',device='lights',layer='fg',settings={'capacity':self.count+1})
        self.assertEqual(self.state('fg')['generation'],before['generation']);self.assertEqual(self.client.status_fields()['heap'],heap)
        revision=self.client.scene(device='lights')['revision']
        with self.assertRaises(ProtocolError):self.client.manage('scene_layer_opacity',device='lights',layer='fg',opacity=128,expected_revision=revision-1)
        self.assertEqual(self.client.scene(device='lights')['revision'],revision)
        with self.assertRaises(ProtocolError):self.client.program_parameters(device='lights',layer='missing')

    def test_default_snapshot_is_composed_and_reset_layer_can_be_replaced(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('red',' pixels.point(0,rgb(255,0,0),128)\n')
        self.scene([{'id':'bg','procedure':'blue'},{'id':'fg','procedure':'red'}])
        expected=self.frame()
        self.assertEqual(self.client.snapshot(device='lights')[1],expected)
        old=self.state('fg')['generation']
        self.client.reset_program(device='lights',layer='fg')
        self.assertGreater(self.state('fg')['generation'],old)
        self.client.activate('red',device='lights',layer='fg')
        self.assertEqual(self.frame(),expected)

    def test_parent_stop_preserves_clocks_and_removing_first_layer_rebinds_peer(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('clock',' pixels.point(0,rgb(int(runtime.time*10.0)%255,0,0),255)\n')
        self.scene([{'id':'bg','procedure':'blue'},{'id':'fg','procedure':'clock'}])
        self.frame(100)
        before=self.state('fg');time.sleep(.08)
        self.assertEqual(self.state('fg')['program_us'],before['program_us'])
        self.client.require_ok(self.client.command(PLAY,device='lights'));time.sleep(.04)
        self.client.require_ok(self.client.command(STOP,device='lights'))
        stopped=self.state('fg');time.sleep(.08)
        self.assertEqual(self.state('fg')['program_us'],stopped['program_us'])
        self.assertEqual(self.client.snapshot(device='lights')[1],bytes(self.count*3))
        self.client.remove_layer('bg',device='lights')
        self.assertEqual(self.state('fg')['generation'],stopped['generation'])
        self.assertEqual(self.frame(100)[0],int((stopped['program_us']+100000)/100000)%255)
        self.client.require_ok(self.client.command(STOP,device='lights',layer='fg'))
        self.assertEqual(self.layer('fg'),bytes(self.count*4))
        self.client.require_ok(self.client.command(RESET,device='lights',layer='fg'))
        self.client.activate('clock',device='lights',layer='fg')
        self.assertEqual(self.frame(),bytes(self.count*3))

    def test_peak_memory_rejection_retains_scene_and_checks_layer_limit(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.data=array("I",[0]*65535)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(255,0,0))\n'
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_procedure('large',artifact.bytecode)
        self.scene([{'id':'bg','procedure':'blue'}]);expected=self.frame();heap=self.client.status_fields()['heap']
        for layers in ([{'id':'bg','procedure':'large'}],[{'id':f'l{i}','procedure':'blue'} for i in range(9)]):
            with self.assertRaises(ProtocolError):self.client.configure_scene(layers,device='lights')
            self.assertEqual(self.frame(),expected);self.assertEqual(self.client.status_fields()['heap'],heap)

    def test_native_resource_kinds_are_validated_before_execution(self):
        for setup,draw in [(' ctx.data=array("I",[0])\n',' pixels.sprites(ctx.data)\n'),
                           (' ctx.data=SpriteBatch(1)\n',' pixels.palette(ctx.data,0.0,1.0)\n')]:
            source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n'+setup+'@render\ndef draw(ctx,runtime,pixels):\n'+draw
            artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
            heap=self.client.status_fields()['heap']
            with self.assertRaises(ProtocolError):self.client.save_procedure('invalid',artifact.bytecode)
            self.assertEqual(self.client.status_fields()['heap'],heap)

if __name__=='__main__':unittest.main()
