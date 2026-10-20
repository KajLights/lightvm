"""Bulk pixel loops retain the scalar language's results and fault behavior."""
import os
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, ProtocolError, FRAME, STOP

PREFIX='''from lightvm import profile,init,render,rgb,hsv,colorwheel,hash01,PixelArray
profile("animation")
'''


def instructions(result):
    nregs,ncode=struct.unpack_from('<2I',result.bytecode,8)
    return [struct.unpack_from('<4BI',result.bytecode,96+nregs*8+i*8) for i in range(ncode)]


class PixelMapTests(unittest.TestCase):
    def setUp(self):
        self.client=Client(ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/composition-host/lightvm'))))
        self.count=min(200,self.client.describe()['pixel_capacity'])
        self.client.configure({'schema':'lightvm.config','outputs':[{
            'id':'lights','type':'ws2812b','config':{'pin':'GP16','count':self.count}}]})

    def tearDown(self):self.client.close()

    def load(self,source,optimize=True,settings=None):
        result=compile_source(source,optimize=optimize)
        self.assertTrue(result.success,result.diagnostics)
        self.client.save_program('map_test',result.bytecode)
        self.client.configure_composition([{'id':'fg','program':'map_test','settings':settings or {}}],output='lights')
        self.client.require_ok(self.client.command(STOP,output='lights'))
        self.client.reset_program(output='lights',layer='fg')
        return result

    def frames(self):
        output=[]
        for ms in (0,1,17,333,1000):
            self.client.require_ok(self.client.command(FRAME,struct.pack('<I',ms),output='lights'))
            output.append(bytes(self.client.snapshot(output='lights',layer='fg')[1]))
        return output

    def compare(self,source,settings=None):
        self.load(source,False,settings);expected=self.frames()
        result=self.load(source,True,settings)
        self.assertTrue(any(i[0]==39 for i in instructions(result)))
        self.assertEqual(self.frames(),expected)
        return result

    def test_rainbow_matches_native_fill_and_scalar_loop(self):
        source=(ROOT/'examples/animations/rainbow.py').read_text().replace('random.seed()','random.seed(1)')
        source=source.replace('default=1.0, minimum=0.0, maximum=16.0','default=2.3, minimum=0.0, maximum=16.0')
        result=self.compare(source)
        native=source[:source.index('    for i in range(pixels.count):')]+'''    pixels.colorwheel(start_hue, spatial_scale)
'''
        self.load(source,True);expected=self.frames()
        self.load(native,True);self.assertEqual(self.frames(),expected)
        self.assertEqual(result.manifest['schema'],'lightvm.program')
        self.assertEqual(struct.unpack_from('<I',result.bytecode,80)[0],0)

    def test_generic_nonlinear_hsv_helpers_integer_arithmetic_and_captured_values(self):
        bodies=[
            'pixels[i]=hsv((i/pixels.count)*(i/pixels.count)+runtime.time,0.6,0.8)',
            'pixels[i]=rgb((i-70)%13,((i-70)//7+10)*3,int(hash01(i,17)*255.0))',
            'pixels[i]=hsv(max(0.0,min(1.0,i/33.0)),1.0,0.75)',
            'pixels[i]=rgb(int((math.sin(i/13.0)+1.0)*100.0),int(99.0/pixels.count),0)',
            'color=shade(i,pixels.count,runtime.time)\n  pixels[i]=color',
        ]
        for body in bodies:
            with self.subTest(body=body):
                self.compare('import math\n'+PREFIX+'''def shade(index,count,phase):
 return hsv(index/count+phase,1.0,1.0)
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  '''+body+'\n')

    def test_loop_locals_and_index_have_scalar_values_after_completion(self):
        self.compare(PREFIX+'''from lightvm import red
@render
def draw(ctx,runtime,pixels):
 color=rgb(7,0,0)
 for i in range(33):
  color=rgb(i,0,0)
  pixels[i]=color
 pixels[0]=rgb(i,red(color),0)
''')

    def test_empty_and_negative_ranges_do_not_evaluate_invariant_faults(self):
        for bound in (0,-1):
            self.compare(PREFIX+f'''@render
def draw(ctx,runtime,pixels):
 for i in range({bound}):
  pixels[i]=rgb(int(1.0/0.0),0,0)
 pixels[0]=rgb(i,0,0)
''')

    def test_all_image_formats_and_nonmultiple_chunk_sizes(self):
        for format in ('rgb8','rgba8','rgbw8','rgbwa8'):
            for size in (1,31,32,33,67):
                with self.subTest(format=format,size=size):
                    self.compare(PREFIX+f'''@init
def setup(ctx,runtime):
 ctx.image=PixelArray({size},format="{format}")
@render
def draw(ctx,runtime,pixels):
 for i in range(ctx.image.count):
  ctx.image[i]=hsv(i/67.0,1.0,1.0)
 pixels.draw(ctx.image)
''')

    def test_effectful_feedback_carried_and_branching_loops_fall_back(self):
        bodies=[
            'ctx.calls+=1\n  pixels[i]=rgb(1,0,0)',
            'value+=1\n  pixels[i]=rgb(value,0,0)',
            'pixels[i]=pixels[0]',
            'pixels[i]=rgb(random.randrange(255),0,0)',
            'if i>5:\n   break\n  pixels[i]=rgb(1,0,0)',
            'pixels[i]=rgb(1,0,0) if i>5 else rgb(0,1,0)',
        ]
        for body in bodies:
            source='import random\n'+PREFIX+'''@init
def setup(ctx,runtime):
 ctx.calls=0
@render
def draw(ctx,runtime,pixels):
 value=0
 for i in range(33):
  '''+body+'\n'
            result=compile_source(source)
            self.assertTrue(result.success,result.diagnostics)
            self.assertFalse(any(i[0]==39 for i in instructions(result)),body)

    def test_target_requirements_and_forged_capability_or_body_are_rejected(self):
        source=PREFIX+'@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n  pixels[i]=hsv(i/pixels.count,1.0,1.0)\n'
        target=self.client.describe()
        old={**target,'version':'0.9.0'}
        result=compile_source(source,target=old)
        self.assertFalse(result.success)
        result=self.load(source)
        forged=bytearray(result.bytecode);struct.pack_into('<I',forged,80,2)
        with self.assertRaises(ProtocolError):self.client.save_program('forged',forged)
        # A kernel cannot replace its store index with an unchecked constant.
        forged=bytearray(result.bytecode);nregs=struct.unpack_from('<I',forged,8)[0]
        pc=next(pc for pc,i in enumerate(instructions(result)) if i[0]==39)
        end=instructions(result)[pc][4]
        forged[96+nregs*8+(end-1)*8+2]=0
        with self.assertRaises(ProtocolError):self.client.save_program('forged',forged)

    def test_faults_preserve_completed_frame_and_heap_is_stable(self):
        source=PREFIX+'''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  pixels[i]=rgb(int(runtime.time*1000.0),0,0)
'''
        self.load(source);before=self.client.snapshot(output='lights',layer='fg')[1]
        heap=self.client.status_fields()['heap']
        for _ in range(10):
            self.client.require_ok(self.client.command(FRAME,bytes(4),output='lights'))
        self.assertEqual(self.client.status_fields()['heap'],heap)
        with self.assertRaises(ProtocolError):self.client.require_ok(self.client.command(FRAME,struct.pack('<I',1000),output='lights'))
        self.assertEqual(self.client.snapshot(output='lights',layer='fg')[1],before)


if __name__=='__main__':unittest.main()
