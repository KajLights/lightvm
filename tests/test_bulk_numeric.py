"""Prepared gather/branch kernels, ordinary bytecode, faults and final locals."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lightvm.portable import PortableCompiler,P_OP
import numeric_oracle as oracle

PREFIX='''import math
from array import array
from lightvm import profile,init,render,rgb,FixedArray,fixed,fixed_bits,fixed_from_bits,fixed_sqrt,PixelArray,floor,ceil,trunc
profile("animation")
'''


class BulkNumericTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver=Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME',ROOT/'build/host/test_platform'))
        if not cls.driver.exists():raise unittest.SkipTest('native test port not built')

    def compile(self,source,optimize=True):
        compiler=PortableCompiler(PREFIX+source,optimize=optimize);package,manifest=compiler.compile()
        return compiler,package,manifest

    def run_package(self,package,count=67,tick=0,workers=1):
        with tempfile.NamedTemporaryFile() as file:
            file.write(package);file.flush()
            result=subprocess.run([str(self.driver),'--numeric-package',file.name,str(count),str(tick),str(workers)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr);self.assertFalse(result.stderr)
        return json.loads(result.stdout)

    def compare(self,source,counts=(1,31,32,33,63,64,65,127,128,129),fault=False,floor='1.3.0'):
        _,scalar,_=self.compile(source,False);compiler,bulk,manifest=self.compile(source)
        self.assertIn(P_OP['PIXEL_MAP'],[i[0] for i in compiler.code])
        self.assertEqual(manifest['requires'],floor)
        for count in counts:
            expected=self.run_package(scalar,count);self.assertEqual(expected['status'],-1 if fault else 1)
            for workers,tick in ((1,0),(2,0),(1,1000),(2,1000)):
                with self.subTest(count=count,workers=workers,tick=tick):
                    result=self.run_package(bulk,count,tick,workers)
                    self.assertGreater(result.pop('plan_bytes'),0)
                    self.assertEqual(result,{k:v for k,v in expected.items() if k!='plan_bytes'})
        return compiler,bulk

    def test_resident_numeric_gathers_and_wrapping(self):
        for kind,values in (('i','[0x123456,0xabcdef,0x020304]'),('I','[0x123456,0xabcdef,0x020304]'),('B','[1,100,255]'),('H','[1,100,65535]')):
            self.compare(f'''@init
def setup(ctx,runtime):ctx.table=array('{kind}',{values})
@render
def draw(ctx,runtime,pixels):
 ctx.table[0]=17
 for i in range(pixels.count):
  color=ctx.table[(i*7-9)%len(ctx.table)]
  pixels[i]=rgb((color>>16)&255,(color>>8)&255,color&255)
''',counts=(1,33,129))
        self.compare('''@init
def setup(ctx,runtime):
 ctx.table=array('f',[0.25,0.5,0.75])
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  pixels[i]=rgb(int(ctx.table[i%3]*255.0),0,0)
''')

    def test_reviewed_fixed_and_bitwise_vectors_through_maps(self):
        cases=json.loads((ROOT/'tests/fixtures/numeric-vectors.json').read_text())['exact']
        unary={'neg':'-ctx.a','to_i32':'int(ctx.a)','to_f32':'float(ctx.a)',
               'bits':'fixed_bits(ctx.a)','sqrt':'fixed_sqrt(ctx.a)',
               'from_i32':'fixed(ctx.a)','from_f32':'fixed(ctx.a)',
               'from_bits':'fixed_from_bits(ctx.a)','not':'~ctx.a'}
        binary={'add':'+','sub':'-','mul':'*','div':'/','mod':'%',
                'lt':'<','eq':'==','and':'&','or':'|','xor':'^','shl':'<<','shr':'>>'}
        for case in cases:
            op=case['op'].split('.')[1];words=[int(w,16) for w in case['args']]
            input_kind='i' if case['op'].startswith('i32.') or op in ('from_i32','from_bits') else 'f' if op=='from_f32' else 'q'
            values=[]
            for word in words:
                if input_kind=='f':
                    import math
                    number=struct.unpack('<f',struct.pack('<I',word))[0]
                    if not math.isfinite(number):break # Invalid initializer; hand-encoded invocation covers this.
                    values.append(repr(number))
                else:
                    number=str(oracle.signed(word))
                    values.append('fixed_from_bits('+number+')' if input_kind=='q' else number)
            if len(values)!=len(words):continue
            expression=unary.get(op)
            if expression is None:
                expression=(f'{op}(ctx.a,ctx.b)' if op in ('min','max') else f'ctx.a {binary[op]} ctx.b')
            initial=('0' if case['op'].startswith('i32.') or op in ('to_i32','bits') else
                     '0.0' if op=='to_f32' else 'False' if op in ('lt','eq') else 'fixed(0)')
            source='@init\ndef setup(ctx,runtime):\n ctx.a='+values[0]+'\n'
            if len(values)>1:source+=' ctx.b='+values[1]+'\n'
            source+=' ctx.last='+initial+'''\n@render
def draw(ctx,runtime,pixels):
 value='''+initial+'''
 for i in range(pixels.count):
  value='''+expression+'''
  pixels[i]=rgb(i%255,0,0)
 ctx.last=value
'''
            with self.subTest(case=case):
                _,package=self.compare(source,counts=(1,33),fault=bool(case.get('fault')))
                if not case.get('fault'):
                    self.assertEqual(self.run_package(package,count=33,workers=2)['state'][-1],int(case['result'],16))

    def test_fixed_arrays_lazy_nested_branches_and_final_values(self):
        self.compare('''@init
def setup(ctx,runtime):
 ctx.table=FixedArray(5,fill=fixed(0.25))
 ctx.last=fixed(0)
@render
def draw(ctx,runtime,pixels):
 ctx.table[0]=fixed(runtime.delta)
 q=fixed(0)
 for i in range(pixels.count):
  if (i&1)==0:
   q=ctx.table[(i*7)%5]
  else:
   q=fixed_sqrt(fixed(0.25)) if i<10000 else ctx.table[999]
  pixels[i]=rgb(int(q*fixed(200)),i%255,0)
 ctx.last=q
''')

    def test_conditional_last_writes_merge_across_lanes_and_yields(self):
        self.compare('''@init
def setup(ctx,runtime):ctx.last=0
@render
def draw(ctx,runtime,pixels):
 last=0
 for i in range(pixels.count):
  if i<2:last=i+7
  pixels[i]=rgb(i%255,0,0)
 ctx.last=last
''')

    def test_lazy_faults_and_persistent_partial_writes(self):
        for expression in ('ctx.table[999]','math.sqrt(-1.0)','fixed(1)/fixed(0)'):
            q=expression.startswith('fixed')
            value='fixed(1)' if q else '1.0'
            if expression.startswith('ctx'):value='1.0'
            source=f'''@init
def setup(ctx,runtime):
 ctx.table=array('f',[1.0])
 ctx.image=PixelArray(runtime.pixel_count)
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x={value} if i<STOP else {expression}
  ctx.image[i]=rgb(int(x),i%255,0)
 pixels.draw(ctx.image)
'''
            self.compare(source.replace('STOP','10000'),counts=(33,129))
            self.compare(source.replace('STOP','31'),counts=(33,129),fault=True)

    def test_long_body_and_math_helpers(self):
        updates=''.join('  x=math.sqrt(x*x+0.01)\n' for _ in range(15))
        self.compare('''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=i/200.0
'''+updates+'''  pixels[i]=rgb(int(min(x,1.0)*255.0),int(floor(4.75)+ceil(-1.5)+trunc(1.9)),0)
''',counts=(1,33,129))
        compiler,_,_=self.compile('''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=i/200.0
'''+updates*5+'  pixels[i]=rgb(int(min(x,1.0)*255.0),0,0)\n')
        self.assertNotIn(P_OP['PIXEL_MAP'],[i[0] for i in compiler.code])

    def test_exact_body_limit_and_definite_assignment(self):
        # Each update is ADD + MOV; these bodies include the common final store.
        for updates,length in ((30,64),(62,128)):
            source='''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=i
'''+('  x=x+1\n'*updates)+'  pixels[i]=rgb(x%255,0,0)\n'
            compiler,_=self.compare(source,counts=(1,33,129))
            pc=next(pc for pc,ins in enumerate(compiler.code) if ins[0]==39)
            self.assertEqual(compiler.code[pc][4]-pc-1,length)
        compiler,_,_=self.compile(source.replace('  x=i\n','  x=i+1\n'))
        self.assertNotIn(39,[ins[0] for ins in compiler.code])

    def test_empty_ranges_do_not_evaluate_domains_or_gathers(self):
        for stop in (0,-1):
            self.compare(f'''@init
def setup(ctx,runtime):ctx.table=array('f',[1.0])
@render
def draw(ctx,runtime,pixels):
 for i in range({stop}):
  pixels[i]=rgb(int(math.log(ctx.table[999])),0,0)
''',counts=(1,33))

    def test_native_math_has_identical_raw_scalar_and_bulk_results(self):
        for expression in ('abs(-x)','floor(-x)','ceil(-x)','trunc(-x)',
                           'math.sin(x)','math.cos(x)','math.sqrt(x)','math.log(x)',
                           'math.exp(x)','math.pow(x,0.5)','min(x,-0.0)','max(-x,-0.0)'):
            with self.subTest(expression=expression):
                self.compare('''@init
def setup(ctx,runtime):ctx.last=0.0
@render
def draw(ctx,runtime,pixels):
 value=0.0
 for i in range(pixels.count):
  x=(i+1)/257.0
  value='''+expression+'''
  pixels[i]=rgb(int(min(255.0,max(0.0,100.0+value*10.0))),0,0)
 ctx.last=value
''',counts=(1,33,129),floor='1.0.0' if expression in ('math.sin(x)','min(x,-0.0)','max(-x,-0.0)') else '1.3.0')

    def test_forged_control_flow_effects_references_and_floor(self):
        compiler,package,_=self.compile('''@init
def setup(ctx,runtime):
 ctx.table=array('i',[1,2,3])
 ctx.last=0
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=ctx.table[i%3] if i<10 else 4
  pixels[i]=rgb(x,0,0)
''')
        code=compiler.code;base=96+len(compiler.regs)*8
        map_pc=next(pc for pc,i in enumerate(code) if i[0]==39);end=code[map_pc][4]
        branch=next(pc for pc,i in enumerate(code) if i[0]==P_OP['JZ'])
        load=next(pc for pc,i in enumerate(code) if i[0]==P_OP['LOAD_ELEM'])
        state=next(r for r,reg in enumerate(compiler.regs) if reg[1]==3)
        mutations=[(base+branch*8+4,struct.pack('<I',map_pc)),(base+branch*8+4,struct.pack('<I',end)),
                   (base+branch*8+4,struct.pack('<I',end-1)), # Valid forward target, undefined stored colour.
                   (base+load*8+1,bytes([state])),(base+load*8+2,bytes([code[map_pc][1]])),
                   (base+load*8+4,struct.pack('<I',1)),(base+load*8,bytes([39]))]
        for offset,value in mutations:
            forged=bytearray(package);forged[offset:offset+len(value)]=value
            self.assertFalse(self.run_package(forged)['load'],(offset,value))
        self.assertFalse(self.run_package(package.replace(b'"requires":"1.3.0"',b'"requires":"1.2.0"'))['load'])


if __name__=='__main__':unittest.main()
