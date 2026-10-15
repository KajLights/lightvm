#!/usr/bin/env python3
"""Focused, checkpointed runtime comparisons using device-side timing."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import statistics
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
import wasmtime
from lightvm.compiler import compile_source
from client import Client,SerialTransport,ProcessTransport,FRAME,PLAY,STOP,RESET,MANAGE
from benchmark_mqtt import measure,state

def summary(values):
    ordered=sorted(values)
    return {'median':statistics.median(values),'p95':ordered[(95*len(values)+99)//100-1],
            'maximum':max(values),'samples':len(values)}

def run(client,args):
    d=client.describe()
    if not d.get('runtime_benchmark'):raise RuntimeError('Benchmark firmware required')
    original=client.get_config()
    record={'timestamp_utc':datetime.now(timezone.utc).isoformat(),'label':args.label,
            'board':d['board'],'flash_programs':d.get('flash_programs'),
            'interpreter':'toywasm' if d.get('flash_interpreter') else 'wasm3',
            'heap_limit':d['vm_heap_limit'],'samples':args.samples,'repetitions':args.repetitions,
            'seconds_per_window':args.seconds,'native_reference':'unsandboxed C; same kernels.c, not WASM AOT',
            'network_testing':'no external broker; service reconnect uses device loopback',
            'kernels':[],'animations':[],'service_admissions':[],'service_windows':[]}
    def save():
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(record,indent=2)+'\n')
    def configure(count):
        pin='D1' if d['board']=='xiao_esp32c3' else 'GP16'
        client.configure({'schema':'lightvm.config/1','devices':[{'id':'benchmark','type':'ws2812b',
            'config':{'pin':pin,'count':count,'order':'GRB','brightness_limit':32}}],
            'targets':{'A':{'device':'benchmark'}}})
    def clean():
        client.manage('service_unload');client.require_ok(client.command(RESET))
        if int(client.status_fields()['heap'])!=0:raise RuntimeError('VM allocations not released')
    def upload(data):
        start=time.monotonic_ns();client.upload(data)
        return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                'upload_wall_ms':(time.monotonic_ns()-start)/1e6,
                'after_load':state(client)}
    try:
        clean();configure(100)
        record['idle']=state(client)
        module=(ROOT/'build/runtime-bench/kernels.wasm').read_bytes()
        record['kernel_module']=upload(module)
        for name in (() if args.service_only else ('bench_integer','bench_float','bench_memory','bench_import')):
            for repetition in range(args.repetitions):
                client.manage('runtime_bench',export=name,iterations=2048,samples=args.samples)
                item=json.loads(client.data_reply)
                item.update(kernel=name,repetition=repetition+1)
                item['guest_summary_us']=summary(item['guest_us']);item['native_summary_us']=summary(item['native_us'])
                record['kernels'].append(item);save()
            print(f'{args.label}: {name} measured',flush=True)
        clean();record['after_kernel_unload']=state(client)
        python_wasm=compile_source((ROOT/'samples/python/rainbow.py').read_text()).wasm
        legacy=bytes(wasmtime.wat2wasm((ROOT/'samples/rainbow.wat').read_text()))
        for name,code,count in ([] if args.service_only else [('legacy_integer',legacy,100),('python_control',python_wasm,100),('python_control',python_wasm,300)]):
            clean();configure(count)
            item={'workload':name,'pixels':count,'module':upload(code),'render_us':[],'show_us':[],
                  'frame_sha256':[],'windows':[]}
            for i in range(args.samples+3):
                client.require_ok(client.command(FRAME,(16).to_bytes(4,'little')))
                fields=client.status_fields()
                if i>=3:
                    item['render_us'].append(int(fields['render_us']));item['show_us'].append(int(fields['show_us']))
                    item['frame_sha256'].append(hashlib.sha256(client.pixel_snapshot).hexdigest())
            item['render_summary_us']=summary(item['render_us']);item['show_summary_us']=summary(item['show_us'])
            item['after_execution']=state(client)
            client.require_ok(client.command(PLAY))
            for repetition in range(args.repetitions):
                time.sleep(1)
                item['windows'].append(measure(client,args.seconds));
                print(f'{args.label}: {name} {count}px window {repetition+1}',flush=True)
            record['animations'].append(item);save()
        clean();configure(100);client.upload(python_wasm);client.require_ok(client.command(PLAY))
        service=args.service.read_bytes()
        record['service_module']={'bytes':len(service),'sha256':hashlib.sha256(service).hexdigest()}
        for repetition in range(args.repetitions):
            client.manage('service_unload');time.sleep(1)
            item={'repetition':repetition+1,'before':state(client)}
            def admit(elapsed):
                if 'status' in item:return
                start=time.monotonic_ns()
                client.upload_object('service',service)
                reply=client.command(MANAGE,b'{"op":"service_load"}')
                item.update(status=reply.code,message=reply.message,upload_and_load_wall_ms=(time.monotonic_ns()-start)/1e6)
            item['window']=measure(client,args.seconds,admit)
            item['after']=state(client)
            record['service_admissions'].append(item);save()
            print(f'{args.label}: MQTT admission {repetition+1} status {item["status"]}',flush=True)
        if state(client)['loaded']:
            record['loaded_disconnected']=measure(client,args.seconds);save()
            reply=client.command(MANAGE,b'{"op":"service_start","ipv4":2130706433,"port":18883}')
            record['service_start']={'status':reply.code,'message':reply.message,'after':state(client)};save()
            if not reply.code:
                for repetition in range(args.repetitions):
                    window=measure(client,args.seconds)
                    record['service_windows'].append(window);save()
                    print(f'{args.label}: disconnected MQTT execution window {repetition+1}',flush=True)
            if args.probe:
                client.manage('service_stop');client.require_ok(client.command(STOP))
                reply=client.command(MANAGE,b'{"op":"runtime_service_probe"}')
                record['service_probe']={'status':reply.code,'message':reply.message}
                if not reply.code:record['service_probe']['timings']=json.loads(client.data_reply)
                save()
        clean();record['after_all_unload']=state(client)
        record['completed']=True;save()
    except Exception as error:
        record['error']=str(error);save();raise
    finally:
        try:
            clean();client.configure(original)
        except Exception as error:
            record['cleanup_error']=str(error);save()
    return record

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    target=p.add_mutually_exclusive_group(required=True)
    target.add_argument('--port');target.add_argument('--process',type=Path)
    p.add_argument('--label',required=True);p.add_argument('--output',required=True,type=Path)
    p.add_argument('--samples',type=int,default=21);p.add_argument('--repetitions',type=int,default=3)
    p.add_argument('--seconds',type=float,default=5)
    p.add_argument('--service',type=Path,default=ROOT/'build/mqtt/service.wasm')
    p.add_argument('--service-only',action='store_true')
    p.add_argument('--probe',action='store_true',help='Measure initialization separately under an explicit diagnostic 500 ms ceiling')
    args=p.parse_args()
    if not 1<=args.samples<=21 or args.repetitions<1 or not 0<args.seconds<=30:p.error('invalid sample/window counts')
    transport=SerialTransport(args.port,no_reset=True) if args.port else ProcessTransport(args.process)
    client=Client(transport,timeout=15)
    try:run(client,args)
    finally:client.close()
