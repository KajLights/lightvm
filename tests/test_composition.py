"""Native drawing, alpha semantics and independent composition instances."""
import os
import json
from pathlib import Path
import struct
import sys
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
from client import Client,ProcessTransport,ProtocolError,FRAME,STOP,PLAY,RESET
from lightvm.compiler import compile_source

PREFIX='from lightvm import profile,setting,rgb,init,render,SpriteBatch,Sprite,Transform,ColorTransform,PixelArray\nprofile("animation")\n'

class CompositionTests(unittest.TestCase):
    def setUp(self):
        self.client=Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/composition-host/lightvm'))))
        self.count=17
        self.client.configure({'schema':'lightvm.config','outputs':[
            {'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':self.count}},
            {'id':'second','type':'ws2812b','config':{'pin':'GP17','count':self.count}}]})
    def tearDown(self): self.client.close()
    def save(self,name,draw,setup='',definitions=''):
        source=PREFIX+definitions+('@init\ndef setup(ctx,runtime):\n'+setup if setup else '')+'@render\ndef draw(ctx,runtime,pixels):\n'+draw
        artifact=compile_source(source)
        self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_program(name,artifact.bytecode)
    def composition(self,layers,output='lights'):
        self.client.configure_composition(layers,output=output)
        self.client.require_ok(self.client.command(STOP,output=output))
        for layer in layers:
            self.client.reset_program(output=output,layer=layer['id'])
    def frame(self,delta=0,output='lights'):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',delta),output=output))
        return bytes(self.client.pixel_snapshot)
    def state(self,layer,output='lights'): return self.client.program_state(output=output,layer=layer)
    def layer(self,layer,output='lights'): return self.client.snapshot(output=output,layer=layer)[1]

    def test_transparency_opaque_black_and_exact_endpoints(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('paint',' pixels.fill(ctx.color,alpha=ctx.alpha)\n',' ctx.color=setting(rgb(255,255,255))\n ctx.alpha=setting(0,minimum=0,maximum=255)\n')
        for color,alpha,expected in [(0xffffff,0,(0,0,255)),(0xffffff,128,(128,128,255)),(0,255,(0,0,0)),(0xffffff,255,(255,255,255))]:
            self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'paint','settings':{'color':color,'alpha':alpha}}])
            self.assertEqual(self.frame(),bytes(expected)*self.count)

    def test_overlapping_sprites_apply_group_opacity_once(self):
        self.save('points',' pixels.point(0,rgb(255,0,0),128)\n pixels.point(0,rgb(255,0,0),128)\n')
        self.composition([{'id':'fg','program':'points','opacity':128}])
        self.assertEqual(self.frame(),bytes([96,0,0])+bytes((self.count-1)*3))
        self.assertEqual(self.layer('fg')[:4],bytes([192,0,0,192]))
        self.client.layer_opacity('fg',0,output='lights')
        self.assertEqual(self.frame(),bytes(self.count*3))
        self.client.layer_opacity('fg',255,output='lights')
        self.assertEqual(self.frame()[:3],bytes([192,0,0]))

    def test_layer_blend_modes_alpha_and_opacity(self):
        self.save('base',' pixels.fill(rgb(80,120,160))\n')
        self.save('paint',' pixels.fill(rgb(200,100,50),alpha=ctx.alpha)\n',
                  ' ctx.alpha=setting(128,minimum=0,maximum=255)\n')
        mul=lambda v,a:(v*a+127)//255
        self.assertEqual(self.client.describe()['composition_blend_modes'],['over','add','screen','multiply'])
        for mode in ('over','add','screen','multiply'):
            for alpha in (0,128,255):
                for opacity in (0,128,255):
                    with self.subTest(mode=mode,alpha=alpha,opacity=opacity):
                        self.composition([{'id':'bg','program':'base'},
                                    {'id':'fg','program':'paint','blend':mode,'opacity':opacity,'settings':{'alpha':alpha}}])
                        a=mul(alpha,opacity)
                        expected=[]
                        for lower,channel in zip((80,120,160),(200,100,50)):
                            paint=mul(mul(channel,alpha),opacity)
                            # Source-over now rounds the combined sum once,
                            # retaining both global opacity and pixel alpha.
                            weight=(opacity*65536+127)//255
                            denominator=255*65536
                            over=(mul(channel,alpha)*weight*255+lower*(denominator-alpha*weight)+denominator//2)//denominator
                            value=(over if mode=='over' else
                                   min(255,lower+paint) if mode=='add' else
                                   255-mul(255-lower,255-paint) if mode=='screen' else
                                   mul(lower,255-a+paint))
                            expected.append(value)
                        self.assertEqual(self.frame(),bytes(expected)*self.count)
                        self.assertEqual(self.client.composition(output='lights')['layers'][1]['blend'],mode)

    def test_blend_update_retains_clocks_and_invalid_modes_are_atomic(self):
        self.save('base',' pixels.fill(rgb(100,0,0))\n')
        self.save('paint',' pixels.fill(rgb(100,0,0))\n')
        self.composition([{'id':'bg','program':'base'},{'id':'fg','program':'paint'}])
        self.assertEqual(self.frame()[:3],bytes([100,0,0]))
        before=self.state('fg'); heap=self.client.status_fields()['heap']
        self.client.layer_blend('fg','add',output='lights')
        self.assertEqual(self.frame()[:3],bytes([200,0,0]))
        after=self.state('fg')
        self.assertEqual(after['generation'],before['generation'])
        self.assertEqual(after['program_us'],before['program_us'])
        revision=self.client.composition(output='lights')['revision']
        for mode in ('unknown','',None,True,2):
            with self.subTest(mode=mode),self.assertRaises(ProtocolError):
                self.client.layer_blend('fg',mode,output='lights')
            self.assertEqual(self.client.composition(output='lights')['revision'],revision)
            with self.assertRaises(ProtocolError):
                self.composition([{'id':'fg','program':'paint','blend':mode}])
            self.assertEqual(self.client.composition(output='lights')['revision'],revision)
            self.assertEqual(self.client.status_fields()['heap'],heap)
            self.assertEqual(self.frame()[:3],bytes([200,0,0]))
        with self.assertRaises(ProtocolError):
            self.client.manage('composition_layer_blend',output='lights',layer='fg',blend='over',expected_revision=revision-1)
        self.client.activate('paint',output='lights',layer='fg')
        self.assertEqual(self.client.composition(output='lights')['layers'][1]['blend'],'add')

    def test_native_gradients_palette_and_colorwheel(self):
        self.save('gradient',' pixels.gradient(rgb(255,0,0),rgb(0,0,255))\n')
        self.composition([{'id':'bg','program':'gradient'}])
        values=self.frame()
        self.assertEqual(values[:3],bytes([255,0,0]));self.assertEqual(values[-3:],bytes([0,0,255]))
        for i in range(self.count):
            t=(i*255+(self.count-1)//2)//(self.count-1)
            self.assertEqual(values[i*3:i*3+3],bytes([255-t,0,t]))
        source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.palette=array("I",[int(rgb(255,0,0)),int(rgb(0,0,255))])\n@render\ndef draw(ctx,runtime,pixels):\n pixels.palette(ctx.palette,-0.5,0.5)\n'
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_program('palette',artifact.bytecode)
        self.composition([{'id':'bg','program':'palette'}]);values=self.frame()
        self.assertEqual(values[:12],bytes([128,0,127,255,0,0,127,0,128,0,0,255]))
        self.save('wheel',' pixels.colorwheel(0.0,1.0)\n')
        self.composition([{'id':'bg','program':'wheel'}]);self.assertEqual(self.frame(),b''.join(bytes([255-i*3,i*3,0]) for i in range(self.count)))

    def test_layer_replacement_pause_and_removal_preserve_peer_state(self):
        self.save('clock',' pixels.fill(rgb(int(runtime.time*10.0)%255,0,0))\n')
        self.save('counter',' ctx.calls+=1\n pixels.point(0,rgb(255,255,255),128)\n',' ctx.calls=0\n')
        self.save('blue',' ctx.calls+=1\n pixels.fill(rgb(0,0,255))\n',' ctx.calls=0\n')
        self.composition([{'id':'bg','program':'clock'},{'id':'fg','program':'counter'}])
        self.frame(100)
        before=self.state('fg');pixels=self.layer('fg')
        self.client.activate('blue',output='lights',layer='bg')
        after=self.state('fg')
        for key in ('generation','program_us','render_count'):self.assertEqual(after[key],before[key])
        self.assertEqual(self.layer('fg'),pixels)
        self.client.require_ok(self.client.command(PLAY,output='lights'))
        self.client.pause_program(output='lights',layer='fg');held=self.state('fg');bg=self.state('bg')
        time.sleep(.12)
        self.assertEqual(self.state('fg')['program_us'],held['program_us'])
        self.assertGreater(self.state('bg')['render_count'],bg['render_count'])
        self.assertEqual(self.layer('fg'),pixels)
        self.client.remove_layer('fg',output='lights')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        self.assertEqual(self.frame(),bytes([0,0,255])*self.count)

    def test_failed_replacement_and_failed_composition_keep_output_and_heap(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('invalid',' pixels.fill(rgb(1,2,3),alpha=256)\n')
        self.composition([{'id':'bg','program':'blue'}]);expected=self.frame();before=self.state('bg');heap=self.client.status_fields()['heap']
        with self.assertRaises(ProtocolError):self.client.activate('invalid',output='lights',layer='bg')
        self.assertEqual(self.state('bg')['generation'],before['generation']);self.assertEqual(self.frame(),expected)
        self.assertEqual(self.client.status_fields()['heap'],heap)
        for layers in ([{'id':'bg','program':'blue'},{'id':'bg','program':'blue'}],[{'id':'bad','program':'invalid'}]):
            with self.assertRaises(ProtocolError):self.client.configure_composition(layers,output='lights')
            self.assertEqual(self.state('bg')['generation'],before['generation']);self.assertEqual(self.frame(),expected)
            self.assertEqual(self.client.status_fields()['heap'],heap)

    def test_fault_preserves_last_layer_contribution(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('fault',' pixels.point(0,rgb(255,0,0),128)\n if runtime.delta>0.0: pixels.point(runtime.pixel_count,rgb(0,0,0),255)\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'fault'}]);before=self.frame();layer=self.layer('fg')
        with self.assertRaises(ProtocolError):self.frame(10)
        self.assertTrue(self.state('fg')['faulted']);self.assertEqual(self.layer('fg'),layer)
        # A faulted foreground remains a valid retained layer while its peer renders.
        self.client.require_ok(self.client.command(PLAY,output='lights'));time.sleep(.08)
        self.assertEqual(self.client.composition_snapshot(output='lights')[1],before)

    def test_seeded_sprite_pool_births_expiry_and_native_alpha(self):
        definition = 'def particle_init(s,runtime,position):\n s.position=position\ndef particle_render(s,runtime,pixels):\n if runtime.time>=1.0:\n  s.remove()\n  return\n pixels.fill(rgb(255,255,255))\n s.transform=Transform(translate=s.position)\n s.color=ColorTransform(alpha=1.0-runtime.time)\nParticle=Sprite(width=1,init=particle_init,render=particle_render)\n'
        self.save('sprites',' if runtime.delta>0.0: ctx.stars.spawn(ctx.births%runtime.pixel_count)\n ctx.births+=1\n ctx.stars.render(runtime,pixels)\n',
                  ' ctx.births=0\n ctx.stars=SpriteBatch(Particle,capacity=3)\n',definition)
        self.composition([{'id':'fg','program':'sprites'}])
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
        source=(ROOT/'examples/animations/stars.py').read_text().replace('random.seed()','random.seed(17)')
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_program('stars',artifact.bytecode)
        for output in ('lights','second'):self.composition([{'id':'fg','program':'stars','settings':{'capacity':3,'probability':20.0}}],output)
        for delta in (100,100,250):
            self.frame(delta,'lights');self.frame(delta,'second')
            self.assertEqual(self.layer('fg','lights'),self.layer('fg','second'))
        self.frame(100,'lights')
        self.assertNotEqual(self.state('fg','lights')['program_us'],self.state('fg','second')['program_us'])
        self.assertNotEqual(self.layer('fg','lights'),self.layer('fg','second'))

    def test_one_profile_supports_direct_playback_and_transparent_layers(self):
        target=self.client.describe()
        self.assertTrue(target['compositions'])
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('point',' pixels[0]=rgb(255,0,0)\n')
        self.client.activate('point',output='lights')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        self.assertEqual(self.frame(),bytes([255,0,0])+bytes((self.count-1)*3))
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'point'}])
        self.assertEqual(self.frame(),bytes([255,0,0])+bytes([0,0,255])*(self.count-1))
        self.assertEqual(self.layer('fg'),bytes([255,0,0,255])+bytes((self.count-1)*4))
        source=PREFIX+'@render\ndef draw(ctx,runtime,pixels): pass\n'
        self.assertTrue(compile_source(source,target=target).success)
        old={**target,'version':'0.9.0'}
        self.assertFalse(compile_source(source,target=old).success)
        self.assertFalse(compile_source(source.replace('animation','layer/1')).success)

    def test_frames_start_blank_and_black_paint_is_explicit(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('moving',' pixels[int(runtime.time*10.0)]=rgb(255,0,0)\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'moving'}])
        self.assertEqual(self.frame(),bytes([255,0,0])+bytes([0,0,255])*(self.count-1))
        self.assertEqual(self.frame(100),bytes([0,0,255,255,0,0])+bytes([0,0,255])*(self.count-2))
        self.save('black',' pixels.fill(rgb(0,0,0))\n pixels.clear(start=1,stop=pixels.count)\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'black'}])
        self.assertEqual(self.frame(),bytes(3)+bytes([0,0,255])*(self.count-1))
        self.assertEqual(self.layer('fg'),bytes([0,0,0,255])+bytes((self.count-1)*4))
        self.save('blank',' pixels.fill(rgb(255,0,0))\n pixels.clear()\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'blank'}])
        self.assertEqual(self.frame(),bytes([0,0,255])*self.count)

    def test_partial_transparency_matches_direct_and_single_layer_output(self):
        self.save('fading',' pixels.point(1,rgb(255,0,0),128)\n')
        self.client.activate('fading',output='lights')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        expected=bytes([0,0,0,128,0,0])+bytes((self.count-2)*3)
        self.assertEqual(self.frame(),expected)
        self.assertEqual(self.client.snapshot(output='lights')[1],expected)
        self.composition([{'id':'fg','program':'fading'}])
        self.assertEqual(self.frame(),expected)
        self.assertEqual(self.layer('fg'),bytes(4)+bytes([128,0,0,128])+bytes((self.count-2)*4))

    def test_obsolete_rgb_and_layer_packages_are_rejected(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.composition([{'id':'bg','program':'blue'}])
        source='from lightvm import profile,rgb,render\nprofile("animation")\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(0,0,255))\n'
        artifact=compile_source(source)
        for binding,profile,schema,format in ((1,'animation',5,'rgb8'),(2,'layer/1',6,'rgba8-premul')):
            raw=bytearray(artifact.bytecode)
            size=struct.unpack_from('<I',raw,76)[0]
            manifest={**artifact.manifest,'schema':f'lightvm.program/{schema}','profile':profile,'pixels':{'format':format}}
            metadata=json.dumps(manifest,separators=(',',':')).encode()
            raw[-size:]=metadata
            struct.pack_into('<II',raw,72,binding,len(metadata))
            nregs,ncode,ncolors,nfunctions=struct.unpack_from('<4I',raw,8)
            resources=96+nregs*8+ncode*8+ncolors*4+nfunctions*24
            struct.pack_into('<I',raw,resources,3 if binding==1 else 12)
            with self.assertRaises(ProtocolError):self.client.save_program('obsolete',raw)
            self.assertEqual(self.frame(),bytes([0,0,255])*self.count)

    def test_budget_rejection_and_stale_requests_are_atomic(self):
        self.save('sprite',' ctx.stars.render(runtime,pixels)\n',' ctx.capacity=setting(3,minimum=1)\n ctx.stars=SpriteBatch(Particle,capacity=ctx.capacity)\n','def particle_render(s,runtime,pixels):\n pixels.fill(rgb(255,255,255))\nParticle=Sprite(width=1,render=particle_render)\n')
        self.composition([{'id':'fg','program':'sprite'}]);before=self.state('fg');heap=self.client.status_fields()['heap']
        with self.assertRaises(ProtocolError):self.client.activate('sprite',output='lights',layer='fg',settings={'capacity':65536})
        self.assertEqual(self.state('fg')['generation'],before['generation']);self.assertEqual(self.client.status_fields()['heap'],heap)
        revision=self.client.composition(output='lights')['revision']
        with self.assertRaises(ProtocolError):self.client.manage('composition_layer_opacity',output='lights',layer='fg',opacity=128,expected_revision=revision-1)
        self.assertEqual(self.client.composition(output='lights')['revision'],revision)
        with self.assertRaises(ProtocolError):self.client.program_state(output='lights',layer='missing')

    def test_default_snapshot_is_composed_and_reset_layer_can_be_replaced(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('red',' pixels.point(0,rgb(255,0,0),128)\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'red'}])
        expected=self.frame()
        self.assertEqual(self.client.snapshot(output='lights')[1],expected)
        old=self.state('fg')['generation']
        self.client.reset_program(output='lights',layer='fg')
        self.assertGreater(self.state('fg')['generation'],old)
        self.client.activate('red',output='lights',layer='fg')
        self.assertEqual(self.frame(),expected)

    def test_parent_stop_preserves_clocks_and_removing_first_layer_rebinds_peer(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        self.save('clock',' pixels.point(0,rgb(int(runtime.time*10.0)%255,0,0),255)\n')
        self.composition([{'id':'bg','program':'blue'},{'id':'fg','program':'clock'}])
        self.frame(100)
        before=self.state('fg');time.sleep(.08)
        self.assertEqual(self.state('fg')['program_us'],before['program_us'])
        self.client.require_ok(self.client.command(PLAY,output='lights'));time.sleep(.04)
        self.client.require_ok(self.client.command(STOP,output='lights'))
        stopped=self.state('fg');time.sleep(.08)
        self.assertEqual(self.state('fg')['program_us'],stopped['program_us'])
        self.assertEqual(self.client.snapshot(output='lights')[1],bytes(self.count*3))
        self.client.remove_layer('bg',output='lights')
        self.assertEqual(self.state('fg')['generation'],stopped['generation'])
        self.assertEqual(self.frame(100)[0],int((stopped['program_us']+100000)/100000)%255)
        self.client.require_ok(self.client.command(STOP,output='lights',layer='fg'))
        self.assertEqual(self.layer('fg'),bytes(self.count*4))
        self.client.require_ok(self.client.command(RESET,output='lights',layer='fg'))
        self.client.activate('clock',output='lights',layer='fg')
        self.assertEqual(self.frame(),bytes(self.count*3))

    def test_peak_memory_rejection_retains_composition_and_checks_layer_limit(self):
        self.save('blue',' pixels.fill(rgb(0,0,255))\n')
        source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.data=array("I",[0]*65535)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.fill(rgb(255,0,0))\n'
        artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
        self.client.save_program('large',artifact.bytecode)
        self.composition([{'id':'bg','program':'blue'}]);expected=self.frame();heap=self.client.status_fields()['heap']
        for layers in ([{'id':'bg','program':'large'}],[{'id':f'l{i}','program':'blue'} for i in range(9)]):
            with self.assertRaises(ProtocolError):self.client.configure_composition(layers,output='lights')
            self.assertEqual(self.frame(),expected);self.assertEqual(self.client.status_fields()['heap'],heap)

    def test_native_resource_kinds_are_validated_before_execution(self):
        for setup,draw in [(' ctx.data=PixelArray(1)\n',' pixels.palette(ctx.data,0.0,1.0)\n')]:
            source='from array import array\n'+PREFIX+'@init\ndef setup(ctx,runtime):\n'+setup+'@render\ndef draw(ctx,runtime,pixels):\n'+draw
            artifact=compile_source(source);self.assertTrue(artifact.success,artifact.diagnostics)
            heap=self.client.status_fields()['heap']
            with self.assertRaises(ProtocolError):self.client.save_program('invalid',artifact.bytecode)
            self.assertEqual(self.client.status_fields()['heap'],heap)

if __name__=='__main__':unittest.main()
