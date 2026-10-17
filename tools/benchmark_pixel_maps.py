#!/usr/bin/env python3
"""Compare native fill, scalar loops and optimized maps on the same host build.

Each variant gets a fresh instance, equal timestamps and the same output count.
Timing uses engine CPU counters, excluding Python/transport/snapshot latency.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from lightvm.compiler import compile_source
from client import Client, ProcessTransport, FRAME, STOP


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--process',type=Path,default=ROOT/'build/composition-host/lightvm')
    parser.add_argument('--pixels',type=int,default=200)
    parser.add_argument('--frames',type=int,default=300,help='frames per timed window')
    parser.add_argument('--rounds',type=int,default=7)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if min(args.pixels,args.frames,args.rounds)<1:parser.error('counts must be positive')
    rainbow=(ROOT/'examples/layers/rainbow.py').read_text()
    native=rainbow[:rainbow.index('    stride =')]+'''    pixels.colorwheel(phase, ctx.scale * 255.0 / runtime.pixel_count)
'''
    sprite=(ROOT/'examples/layers/rainbow_sprite.py').read_text()
    nonlinear='''from lightvm import profile,render,hsv
profile("layer/1")
@render
def draw(ctx,runtime,pixels):
 phase=runtime.time%1.0
 for i in range(pixels.count):
  pixels[i]=hsv((i/pixels.count)*(i/pixels.count)+phase,0.6,0.8)
'''
    variants=[('rainbow/native',native,False),('rainbow/scalar',rainbow,False),
              ('rainbow/bulk',rainbow,True),('nonlinear/scalar',nonlinear,False),
              ('nonlinear/bulk',nonlinear,True),('sprite/scalar',sprite,False),('sprite/bulk',sprite,True)]
    client=Client(ProcessTransport(args.process.resolve()))
    report={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'process':str(args.process.resolve()),
            'executable_sha256':hashlib.sha256(args.process.read_bytes()).hexdigest(),
            'pixels':args.pixels,'frames_per_window':args.frames,'rounds':args.rounds,
            'timing':'engine render_total_us deltas / render_count deltas; host CPU, not hardware fps',
            'variants':{}}
    try:
        target=client.describe()
        if 'lightvm.program/9' not in target['program_schemas']:raise RuntimeError('build the receiver with pixel-map support first')
        client.configure({'schema':'lightvm.config/1','devices':[{
            'id':'benchmark','type':'ws2812b','config':{'pin':'GP16','count':args.pixels}}]})
        expected={}
        for name,source,optimize in variants:
            artifact=compile_source(source,optimize=optimize)
            if not artifact.success:raise RuntimeError(artifact.diagnostics)
            client.save_procedure(name.replace('/','_'),artifact.bytecode)
            report['variants'][name]={'bytecode_bytes':len(artifact.bytecode),'schema':artifact.manifest['schema'],
                                      'source_sha256':hashlib.sha256(source.encode()).hexdigest(),'windows':[]}
            client.configure_scene([{'id':'fg','procedure':name.replace('/','_')}],device='benchmark')
            client.require_ok(client.command(STOP,device='benchmark'))
            frames=[]
            for delta in (0,17,333,1000):
                client.require_ok(client.command(FRAME,struct.pack('<I',delta),device='benchmark'))
                frames.append(bytes(client.snapshot(device='benchmark',layer='fg')[1]))
            family=name.split('/')[0]
            if family in expected and frames!=expected[family]:raise AssertionError('pixel mismatch: '+name)
            expected[family]=frames
        for round_index in range(args.rounds):
            order=variants if round_index%2==0 else list(reversed(variants))
            for name,_,_ in order:
                client.configure_scene([{'id':'fg','procedure':name.replace('/','_')}],device='benchmark')
                client.require_ok(client.command(STOP,device='benchmark'))
                for _ in range(10):client.require_ok(client.command(FRAME,struct.pack('<I',16),device='benchmark'))
                before=client.program_parameters(device='benchmark',layer='fg')
                heap=client.status_fields()['heap']
                for _ in range(args.frames):client.require_ok(client.command(FRAME,struct.pack('<I',16),device='benchmark'))
                after=client.program_parameters(device='benchmark',layer='fg')
                if heap!=client.status_fields()['heap']:raise AssertionError('heap changed during rendering')
                frames=after['render_count']-before['render_count']
                report['variants'][name]['windows'].append({
                    'render_us_per_frame':(after['render_total_us']-before['render_total_us'])/frames,
                    'dispatches_per_frame':(after['dispatch_count']-before['dispatch_count'])/frames,
                    'native_us_per_frame':(after['native_draw_us']-before['native_draw_us'])/frames,
                    'heap_bytes':int(heap)})
        for name,data in report['variants'].items():
            for metric in ('render_us_per_frame','dispatches_per_frame','native_us_per_frame','heap_bytes'):
                data[metric]=statistics.median(w[metric] for w in data['windows'])
            print(f"{name:20s} {data['render_us_per_frame']:9.3f} us/frame  {data['dispatches_per_frame']:8.1f} dispatches  {data['heap_bytes']:6.0f} B heap")
        report['pixel_equality']='exact at all four timestamps within each workload family'
    finally:client.close()
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
