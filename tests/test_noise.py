"""Noise equations, domains, independent wire admission, maps and work limits."""
import json
import math
import os
from pathlib import Path
import random
import shlex
import struct
import subprocess
import tempfile
import unittest

import test_bulk_numeric as bulk
import noise_oracle
from lightvm.compiler import compile_source
from lightvm.numeric_metadata import NOISE_OPERATIONS

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('perlin2','perlin3','simplex2','simplex3')
IMPORT = 'from lightvm import perlin2,perlin3,simplex2,simplex3\n'


def bits(value): return struct.unpack('<I',struct.pack('<f',value))[0]
def value(word): return struct.unpack('<f',struct.pack('<I',word))[0]
def arguments(name, coords, seed=0, repeat=None):
    words = [bits(x) for x in coords]+[seed & 0xffffffff]
    if name.startswith('perlin'): words += repeat or [256]*len(coords)
    return words+[0]*(7-len(words))


class NoiseMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.driver = Path(cls.temp.name)/'noise'
        subprocess.run(shlex.split(os.environ.get('CC','cc'))+[
            '-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off',
            '-I',str(ROOT/'src'),str(ROOT/'src/noise.c'),str(ROOT/'tests/noise_driver.c'),
            '-o',str(cls.driver)]+shlex.split(os.environ.get('LIGHTVM_NUMERIC_CFLAGS','')),check=True)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def run_cases(self,cases,command=None):
        data=''.join(str(binding)+' '+' '.join(f'{x&0xffffffff:08x}' for x in words)+'\n' for binding,words in cases)
        run=subprocess.run(command or [str(self.driver)],input=data,capture_output=True,text=True,check=True)
        self.assertFalse(run.stderr)
        rows=[(int(a),int(b,16)) for a,b in (line.split() for line in run.stdout.splitlines())]
        self.assertEqual(len(rows),len(cases));return rows

    def test_independent_equations_and_range(self):
        rng=random.Random(1937);cases=[];references=[]
        for name in NAMES:
            dims=int(name[-1])
            coords=[[0.0]*dims,[-0.0]*dims,[65536.0]*dims,[-65536.0]*dims]
            coords += [[value(bits(rng.uniform(-64,64))) for _ in range(dims)] for _ in range(600)]
            coords += [[value(bits(rng.uniform(-65536,65536))) for _ in range(dims)] for _ in range(100)]
            coords += [[x]*dims for x in (-2,-1,-0.000001,0.000001,0.25,0.5,1,2)]
            for p in coords:
                seed=rng.getrandbits(32);repeat=[rng.choice((1,2,3,17,256,65536)) for _ in range(dims)]
                cases.append((NOISE_OPERATIONS[name]['binding'],arguments(name,p,seed,repeat)))
                references.append(noise_oracle.sample(name,p,seed,repeat))
        rows=self.run_cases(cases)
        for case,reference,(ok,word) in zip(cases,references,rows):
            self.assertTrue(ok,case);self.assertLessEqual(abs(value(word)),1)
            self.assertAlmostEqual(value(word),reference,delta=0.00002,msg=str(case))
        wasm=os.environ.get('LIGHTVM_NOISE_WASM')
        if wasm:self.assertEqual(rows,self.run_cases(cases,shlex.split(wasm)))

    def test_faults_do_not_write_and_subnormal_coordinates_are_zero(self):
        cases=[]
        for name in NAMES:
            dims=int(name[-1]);base=arguments(name,[0.125]*dims)
            for axis in range(dims):
                for word in (0x7fc00000,0x7f800000,0xff800000,0x47800001,0xc7800001,0x7f7fffff):
                    args=base.copy();args[axis]=word;cases.append((NOISE_OPERATIONS[name]['binding'],args))
            if name.startswith('perlin'):
                for axis in range(dims):
                    for word in (0,0xffffffff,65537,0x80000000):
                        args=base.copy();args[dims+1+axis]=word;cases.append((NOISE_OPERATIONS[name]['binding'],args))
        cases.append((999,[0]*7))
        self.assertEqual(self.run_cases(cases),[(0,0x5a5aa5a5)]*len(cases))
        for name in NAMES:
            args=arguments(name,[0.0]*int(name[-1]));binding=NOISE_OPERATIONS[name]['binding']
            variants=[]
            for word in (0,0x80000000,1,0x807fffff):
                a=args.copy();a[0]=word;variants.append((binding,a))
            self.assertEqual(self.run_cases(variants),[(1,0)]*4)

    def test_periodicity_continuity_lattice_and_all_seed_bits(self):
        for name in NAMES:
            dims=int(name[-1]);binding=NOISE_OPERATIONS[name]['binding'];p=[0.25,-0.75,1.5][:dims]
            seeds=[0]+[1<<n for n in range(32)]
            rows=self.run_cases([(binding,arguments(name,[x+offset for x in p],seed)) for seed in seeds for offset in (0.0,0.17,1.39,7.63,19.15,34.7,51.9)])
            signatures=[tuple(rows[n:n+7]) for n in range(0,len(rows),7)]
            self.assertEqual(len(set(signatures)),33)
            for axis in range(dims):
                left=p.copy();right=p.copy();left[axis]=1-2**-16;right[axis]=1+2**-16
                a,b=self.run_cases([(binding,arguments(name,left)),(binding,arguments(name,right))])
                self.assertLess(abs(value(a[1])-value(b[1])),0.001)
            if name.startswith('perlin'):
                period=[3,17,256][:dims]
                q=[x-r for x,r in zip(p,period)]
                a,b=self.run_cases([(binding,arguments(name,p,17,period)),(binding,arguments(name,q,17,period))])
                self.assertEqual(a,b)
                self.assertEqual(self.run_cases([(binding,arguments(name,[-3.0]*dims))]),[(1,0)])


