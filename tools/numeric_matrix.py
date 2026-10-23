#!/usr/bin/env python3
"""Freeze the specified parameter matrix and replay it on the native test port.

These are correctness/admission results, never board timing evidence. Generated
packages can also be passed to benchmark_numeric.py for a measured board campaign.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lightvm.portable import PortableCompiler

PREFIX='''import math
import random
from array import array
from lightvm import profile,init,render,rgb,fixed,fixed_from_bits,FixedArray
profile("animation")
'''


def workloads():
    for length in (3,17,64):
        init=f"@init\ndef setup(ctx,runtime):\n ctx.table=array('I',[0]*{length})\n for j in range({length}):\n  ctx.table[j]=(((j*73+17)%256)<<16)|(((j*37+11)%256)<<8)|((j*19+5)%256)\n"
        for offset in (-19,0,23):
            yield f'G1-l{length}-o{offset}', PREFIX+init+f'''@render
def draw(ctx,runtime,pixels):
 offset={offset}+int(runtime.time*0.0)
 for i in range(pixels.count):
  color=ctx.table[(i+offset)%{length}]
  pixels[i]=rgb((color>>16)&255,(color>>8)&255,color&255)
''',True,1
        yield f'G2-l{length}',PREFIX+init+f'''@render
def draw(ctx,runtime,pixels):
 offset=int(runtime.time*0.0)
 for i in range(pixels.count):
  color=ctx.table[(i*{length}//pixels.count+offset)%{length}]
  pixels[i]=rgb((color>>16)&255,(color>>8)&255,color&255)
''',True,1
    for phase in (0,1,32768,65535,65536):
        yield f'Q1-p{phase}',PREFIX+f'''@init
def setup(ctx,runtime):ctx.last=fixed(0)
@render
def draw(ctx,runtime,pixels):
 p=fixed(0)
 phase=fixed_from_bits({phase})+fixed(runtime.time*0.0)
 for i in range(pixels.count):
  p=(phase+fixed(i)/fixed(pixels.count))%fixed(1)
  envelope=fixed(2)*p if p<fixed(0.5) else fixed(2)*(fixed(1)-p)
  pixels[i]=rgb(int(fixed(255)*envelope),0,0)
 ctx.last=p
''',True,1
    yield 'Q2-five-impulses',PREFIX+'''@init
def setup(ctx,runtime):
 ctx.x=fixed_from_bits(52429)
 ctx.v=fixed(0)
 ctx.step=0
 ctx.impulses=array('i',[0,1311,-1311,655,-655])
 ctx.positions=FixedArray(5)
 ctx.velocities=FixedArray(5)
@render
def draw(ctx,runtime,pixels):
 ctx.v=(ctx.v+fixed_from_bits(ctx.impulses[ctx.step%5]))*fixed_from_bits(58982)
 ctx.x=min(fixed(1),max(fixed_from_bits(36045),ctx.x+ctx.v*fixed_from_bits(22938)))
 ctx.positions[ctx.step%5]=ctx.x
 ctx.velocities[ctx.step%5]=ctx.v
 ctx.step+=1
 for i in range(pixels.count):
  shade=ctx.x if i%2==0 else fixed(1)-ctx.x
  pixels[i]=rgb(int(shade*fixed(255)),10,30)
''',True,5
    yield 'B1-lazy-piecewise',PREFIX+'''from lightvm.easing import circular_out
@init
def setup(ctx,runtime):ctx.table=array('i',[3,7,11])
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
  x=i/float(pixels.count)
  shade=0
  if i<0:shade=ctx.table[999]
  else:
   if x<0.5:shade=int(circular_out(x)*255.0)
   else:shade=ctx.table[i%3]
  pixels[i]=rgb(shade,0,0)
''',True,1
    for length in (33,64,128,129):
        even=length%2==0
        body='  x=i\n'+'  x=x+1\n'*((length-5-int(even))//2)
        if even:body+='  x=x\n'
        body+='  x=x%255\n  pixels[i]=rgb(x,0,0)\n'
        yield f'B2-body{length}',PREFIX+'@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n'+body,length<=128,1
    yield 'M1-all-pairs',PREFIX+'''@init
def setup(ctx,runtime):
 ctx.u1=array('f',[0.0000152587890625,0.125,0.5,0.9999847412109375])
 ctx.u2=array('f',[0.0,0.125,0.5,0.875])
 ctx.x=0.0
 ctx.y=0.0
@render
def draw(ctx,runtime,pixels):
 x=0.0
 y=0.0
 offset=int(runtime.time*0.0)
 for i in range(pixels.count):
  radius=math.sqrt(-2.0*math.log(ctx.u1[(i+offset)%4]))
  angle=(2.0*3.1415927410125732)*ctx.u2[(i//4)%4]
  x=radius*math.cos(angle)
  y=radius*math.sin(angle)
  pixels[i]=rgb(int(128.0+20.0*x),int(128.0+20.0*y),40)
 ctx.x=x
 ctx.y=y
''',True,1
    yield 'M2-all-pairs',PREFIX+'''@init
def setup(ctx,runtime):
 ctx.exponents=array('f',[0.5,1.0,2.0,4.0])
 ctx.phases=array('f',[0.0,0.125,0.25,0.5,0.75,0.875])
 ctx.last=0.0
@render
def draw(ctx,runtime,pixels):
 value=0.0
 offset=int(runtime.time*0.0)
 for i in range(pixels.count):
  angle=(2.0*3.1415927410125732)*ctx.phases[((i+offset)//4)%6]
  s=math.sin(angle)
  value=math.pow(abs(s),ctx.exponents[i%4])
  red=int(value*255.0) if s<0.0 else 0
  blue=0 if s<0.0 else int(value*255.0)
  pixels[i]=rgb(red,20,blue)
 ctx.last=value
''',True,1
    ordered={
        'state':'  ctx.value+=1\n  pixels[i]=rgb(ctx.value%255,0,0)\n',
        'rng':'  pixels[i]=rgb(random.randrange(255),0,0)\n',
        'sparse':'  if i%2==0:pixels[i]=rgb(1,2,3)\n',
        'scatter':'  pixels[(i+1)%pixels.count]=rgb(i%255,0,0)\n',
        'feedback':'  ctx.table[i%3]+=1\n  pixels[i]=rgb(ctx.table[i%3]%255,0,0)\n',
    }
    for name,body in ordered.items():
        yield 'O1-'+name,PREFIX+'''@init
def setup(ctx,runtime):
 ctx.value=0
 ctx.table=array('i',[0,1,2])
 random.seed(12345)
@render
def draw(ctx,runtime,pixels):
 for i in range(pixels.count):
'''+body,False,1
    for name in ('color','rainbow','stars','sweep'):
        yield 'control-'+name,(ROOT/f'examples/animations/{name}.py').read_text(),None,1


def digest(data):return hashlib.sha256(data).hexdigest()


def prepare(directory):
    directory.mkdir(parents=True,exist_ok=True);manifest={}
    for case,source,expected,frames in workloads():
        (directory/(case+'.py')).write_text(source)
        for mode,optimize in (('scalar',False),('bulk',True)):
            compiler=PortableCompiler(source,optimize=optimize);data,metadata=compiler.compile()
            maps=[row for row in compiler.code if row[0]==39]
            if not optimize and maps:raise AssertionError((case,'scalar map'))
            if optimize and expected is not None and bool(maps)!=expected:raise AssertionError((case,compiler.map_reports))
            if case.startswith('B2') and maps:
                assert maps[0][4]-compiler.code.index(maps[0])-1==int(case.split('body')[1])
            name=case+'-'+mode;(directory/(name+'.lbc')).write_bytes(data)
            manifest[name]={'source_sha256':digest(source.encode()),'bytecode_sha256':digest(data),
                            'bytes':len(data),'maps':len(maps),'requires':metadata['requires'],'frames':frames}
    (directory/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest


def replay(driver,directory,counts,result):
    manifest=json.loads((directory/'manifest.json').read_text());rows=[]
    for name in manifest:
        if not name.endswith('-bulk'):continue
        case=name[:-5]
        for count in counts:
            expected=None
            for mode,workers,tick in (('scalar',1,0),('bulk',1,0),('bulk',2,0),('bulk',1,1000),('bulk',2,1000)):
                path=directory/(case+'-'+mode+'.lbc')
                assert digest(path.read_bytes())==manifest[case+'-'+mode]['bytecode_sha256']
                output=subprocess.check_output([str(driver),'--numeric-frames',str(path),str(count),str(tick),str(workers),str(manifest[name]['frames'])],text=True)
                value=json.loads(output)
                if not value.get('load') or not value.get('prepare') or value['status']!=1:raise AssertionError((case,count,mode,value))
                plan_bytes=value.pop('plan_bytes')
                if expected is None:expected=value
                elif value!=expected:raise AssertionError((case,count,mode,workers,tick,'scalar mismatch'))
                if case.startswith(('G1-','G2-')):
                    length=int(case.split('-l')[1].split('-o')[0])
                    offset=int(case.split('-o')[1]) if case.startswith('G1-') else 0
                    expected_pixels=bytearray()
                    for i in range(count):
                        at=(i+offset)%length if case.startswith('G1-') else i*length//count
                        expected_pixels.extend(((at*73+17)%256,(at*37+11)%256,(at*19+5)%256,255))
                    assert value['pixels']==expected_pixels.hex()
                if case.startswith('Q1-'):
                    phase=int(case.split('-p')[1]);expected_pixels=bytearray()
                    for i in range(count):
                        phase_value=(phase+i*65536//count)%65536
                        envelope=2*phase_value if phase_value<32768 else 2*(65536-phase_value)
                        expected_pixels.extend((255*envelope//65536,0,0,255))
                    assert value['pixels']==expected_pixels.hex() and value['state']==[phase_value]
                if case=='Q2-five-impulses':
                    x,v=52429,0;xs=[];vs=[]
                    def mul(a,b):return (abs(a*b)//65536)*(-1 if (a<0)!=(b<0) else 1)
                    for impulse in (0,1311,-1311,655,-655):
                        v=mul(v+impulse,58982);x=min(65536,max(36045,x+mul(v,22938)))
                        xs.append(x&0xffffffff);vs.append(v&0xffffffff)
                    assert value['arrays']==[xs,vs]
                rows.append({'case':case,'pixels':count,'mode':mode,'workers':workers,'tick':tick,
                             'plan_bytes':plan_bytes,'result_sha256':digest(json.dumps(value,sort_keys=True).encode())})
    report={'scope':'host correctness; no hardware timing claim','default_seed':0x5eed,'first_time_seconds':1.25,'delta_seconds':0.25,'driver_sha256':digest(driver.read_bytes()),'packages':manifest,'replays':rows,'passed':True}
    result.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'cases':len(manifest)//2,'replays':len(rows),'passed':True}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages',type=Path,required=True);parser.add_argument('--prepare',action='store_true')
    parser.add_argument('--driver',type=Path);parser.add_argument('--result',type=Path)
    parser.add_argument('--pixels',type=int,nargs='+',default=[1,31,32,33,63,64,65,127,128,129,200,500,1024])
    args=parser.parse_args()
    if args.prepare:prepare(args.packages)
    else:
        if not args.driver or not args.result:parser.error('replay requires --driver and --result')
        if any(n<1 or n>1024 for n in args.pixels):parser.error('pixel counts must be 1..1024')
        replay(args.driver,args.packages,args.pixels,args.result)
