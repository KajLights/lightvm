"""Language motion/easing and periodic drawing against the real host runtime."""
import math
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tools')]
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP
from lightvm.compiler import compile_source
from lightvm import easing, motion

PREFIX = '''from lightvm import profile, init, render, rgb, PixelArray, Sprite, SpriteBatch, Transform, ColorTransform
profile("animation")
'''


class MotionCompilerTests(unittest.TestCase):
    def test_library_imports_are_bounded_and_do_not_execute_guest_code(self):
        for statement in ('from lightvm.motion import missing',
                          'from lightvm.motion import _fit_edge',
                          'from lightvm.easing import *',
                          'from pathlib import Path',
                          'from .lightvm.motion import repeat'):
            result = compile_source(statement + '\n' + PREFIX + '@render\ndef draw(ctx,runtime,pixels): pass\n')
            self.assertFalse(result.success, statement)

    def test_compilation_is_deterministic_and_scalar_helpers_need_no_drawing_api(self):
        source = '''from lightvm import profile, render, rgb
from lightvm.motion import pingpong
from lightvm.easing import smoothstep
profile("animation")
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(int(255.0*smoothstep(pingpong(runtime.time))),0,0))
'''
        result = compile_source(source)
        self.assertTrue(result.success, result.diagnostics)
        self.assertEqual(result.bytecode, compile_source(source).bytecode)
        self.assertEqual(result.manifest['schema'], 'lightvm.program')
        self.assertEqual(struct.unpack_from('<I', result.bytecode, 80)[0], 0)
        ncalls = struct.unpack_from('<I', result.bytecode, 32)[0]
        self.assertEqual(ncalls, 0)

    def test_fit_validation_on_desktop(self):
        for args in ((0,67), (3,0), (68,67), (3,67,0),
                     (3,67,math.inf), (3,67,1,math.nan)):
            with self.subTest(args=args), self.assertRaises(AssertionError):
                motion.fit_start(*args)


class MotionRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.client = Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER', str(ROOT/'build/composition-host/lightvm'))))
        self.configure(67)

    def tearDown(self):
        self.client.close()

    def configure(self, count):
        self.count = count
        self.client.configure({'schema':'lightvm.config', 'devices':[
            {'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':count}}]})

    def load(self, source, settings=None, complete=False):
        result = compile_source(source if complete else PREFIX + source)
        self.assertTrue(result.success, result.diagnostics)
        self.client.save_procedure('test', result.bytecode)
        self.client.configure_scene([{'id':'fg','procedure':'test','settings':settings or {}}], device='lights')
        self.client.require_ok(self.client.command(STOP, device='lights'))
        return result

    def frame(self, milliseconds=0):
        self.client.require_ok(self.client.command(FRAME, struct.pack('<I', milliseconds), device='lights'))
        return bytes(self.client.snapshot(device='lights', layer='fg')[1])

    def test_motion_and_all_easings_match_their_desktop_functions(self):
        cases = [('repeat(-0.25)', motion.repeat(-0.25)),
                 ('repeat(5.0)', 0.0), ('pingpong(-0.25)', 0.25),
                 ('pingpong(1.0)', 1.0), ('pingpong(2.0)', 0.0),
                 ('pingpong(31.25)', 0.75), ('clamp01(2.0)', 1.0),
                 ('clamp01(-2.0)', 0.0), ('lerp(0.0,1.0,1.5)', 1.5)]
        curves = ('linear','quadratic_in','quadratic_out','quadratic_in_out',
                  'cubic_in','cubic_out','cubic_in_out','smoothstep')
        for name in curves:
            for t in (-0.5,0.0,0.25,0.5,0.75,1.0,1.5):
                cases.append((f'{name}({t})', getattr(easing,name)(t)))
        source = ('from lightvm.motion import repeat, pingpong, clamp01, lerp\n'
                  'from lightvm.easing import '+', '.join(curves)+'\n'
                  '@render\ndef draw(ctx,runtime,pixels):\n')
        for i,(expression,_) in enumerate(cases):
            source += f' pixels[{i}]=rgb(int(100.0*({expression})+0.5),0,0)\n'
        self.load(source)
        values = self.frame()
        for i,(expression,expected) in enumerate(cases):
            self.assertAlmostEqual(values[i*4], int(100*expected+0.5), delta=1, msg=expression)

    def test_library_dependencies_cannot_capture_user_helpers_and_aliases_work(self):
        self.load('''from lightvm.easing import smoothstep as ease
from lightvm.motion import fit_end as end
def linear(t):
 return 0.0
def max(a,b):
 return 0.0
@render
def draw(ctx,runtime,pixels):
 pixels[0]=rgb(int(ease(0.5)*200.0),int(end(count=67,width=3)),0)
''')
        self.assertEqual(self.frame()[:4], bytes([100,64,0,255]))

    def test_fitting_respects_pixel_edges_scale_reflection_origin_and_equal_size(self):
        cases = [(3,67,1.0,0.0), (3,67,2.0,0.0), (3,67,-2.0,0.0),
                 (3,67,2.0,1.0), (3,67,-2.0,1.0), (67,67,1.0,0.0)]
        source = 'from lightvm.motion import fit_start, fit_end\n@render\ndef draw(ctx,runtime,pixels):\n'
        for i,args in enumerate(cases):
            args_source = ','.join(map(repr,args))
            source += f' pixels[{i}]=rgb(int(fit_start({args_source})*2.0),int(fit_end({args_source})*2.0),0)\n'
        self.load(source)
        values = self.frame()
        for i,args in enumerate(cases):
            self.assertEqual(values[i*4:i*4+4], bytes([int(motion.fit_start(*args)*2),int(motion.fit_end(*args)*2),0,255]))
        for expression in ('fit_start(68,67)', 'fit_end(3,67,scale=0.0)', 'fit_start(-1,67)'):
            with self.subTest(expression=expression), self.assertRaises(ProtocolError):
                self.load('from lightvm.motion import fit_start, fit_end\n@init\ndef setup(ctx,runtime):\n ctx.x='+expression+'\n@render\ndef draw(ctx,runtime,pixels): pass\n')

    def test_wrap_crosses_both_edges_with_fractional_alpha_and_large_offsets(self):
        art = 'ART=PixelArray([255,0,0,255,0,255,0,128,0,0,255,255],readonly=True)\n'
        expected = bytearray(67*4)
        expected[:12] = bytes([128,64,0,192,0,64,128,192,0,0,128,128])
        expected[-4:] = bytes([128,0,0,128])
        for position in (66.5, -0.5, 6766.5, -6700.5):
            with self.subTest(position=position):
                self.load(art+f'@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(ART,transform=Transform(translate={position}),overflow="wrap")\n')
                self.assertEqual(self.frame(), bytes(expected))

    def test_wrapped_full_strip_has_no_dim_seam_and_does_not_allocate_per_frame(self):
        for width,scale in ((67,1.0),(1,67.0),(67,-1.0)):
            with self.subTest(width=width,scale=scale):
                self.load(f'''@init
def setup(ctx,runtime):
 ctx.image=PixelArray({width})
 ctx.image.fill(rgb(255,0,0),alpha=128)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ctx.image,transform=Transform(translate=0.5,scale={scale}),color=ColorTransform(alpha=0.5),overflow="wrap")
''')
                self.assertEqual(self.frame(), bytes([64,0,0,64])*67)
        heap = self.client.status_fields()['heap']
        for _ in range(10):
            self.assertEqual(self.frame(17), bytes([64,0,0,64])*67)
        self.assertEqual(self.client.status_fields()['heap'], heap)

    def test_wrapped_reflection_origin_and_nearest_sampling(self):
        self.load('''ART=PixelArray([255,0,0,255,0,255,0,128,0,0,255,255],readonly=True)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ART,transform=Transform(translate=66.0,scale=-1.0,origin=2.0),sampling="nearest",overflow="wrap")
''')
        expected = bytearray(67*4)
        expected[:8] = bytes([0,128,0,128,255,0,0,255])
        expected[-4:] = bytes([0,0,255,255])
        self.assertEqual(self.frame(), bytes(expected))

    def test_batch_wrapping_and_sprite_width_metadata(self):
        result = self.load('''def paint(s,runtime,pixels):
 pixels.fill(rgb(255,0,0))
 s.transform=Transform(translate=66.0)
Particle=Sprite(width=3,render=paint)
@init
def setup(ctx,runtime):
 ctx.batch=SpriteBatch(Particle,capacity=1)
 ctx.image=PixelArray(Particle.width)
 ctx.batch.spawn()
@render
def draw(ctx,runtime,pixels):
 ctx.batch.render(runtime,pixels,overflow="wrap")
''')
        expected = bytearray(67*4)
        for i in (0,1,66):
            expected[i*4:i*4+4] = bytes([255,0,0,255])
        self.assertEqual(self.frame(), bytes(expected))
        self.assertEqual(result.manifest['schema'], 'lightvm.program')
        self.assertEqual(struct.unpack_from('<I', result.bytecode, 80)[0], 0)
        target = self.client.describe()
        self.assertTrue(compile_source(PREFIX+'ART=PixelArray([255,0,0,255],readonly=True)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(ART,overflow="wrap")\n', target=target).success)
        self.assertFalse(compile_source(PREFIX+'ART=PixelArray([255,0,0,255],readonly=True)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(ART,overflow="wrap")\n', target={**target,'version':'0.9.0'}).success)
        older = {**target,'version':'2.0.0'}
        self.assertFalse(compile_source(PREFIX+'ART=PixelArray([255,0,0,255],readonly=True)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(ART,overflow="wrap")\n', target=older).success)
        forged = bytearray(result.bytecode)
        struct.pack_into('<I', forged, 80, 2)
        with self.assertRaises(ProtocolError):
            self.client.save_procedure('forged', forged)

    def test_batch_nearest_sampling_scales_a_solid_brush(self):
        self.load('''def paint(s,runtime,pixels):
 pixels.fill(rgb(255,0,0))
 s.transform=Transform(translate=2.0,scale=5.0)
Brush=Sprite(width=1,render=paint)
@init
def setup(ctx,runtime):
 ctx.brush=SpriteBatch(Brush,capacity=1)
 ctx.brush.spawn()
@render
def draw(ctx,runtime,pixels):
 ctx.brush.render(runtime,pixels,sampling="nearest")
''')
        self.assertEqual(self.frame(),bytes([255,0,0,255])*5+bytes(62*4))
        invalid=PREFIX+'''def paint(s,runtime,pixels): pass
Brush=Sprite(width=1,render=paint)
@init
def setup(ctx,runtime):
 ctx.brush=SpriteBatch(Brush,capacity=1)
 ctx.brush.spawn()
@render
def draw(ctx,runtime,pixels):
 ctx.brush.render(runtime,pixels,sampling="cubic")
'''
        self.assertFalse(compile_source(invalid).success)

    def test_wrap_rejects_oversized_images_and_unknown_modes(self):
        with self.assertRaises(ProtocolError):
            self.load('''@init
def setup(ctx,runtime):
 ctx.image=PixelArray(68)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ctx.image,overflow="wrap")
''')
        for option in ('"bounce"', 'True', 'runtime.time'):
            result = compile_source(PREFIX+'ART=PixelArray([255,0,0,255],readonly=True)\n@render\ndef draw(ctx,runtime,pixels):\n pixels.draw(ART,overflow='+option+')\n')
            self.assertFalse(result.success)

    def test_forged_wrap_cannot_sample_with_an_uninitialized_transform(self):
        result = compile_source(PREFIX+'''ART=PixelArray([255,0,0,255],readonly=True)
@render
def draw(ctx,runtime,pixels):
 pixels.draw(ART,overflow="wrap")
''')
        self.assertTrue(result.success, result.diagnostics)
        forged = bytearray(result.bytecode)
        nregs,ncode,ncolors,nfunc,nres,nexpr,ncalls = struct.unpack_from('<7I', forged, 8)
        code = 96+nregs*8
        calls = code+ncode*8+ncolors*4+nfunc*24+nres*24+nexpr*16
        geometry = {i for i in range(ncalls) if struct.unpack_from('<I',forged,calls+i*16)[0] == 32}
        skipped = 0
        for pc in range(ncode):
            op,a,b,c,index = struct.unpack_from('<4BI',forged,code+pc*8)
            if op == 38 and index in geometry:
                # Valid control flow that skips the normal geometry initializer.
                struct.pack_into('<4BI',forged,code+pc*8,19,0,0,0,pc+1)
                skipped += 1
        self.assertEqual(skipped, 1)
        self.client.save_procedure('forged', forged)
        with self.assertRaises(ProtocolError):
            self.client.configure_scene([{'id':'fg','procedure':'forged'}],device='lights')

    def test_rainbow_example_edges_hue_cycle_easing_and_frame_rate_independence(self):
        count = min(200, self.client.describe()['pixel_capacity'])
        self.configure(count)
        source = (ROOT/'examples/animations/rainbow_sprite.py').read_text()
        self.load(source, complete=True)
        frames = {0:self.frame()}
        for t in range(1,41):
            data = self.frame(1000)
            if t in (5,10,20,30,40):
                frames[t] = data
        self.assertEqual([i for i in range(count) if frames[0][i*4+3]], list(range(15)))
        self.assertEqual([i for i in range(count) if frames[20][i*4+3]], list(range(count-15,count)))
        self.assertEqual(frames[0][:60], frames[20][-60:])
        self.assertEqual(frames[0], frames[40])
        self.assertEqual(frames[10], frames[30])
        middle = (count-15)/2
        at = math.ceil(middle)
        self.assertEqual(frames[10][at*4:at*4+4], bytes([255,int(102*(at-middle)+0.5),0,255]))
        first = next(i for i in range(count) if frames[5][i*4+3])
        self.assertEqual(first, math.floor((count-15)*motion.pingpong(0.25)**2*(3-2*motion.pingpong(0.25))))
        self.assertEqual(frames[5][(first+1)*4], 0)  # Half a hue cycle: cyan end.
        self.load(source, complete=True)
        for _ in range(100):
            data = self.frame(100)
        self.assertEqual(data, frames[10])


if __name__ == '__main__':
    unittest.main()