class NoiseRuntimeTests(unittest.TestCase):
    setUpClass=classmethod(bulk.BulkNumericTests.setUpClass.__func__)
    compile=bulk.BulkNumericTests.compile
    run_package=bulk.BulkNumericTests.run_package
    compare=bulk.BulkNumericTests.compare

    def test_scalar_prepared_yielded_and_two_worker_paths(self):
        for name in NAMES:
            args='x,0.375'+(',0.625' if name.endswith('3') else '')
            for suffix in ('',',-2147483648'+(',3,17,256' if name=='perlin3' else ',3,17' if name=='perlin2' else '')):
                source=IMPORT+'''@init
def setup(ctx,runtime):ctx.last=0.0
@render
def draw(ctx,runtime,pixels):
 v=0.0
 for i in range(pixels.count):
  x=i/13.0-2.0
  v='''+name+'('+args+suffix+''')
  pixels[i]=rgb(int(127.0+v*127.0),0,0)
 ctx.last=v
'''
                _,package=self.compare(source,counts=(1,33,129),floor='1.4.0')
                x=value(bits(value(bits(32/13.0))-2.0))
                coords=[x,0.375]+([0.625] if name.endswith('3') else [])
                expected=noise_oracle.sample(name,coords,-2147483648 if suffix else 0,
                                             [3,17,256][:len(coords)] if suffix else None)
                actual=value(self.run_package(package,count=33)['state'][0])
                self.assertAlmostEqual(actual,expected,delta=0.00002)

    def test_seventh_operand_varies_per_pixel_and_faults_lazily(self):
        for period,fault in (('i%5+1',False),('7 if i<31 else 0',True)):
            self.compare(IMPORT+f'''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  n=perlin3(i/17.0,0.25,7.75,i,3,17,{period})
  pixels[i]=rgb(int(127.0+n*127.0),0,0)
''',counts=(33,129),floor='1.4.0',fault=fault)

    def test_expensive_noise_maps_respect_first_pixel_budget(self):
        for calls,admitted in ((5,True),(6,False)):
            source=IMPORT+'@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n'
            source+='  n=simplex3(i/17.0,0.25,0.5)\n'*calls
            source+='  pixels[i]=rgb(int(127.0+n*127.0),0,0)\n'
            compiler,package,_=self.compile(source)
            self.assertEqual(any(i[0]==39 for i in compiler.code),admitted)
            if not admitted:self.assertIn('first-pixel work budget',compiler.map_reports[0][2])
            self.assertEqual(self.run_package(package)['status'],1)

    def test_lazy_faults_and_first_pixel_work_reservation(self):
        for name in NAMES:
            coords='x,0.25'+(',0.5' if name.endswith('3') else '')
            for stop in (31,1000):
                source=IMPORT+f'''@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=i/11.0 if i<{stop} else 65537.0
  v={name}({coords})
  pixels[i]=rgb(int(127.0+v*127.0),0,0)
'''
                self.compare(source,counts=(33,),floor='1.4.0',fault=stop==31)
            source=IMPORT+'@init\ndef setup(ctx,runtime):ctx.v=0.0\n@render\ndef draw(ctx,runtime,pixels):ctx.v='+name+'('+','.join(['0.25']*int(name[-1]))+')\n'
            _,package,_=self.compile(source,False)
            work=NOISE_OPERATIONS[name]['work']
            for budget,status in ((work-1,-1),(work+2,1)):
                with tempfile.NamedTemporaryFile() as f:
                    f.write(package);f.flush()
                    r=json.loads(subprocess.check_output([str(self.driver),'--numeric-package',f.name,'33','0','1',str(budget)]))
                self.assertEqual(r['status'],status)
                if status==-1:self.assertEqual((r['work'],r['state']),(0,[0]))

    def test_rng_and_call_order_independence(self):
        prefix='import random\n'+IMPORT+'@init\ndef setup(ctx,runtime):\n random.seed(19)\n ctx.a=0.0\n ctx.b=0.0\n ctx.n=0.0\n@render\ndef draw(ctx,runtime,pixels):\n ctx.a=random.random()\n'
        suffix=' ctx.b=random.random()\n'
        plain=self.run_package(self.compile(prefix+suffix)[1])
        sampled=self.run_package(self.compile(prefix+' ctx.n=simplex3(0.25,-0.5,0.75,9)\n'+suffix)[1])
        self.assertEqual(plain['state'][:2],sampled['state'][:2])

    def test_compiler_floor_and_argument_types(self):
        for name in NAMES:
            args=','.join(['0.25']*int(name[-1]))
            source=bulk.PREFIX+IMPORT+'@render\ndef draw(ctx,runtime,pixels):pixels.fill(rgb(int('+name+'('+args+')+1.0),0,0))\n'
            self.assertFalse(compile_source(source,target={'version':'1.3.0'}).success)
            self.assertFalse(compile_source(source.replace('profile("animation")','profile("animation")\nrequires("1.3")')).success)
            self.assertFalse(compile_source(source.replace(args+')',args+',0.5)')).success)
            result=compile_source(source);self.assertTrue(result.success,result.diagnostics)
            forged=result.bytecode.replace(b'"requires":"1.4.0"',b'"requires":"1.3.0"')
            self.assertFalse(self.run_package(forged)['load'])

    def test_independent_native_wire_signatures(self):
        # Numeric IDs/types here are deliberately independent of the catalogue.
        for binding,dims,argc in ((59,2,5),(60,3,7),(61,2,3),(62,3,4)):
            manifest=b'{"requires":"1.4.0"}'
            header=[4,argc+1,2,0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,len(manifest),0,0,0,0]
            package=bytearray(b'LBC1'+struct.pack('<23I',*header))
            for k in range(argc):
                package+=struct.pack('<BBHI',2 if k<dims else 1,2,0,bits(0.25) if k<dims else 0 if k==dims else 256)
            package+=struct.pack('<BBHI',2,0,0,0)
            package+=struct.pack('<4BI4BI',38,argc,0,0,0,1,argc,0,0,0)
            package+=struct.pack('<6I',0,2,0,2,0,0)
            package+=struct.pack('<BBHIII',0,1,0,0,0,0)
            call_offset=len(package)
            package+=struct.pack('<II8B',binding,argc,*(list(range(argc))+[0]*(8-argc)))
            package+=manifest
            self.assertEqual(self.run_package(package)['status'],1)
            for k in range(argc):
                forged=package.copy();forged[96+8*k]=1 if k<dims else 2
                self.assertFalse(self.run_package(forged)['load'])
                forged=package.copy();forged[call_offset+8+k]=255
                self.assertFalse(self.run_package(forged)['load'])
            for offset,data in ((call_offset+4,struct.pack('<I',argc-1)),
                                (call_offset+8+argc,b'\x01'),(call_offset,struct.pack('<I',63)),
                                (96+8*argc,b'\x01')):
                forged=package.copy();forged[offset:offset+len(data)]=data
                self.assertFalse(self.run_package(forged)['load'])

    def test_input_snapshots_survive_yields_and_binding_changes(self):
        source=bulk.PREFIX+'''from lightvm import Input,simplex2
SCALE=Input("scale",default=0.25)
FIELD=Input("field",default=0)
@render
def draw(ctx,runtime,pixels):
 scale=runtime.input(SCALE)
 field=runtime.input(FIELD)
 for i in range(pixels.count):
  n=simplex2(i/16.0,scale,field)
  pixels[i]=rgb(int(127.0+n*127.0),0,0)
'''
        for optimize in (False,True):
            result=compile_source(source,optimize=optimize);self.assertTrue(result.success,result.diagnostics)
            with tempfile.NamedTemporaryFile() as f:
                f.write(result.bytecode);f.flush()
                run=subprocess.run([str(self.driver),'--noise-inputs',f.name],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)


if __name__=='__main__':unittest.main()
