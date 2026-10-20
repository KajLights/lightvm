"""Executable SDK contracts for straight images and user-defined sprites."""
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP
from lightvm.compiler import compile_source

PREFIX = '''from lightvm import profile, init, render, rgb, setting, PixelArray, Sprite, SpriteBatch, Transform, ColorTransform
profile("animation")
'''


class PixelArrayTests(unittest.TestCase):
    def setUp(self):
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', str(ROOT/'build/composition-host/lightvm'))))
        self.client.configure({'schema':'lightvm.config', 'outputs':[
            {'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':67}}]})

    def tearDown(self):
        self.client.close()

    def load(self, source):
        result = compile_source(PREFIX + source)
        self.assertTrue(result.success, result.diagnostics)
        self.client.save_program('test', result.bytecode)
        self.client.configure_composition([{'id':'fg','program':'test'}], output='lights')
        self.client.require_ok(self.client.command(STOP, output='lights'))
        self.client.reset_program(output='lights', layer='fg')
        return result

    def frame(self, delta=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', delta), output='lights'))
        return self.client.snapshot(output='lights', layer='fg')[1]

    def test_repeated_colour_transform_matches_individual_pixels(self):
        self.load('''@init
def setup(ctx,runtime):
 ctx.scalar=setting(False)
 ctx.level=setting(0.047058823529411764)
 ctx.alpha=setting(0.73)
 ctx.hue=setting(0.0)
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  pixels[i]=rgb((i//16)*40,87,211)
 pixels.set_alpha(0.5,start=17,stop=49)
 style=ColorTransform(brightness=ctx.level,alpha=ctx.alpha,tint=rgb(197,113,251),hue=ctx.hue)
 if ctx.scalar:
  for i in range(pixels.count):
   pixels.transform(style,start=i,stop=i+1)
 else:
  pixels.transform(style)
''')
        for level, alpha, hue in ((0.047058823529411764, .73, 0), (1, 1, 35), (.617, .31, -95)):
            frames = []
            for scalar in (False, True):
                self.client.reset_program(output='lights', layer='fg', settings={
                    'scalar': scalar, 'level': level, 'alpha': alpha, 'hue': hue})
                frames.append(self.frame())
            self.assertEqual(*frames)

    def test_absolute_alpha_ranges_zero_restoration_and_scaling(self):
        self.load('''@init
def setup(ctx,runtime):
 ctx.image=PixelArray([100,40,20,255,200,60,30,128,80,20,10,255],format="rgba8")
@render
def draw(ctx,runtime,pixels):
 ctx.image.set_alpha(0.0)
 ctx.image.set_alpha(1.0)
 ctx.image.set_alpha(0.5,start=1,stop=2)
 ctx.image.set_alpha(0.5,start=1,stop=2)
 ctx.image.multiply_alpha(0.5,start=2,stop=3)
 pixels.draw(ctx.image)
''')
        expected = bytes([100,40,20,255,100,30,15,128,40,10,5,128])
        self.assertEqual(self.frame()[:12], expected)
        self.assertEqual(self.frame()[:12], expected)

    def test_channel_ranges_and_capabilities_for_all_formats(self):
        for format,raw,has_white,has_alpha in [
            ('rgb8',[10,20,30],False,False),('rgba8',[10,20,30,255],False,True),
            ('rgbw8',[10,20,30,45],True,False),('rgbwa8',[10,20,30,45,255],True,True)]:
            with self.subTest(format=format):
                self.load(f'''@init
def setup(ctx,runtime):
 ctx.image=PixelArray({raw},format="{format}")
@render
def draw(ctx,runtime,pixels):
 if ctx.image.has_channel("w"):
  ctx.image.set_channel("w",99)
  ctx.image.set_channel("r",99)
 else:
  ctx.image.set_channel("r",11)
 if ctx.image.has_channel("a"):
  ctx.image.set_channel("a",128)
 pixels.draw(ctx.image)
''')
                alpha = 128 if has_alpha else 255
                red = 99 if has_white else 11
                mul = lambda v: (v*alpha+127)//255
                self.assertEqual(self.frame()[:4], bytes([mul(red),mul(20),mul(30),alpha]))
        self.load('''@init
def setup(ctx,runtime):
 ctx.image=PixelArray([1,2,3,4,5,6,7,8,9],format="rgb8")
@render
def draw(ctx,runtime,pixels):
 ctx.image.set_channel("g",77,start=1,stop=2)
 pixels.draw(ctx.image)
''')
        self.assertEqual(self.frame()[:12],bytes([1,2,3,255,4,77,6,255,7,8,9,255]))

    def test_absent_channels_invalid_ranges_and_readonly_are_strict(self):
        for operation in ('ctx.image.set_channel("w",1)', 'ctx.image.set_alpha(0.5)',
                          'ctx.image.set_channel("r",256)', 'ctx.image.set_channel("r",1,start=-1)',
                          'ctx.image.set_channel("r",1,start=1,stop=0)'):
            source=PREFIX+'''@init
def setup(ctx,runtime):
 ctx.image=PixelArray([10,20,30],format="rgb8")
@render
def draw(ctx,runtime,pixels):
 '''+operation+'\n'
            result=compile_source(source)
            self.assertTrue(result.success,result.diagnostics)
            self.client.save_program('invalid',result.bytecode)
            with self.assertRaises(ProtocolError):
                self.client.configure_composition([{'id':'fg','program':'invalid'}],output='lights')
        for method in ('set_alpha(0.5)','set_channel("r",1)','fill(rgb(0,0,0))','clear()'):
            result=compile_source(PREFIX+'ART=PixelArray([255,0,0,255],readonly=True)\n@render\ndef draw(ctx,runtime,pixels):\n ART.'+method+'\n')
            self.assertFalse(result.success)

    def test_clear_removes_paint_from_persistent_images(self):
        self.load('''@init
def setup(ctx,runtime):
 ctx.image=PixelArray(67)
@render
def draw(ctx,runtime,pixels):
 ctx.image.clear()
 ctx.image[int(runtime.time*10.0)]=rgb(255,0,0)
 pixels.draw(ctx.image)
''')
        self.assertEqual(self.frame(),bytes([255,0,0,255])+bytes(66*4))
        self.assertEqual(self.frame(100),bytes(4)+bytes([255,0,0,255])+bytes(65*4))

    def test_clear_resets_all_channels_and_checks_ranges(self):
        for format,stride in (('rgb8',3),('rgba8',4),('rgbw8',4),('rgbwa8',5)):
            with self.subTest(format=format):
                data=[255]*(3*stride)
                self.load(f'''@init
def setup(ctx,runtime):
 ctx.image=PixelArray({data},format="{format}")
@render
def draw(ctx,runtime,pixels):
 ctx.image.clear(start=1,stop=2)
 pixels.draw(ctx.image)
''')
                middle=bytes([0,0,0,0 if 'a' in format else 255])
                self.assertEqual(self.frame()[:12],bytes([255]*4)+middle+bytes([255]*4))
        for args in ('start=-1','start=2,stop=1','stop=68'):
            result=compile_source(PREFIX+'@render\ndef draw(ctx,runtime,pixels):\n pixels.clear('+args+')\n')
            self.assertTrue(result.success,result.diagnostics)
            self.client.save_program('invalid_clear',result.bytecode)
            with self.assertRaises(ProtocolError):
                self.client.configure_composition([{'id':'fg','program':'invalid_clear'}],output='lights')

    def test_draw_clipping_fractional_sampling_scale_origin_and_tint(self):
        self.load('''ART=PixelArray([255,255,255,255],readonly=True)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ART,transform=Transform(translate=0.5),color=ColorTransform(tint=rgb(255,0,0)))
 pixels.draw(ART,transform=Transform(translate=4.0,scale=2.0),sampling="nearest")
 pixels.draw(ART,transform=Transform(translate=-100.0))
''')
        result=self.frame()
        self.assertEqual(result[:8], bytes([128,0,0,128])*2)
        self.assertEqual(result[12:20],bytes([255,255,255,255])*2)
        self.assertEqual(result[20:24],bytes(4))
        self.load('''ART=PixelArray([64,64,64,255,255,255,255,255,64,64,64,255],readonly=True)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ART,transform=Transform(translate=4.0,origin=1.0))
''')
        self.assertEqual(self.frame()[12:24],bytes([64,64,64,255,255,255,255,255,64,64,64,255]))

    def test_bulk_color_operations_copy_and_context_transform(self):
        self.load('''ART=PixelArray([255,0,0,255,255,0,0,255],readonly=True)
@init
def setup(ctx,runtime):
 ctx.image=PixelArray(2)
 ctx.color=ColorTransform(hue=120.0,brightness=0.5)
 ctx.position=Transform(translate=33.0)
@render
def draw(ctx,runtime,pixels):
 ctx.image.copy(ART)
 ctx.image.transform(ctx.color,start=0,stop=1)
 pixels.draw(ctx.image,transform=ctx.position)
''')
        self.assertEqual(self.frame()[132:140],bytes([0,128,0,255,255,0,0,255]))

    def test_sprite_default_style_folding_preserves_conditional_and_retained_styles(self):
        source = PREFIX + '''def birth(s,runtime,position):
 s.position=position
def paint(s,runtime,pixels):
 pixels.fill(rgb(200,100,60))
 if runtime.time < 0.25:
  s.transform=Transform(translate=s.position)
  s.color=ColorTransform(alpha=0.5,tint=rgb(255,128,255))
  return
 if runtime.time >= 0.5:
  s.transform=Transform(translate=s.position+1.0)
  s.color=ColorTransform()
Particle=Sprite(width=1,init=birth,render=paint)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=2)
 ctx.batch.spawn(position=2.0)
 ctx.batch.spawn(position=5.0)
@render
def draw(ctx,runtime,pixels):
 ctx.batch.render(runtime,pixels)
'''
        variants=[]
        for optimized in (False, True):
            result=compile_source(source,optimize=optimized)
            self.assertTrue(result.success,result.diagnostics)
            self.client.save_program('test',result.bytecode)
            self.client.configure_composition([{'id':'fg','program':'test'}],output='lights')
            self.client.require_ok(self.client.command(STOP,output='lights'))
            variants.append([self.frame(delta) for delta in (0,100,200,300)])
        self.assertEqual(variants[0],variants[1])
        self.assertEqual(variants[1][0][8:12],bytes([100,25,30,128]))
        self.assertEqual(variants[1][2][8:12],bytes([100,25,30,128]))
        self.assertEqual(variants[1][3][12:16],bytes([200,100,60,255]))

    def test_spawn_callbacks_independent_state_removal_and_slot_reuse(self):
        self.load('''def birth(s,runtime,position,duration):
 s.position=position
 s.duration=duration
 s.calls=0
 s.enabled=True
def paint(s,runtime,pixels):
 if runtime.time>=s.duration:
  s.remove()
  return
 s.calls+=1
 if s.enabled:
  pixels.fill(rgb(s.calls,0,0))
 s.transform=Transform(translate=s.position)
Particle=Sprite(width=1,init=birth,render=paint)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=2)
 ctx.done=False
@render
def draw(ctx,runtime,pixels):
 if not ctx.done:
  ctx.batch.spawn(2,0.5)
  ctx.batch.spawn(4,1.0)
  ctx.batch.spawn(6,10.0)
  ctx.done=True
 ctx.batch.render(runtime,pixels)
 if runtime.time>=0.5:
  ctx.batch.spawn(8,1.0)
''')
        first=self.frame(100)
        self.assertEqual(first[8:12],bytes([2,0,0,255]))
        self.assertEqual(first[16:20],bytes([2,0,0,255]))
        self.assertEqual(first[24:28],bytes(4))
        second=self.frame(400)
        self.assertEqual(second[8:12],bytes(4))
        third=self.frame(100)
        self.assertEqual(third[32:36],bytes([1,0,0,255]))

    def test_bulk_ranges_resume_across_native_chunks(self):
        self.load('''@init
def setup(ctx,runtime):
 ctx.image=PixelArray(67,format="rgbwa8")
@render
def draw(ctx,runtime,pixels):
 ctx.image.fill(rgb(100,20,30))
 ctx.image.set_channel("w",80)
 ctx.image.set_channel("g",60,start=31,stop=66)
 ctx.image.set_alpha(0.5,start=31,stop=66)
 pixels.draw(ctx.image)
''')
        expected=b''.join(bytes([50,30,15,128]) if 31<=i<66 else bytes([100,20,30,255]) for i in range(67))
        self.assertEqual(self.frame(),expected)
        self.assertEqual(self.frame(),expected)

    def test_sprite_canvas_native_gradient_palette_points_and_clock_delta(self):
        self.load('''from array import array
def birth(s,runtime):
 s.age=0.0
def paint(s,runtime,pixels):
 s.age+=runtime.delta
 pixels.gradient(rgb(255,0,0),rgb(0,0,255))
 pixels.point(1,rgb(0,255,0),128)
 pixels[2]=rgb(int(s.age*100.0),0,0)
Particle=Sprite(width=3,init=birth,render=paint)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=1)
 ctx.image=PixelArray(3,format="rgb8")
 ctx.palette=array("I",[int(rgb(255,0,0)),int(rgb(0,0,255))])
@render
def draw(ctx,runtime,pixels):
 ctx.batch.spawn()
 ctx.batch.render(runtime,pixels)
 ctx.image.colorwheel(0.0,1.0)
 pixels.draw(ctx.image,transform=Transform(translate=10.0))
 ctx.image.palette(ctx.palette,0.0,0.5)
 pixels.draw(ctx.image,transform=Transform(translate=20.0))
''')
        result=self.frame(250)
        self.assertEqual(result[:12],bytes([255,0,0,255,63,128,64,255,25,0,0,255]))
        self.assertEqual(result[40:52],bytes([255,0,0,255,252,3,0,255,249,6,0,255]))
        self.assertEqual(result[80:92],bytes([255,0,0,255,127,0,128,255,0,0,255,255]))

    def test_sprite_group_opacity_clock_delta_and_no_frame_allocations(self):
        self.load('''def paint(s,runtime,pixels):
 pixels.fill(rgb(255,0,0),alpha=128)
 pixels.draw(ART)
 s.color=ColorTransform(alpha=0.5)
 s.transform=Transform(translate=40.0+runtime.time)
Particle=Sprite(width=1,render=paint)
ART=PixelArray([255,0,0,128],readonly=True)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=20)
@render
def draw(ctx,runtime,pixels):
 if ctx.batch.active<20:
  ctx.batch.spawn()
 ctx.batch.render(runtime,pixels)
''')
        # Two overlapping local contributions have alpha 192, then group alpha
        # is applied once: 96. The next coincident sprite produces 156.
        self.assertEqual(self.frame()[160:164],bytes([156,0,0,156]))
        for _ in range(20):self.frame(10)
        heap=self.client.status_fields()['heap']
        for _ in range(40):self.frame(10)
        self.assertEqual(self.client.status_fields()['heap'],heap)

    def test_new_schema_negotiation_and_forged_readonly_write(self):
        result=self.load('''ART=PixelArray([255,0,0,255],readonly=True)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ART)
''')
        target=self.client.describe()
        self.assertIn('version',target)
        self.assertEqual(result.manifest['schema'],'lightvm.program')
        old={**target,'version':'0.9.0'}
        self.assertFalse(compile_source(PREFIX+'@init\ndef setup(ctx,runtime):\n ctx.image=PixelArray(1)\n@render\ndef draw(ctx,runtime,pixels): pass\n',target=old).success)
        # Forge the destination of image_draw to the readonly source. Admission
        # must reject the package before executing it.
        raw=bytearray(result.bytecode)
        nregs,ncode,ncolors,nfunc,nres,nexpr,ncalls=struct.unpack_from('<7I',raw,8)
        calls=96+nregs*8+ncode*8+ncolors*4+nfunc*24+nres*24+nexpr*16
        for i in range(ncalls):
            at=calls+i*16
            if struct.unpack_from('<I',raw,at)[0]==31:
                raw[at+8]=raw[at+9]
        with self.assertRaises(ProtocolError):self.client.save_program('forged',bytes(raw))

    def test_spawn_initializes_state_even_without_a_render_call(self):
        self.load('''def birth(s,runtime):
 s.value=7
def paint(s,runtime,pixels):
 pixels.fill(rgb(s.value,0,0))
Particle=Sprite(width=1,init=birth,render=paint)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=1)
 ctx.batch.spawn()
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(ctx.batch.active,0,0))
''')
        self.assertEqual(self.frame(),bytes([1,0,0,255])*67)


if __name__=='__main__':
    unittest.main()
