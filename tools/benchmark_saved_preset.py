#!/usr/bin/env python3
"""Measure a saved preset on hardware without replacing its program artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics
import time
from client import Client, SerialTransport


def percentile(values, fraction):
    ordered=sorted(values)
    return ordered[min(len(ordered)-1, max(0, __import__('math').ceil(len(ordered)*fraction)-1))]


def sample(client, output, seconds, *, diagnostics=False, poll_hz=20):
    def state():
        began=time.monotonic();value=client.transition(output=output);ended=time.monotonic()
        return value,(began+ended)/2,ended-began
    description_before=client.describe();identity=description_before['boot_id']
    before,started,before_query=state()
    latencies=[];observations=[];heaps=[];foreground=[];deadline=started+seconds;next_poll=time.monotonic()
    while time.monotonic()<deadline:
        if not poll_hz:
            time.sleep(min(.1,max(0,deadline-time.monotonic())))
            continue
        time.sleep(max(0,next_poll-time.monotonic()))
        tick=time.monotonic();status=client.status_fields(output=output);latencies.append((time.monotonic()-tick)*1000)
        if status.get('animation_error')!='-':raise RuntimeError(status)
        heaps.append({key:int(status[key]) for key in ('heap','heap_peak','heap_limit') if key in status})
        if 'fg10' in status:foreground.append({'elapsed_s':time.monotonic()-started,'over_10ms':int(status['fg10']),'ingress_over_10ms':int(status['io10']) if 'io10' in status else None})
        if diagnostics:
            transition,when,cost=state();observations.append({'elapsed_s':when-started,'query_s':cost,'transition':transition})
        next_poll+=1/poll_hz
        if next_poll<time.monotonic():next_poll=time.monotonic()+1/poll_hz
    after,ended,after_query=state();description=client.describe()
    if identity!=description['boot_id']:raise RuntimeError('controller rebooted during sample')
    elapsed=ended-started;layers={}
    for slot in ('A','B'):
        old={row['id']:row for row in before[slot].get('layers',[])}
        for row in after[slot].get('layers',[]):
            prev=old[row['id']]
            if prev['generation']!=row['generation']:raise RuntimeError('scene changed during sample')
            frames=row['frames']-prev['frames'];execution=row['render_total_us']-prev['render_total_us']
            layers[slot+'/'+row['id']]={'updates_per_second':frames/elapsed,'updates':frames,'execution_us_per_second':execution/elapsed,'mean_render_us':execution/frames if frames else None,'before':prev,'after':row}
    # An A-only composition exposes its physical counter under A; the root
    # frames field belongs to a transition and is zero when none exists.
    before_presented=before['frames'] if before['active'] else before['A']['frames']
    after_presented=after['frames'] if after['active'] else after['A']['frames']
    return {'seconds':elapsed,'boundary_query_seconds':[before_query,after_query],
        'transition_before':before,'transition_after':after,
        'presentation_counter_path':'frames' if after['active'] else 'A.frames',
        'presentations_per_second':(after_presented-before_presented)/elapsed,'layers':layers,
        'status_latency_ms':{'count':len(latencies),'median':statistics.median(latencies) if latencies else None,'p95':percentile(latencies,.95) if latencies else None,'p99':percentile(latencies,.99) if latencies else None,'max':max(latencies) if latencies else None,'samples':latencies},
        'poll_hz':poll_hz,'heap_samples':heaps,'foreground_gap_samples':foreground,'diagnostics':observations,'description_before':description_before,'description_after':description}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True);parser.add_argument('--preset',default='stars');parser.add_argument('--output-id',default='lights')
    parser.add_argument('--firmware',type=Path,required=True);parser.add_argument('--label',required=True);parser.add_argument('--result',type=Path,required=True)
    parser.add_argument('--samples',type=int,default=5);parser.add_argument('--seconds',type=float,default=30);parser.add_argument('--warmup',type=float,default=7);parser.add_argument('--diagnostics',action='store_true')
    args=parser.parse_args()
    if args.samples<1 or args.seconds<=0 or args.warmup<0:parser.error('invalid measurement duration/count')
    client=Client(SerialTransport(args.port),timeout=30)
    result={'label':args.label,'firmware_sha256':hashlib.sha256(args.firmware.read_bytes()).hexdigest(),'workload':'unchanged saved preset '+args.preset,'warmup_seconds':args.warmup,'diagnostic_sampling':args.diagnostics,'samples':[]}
    try:
        result['initial_description']=client.describe();result['config']=client.get_config();result['preset']=client.preset(args.preset)
        current=next(o for o in result['initial_description']['outputs'] if o['id']==args.output_id)
        restore=current['selected_preset'];result['artifacts']=[]
        for p in client.programs():
            data=client.get_program(p['name'],p['type']);result['artifacts'].append({**p,'sha256':hashlib.sha256(data).hexdigest()})
        for trial in range(args.samples):
            client.recall_preset(args.preset,args.output_id);time.sleep(args.warmup)
            row=sample(client,args.output_id,args.seconds,diagnostics=args.diagnostics);row['trial']=trial+1;result['samples'].append(row)
            args.result.write_text(json.dumps(result,indent=2)+'\n')
            print(json.dumps({'label':args.label,'trial':trial+1,'presentations_per_second':row['presentations_per_second'],'layers':{k:v['updates_per_second'] for k,v in row['layers'].items()},'status_p95_ms':row['status_latency_ms']['p95']}),flush=True)
    finally:
        if 'restore' in locals() and restore:client.recall_preset(restore,args.output_id)
        client.close()


if __name__=='__main__':main()
