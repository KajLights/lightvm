#!/usr/bin/env python3
"""One-boot ESP32 endurance with frozen mixed scenes and native temperatures."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import time
import board_monitor
from benchmark_animation_profile import EMPTY, STABLE_STARS
from benchmark_controller import DEFAULT_PINS
from benchmark_saved_preset import sample
from client import Client, SerialTransport


def run(args):
    manifest=json.loads((args.packages/'manifest.json').read_text())
    names=['map','scalar','control-color','control-stars']
    packages={name:(args.packages/(name+'.lbc')).read_bytes() for name in names}
    for name,data in packages.items():
        if hashlib.sha256(data).hexdigest()!=manifest[name]['bytecode_sha256']:
            raise ValueError('package mismatch: '+name)
    result={'started_utc':datetime.now(timezone.utc).isoformat(),'board':args.board,
            'firmware_sha256':hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
            'requested_seconds':args.seconds,'packages':{n:manifest[n] for n in names},
            'pixels':args.pixels,'pin':DEFAULT_PINS[args.board],'poll_hz':20,'blocks':[]}
    def checkpoint():args.result.write_text(json.dumps(result,indent=2)+'\n')
    c=Client(SerialTransport(args.port,no_reset=True),timeout=30)
    board_monitor.attach(c,args)
    created=[]
    started=time.monotonic()
    try:
        original=c.get_config();description=c.describe();result['description_before']=description
        if description['board']!=args.board:raise ValueError('unexpected board')
        if {'soak_'+n for n in names}.intersection(p['name'] for p in c.programs()):
            raise ValueError('temporary endurance program already exists')
        c.configure(EMPTY)
        for name,data in packages.items():
            c.save_program('soak_'+name,data);created.append('soak_'+name)
        c.configure({'schema':'lightvm.config','outputs':[{'id':'soak','type':'ws2812b',
                     'config':{'pin':result['pin'],'count':args.pixels,'brightness_limit':32}}]})
        measured=0;heaps={}
        while measured<args.seconds:
            index=len(result['blocks']);scene='mixed' if index%2==0 else 'stars-background'
            layers=([{'id':'map','program':'soak_map'},{'id':'scalar','program':'soak_scalar'}]
                    if scene=='mixed' else [{'id':'background','program':'soak_control-color'},
                         {'id':'stars','program':'soak_control-stars','settings':STABLE_STARS}])
            setup=time.monotonic();c.configure_composition(layers,output='soak')
            c.set_light(brightness=.5 if index%4<2 else 1.0,output='soak')
            setup_ms=(time.monotonic()-setup)*1000
            board_monitor.sleep(c,5)
            row=sample(c,'soak',min(60,args.seconds-measured),poll_hz=20)
            row.update(scene=scene,setup_ms=setup_ms,block=index+1)
            result['blocks'].append(row)
            if row['description_after']['boot_id']!=description['boot_id']:raise RuntimeError('controller rebooted')
            heap={h['heap'] for h in row['heap_samples']}
            if len(heap)!=1:raise RuntimeError('VM heap changed inside steady window')
            if scene in heaps and heap!=heaps[scene]:raise RuntimeError('VM heap grew across scene replacements')
            heaps[scene]=heap
            dynamic=[v for k,v in row['layers'].items() if not k.endswith('/background')]
            if any(v['updates']<=0 for v in dynamic):raise RuntimeError('dynamic layer stalled')
            native_started=time.monotonic();c.manage('input_list')
            row['native_telemetry_query_ms']=(time.monotonic()-native_started)*1000
            row['native_sensors_after']=json.loads(c.data_reply)
            measured+=row['seconds'];result['measured_seconds']=measured
            result['elapsed_seconds']=time.monotonic()-started;checkpoint()
            print(json.dumps({'block':index+1,'scene':scene,'measured_seconds':round(measured,1),
                  'heap':list(heap)[0],'usb_p95_ms':row['status_latency_ms']['p95'],
                  'updates_hz':{k:v['updates_per_second'] for k,v in row['layers'].items()}}),flush=True)
        result['description_after']=c.describe();result['passed']=True
    except BaseException as error:
        result['passed']=False;result['error']=str(error);raise
    finally:
        try:
            c.configure(EMPTY)
            if 'original' in locals():c.configure(original)
            for name in created:c.delete_program(name)
            if 'original' in locals() and c.get_config()!=original:raise RuntimeError('config cleanup differs')
            if set(created).intersection(p['name'] for p in c.programs()):raise RuntimeError('temporary programs remain')
            result['cleanup_complete']=True
        except BaseException as error:
            result['cleanup_complete']=False;result['cleanup_error']=str(error);raise
        finally:
            try:c.close()
            finally:checkpoint()


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--board',required=True,choices=['xiao_esp32c3','xiao_esp32c6','xiao_esp32s3'])
    parser.add_argument('--firmware',required=True,type=Path)
    parser.add_argument('--packages',required=True,type=Path)
    parser.add_argument('--result',required=True,type=Path)
    parser.add_argument('--seconds',type=float,default=3600)
    parser.add_argument('--pixels',type=int,default=200)
    board_monitor.add_arguments(parser)
    args=parser.parse_args()
    if not 1<=args.pixels<=1024:parser.error('pixels must be 1..1024')
    if not args.temperature_log:parser.error('--temperature-log is required')
    if args.result.exists():parser.error('result exists; retain earlier attempts')
    if not math.isfinite(args.seconds) or args.seconds<=0:parser.error('finite positive duration required')
    run(args)
