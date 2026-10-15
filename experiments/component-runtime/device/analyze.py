#!/usr/bin/env python3
"""Aggregate measured C3 runs without substituting host timing or memory."""
import argparse
import json
from pathlib import Path
import statistics

ROOT=Path(__file__).resolve().parents[3]
BUILD=ROOT/'build/component-runtime/device'

def summary(values):
    v=sorted(values)
    if not v:return None
    return {'n':len(v),'median':statistics.median(v),'p95':v[int((len(v)-1)*.95)],
            'p99':v[int((len(v)-1)*.99)],'max':v[-1]}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,default=BUILD,
                        help='Build directory or saved device result directory')
    parser.add_argument('--output',type=Path,default=BUILD/'comparison.json')
    args=parser.parse_args()
    result={'status':'passed','scope':'one ESP32-C3, 160 MHz, 200 pixels on GPIO3, two effects, USB50Hz, RAM replacement',
            'ram_method':'static shared instruction/data RAM + internal heap total minus free; SDK minimum is conservative sum of regional low-watermarks',
            'backends':{}}
    for backend in ('wamr','native','typed'):
        raw=json.loads((args.input/f'{backend}-measurements.json').read_text())
        if raw['status']!='passed' or len(raw['runs'])!=3:raise ValueError(f'{backend}: incomplete measurements')
        size_path=args.input/f'{backend}-size.json'
        if not size_path.exists():size_path=args.input/f'{backend}-firmware/size.json'
        size=json.loads(size_path.read_text())
        build=json.loads((args.input/'builds.json').read_text())[backend]
        static=size['used_diram']+size['used_iram']+size['used_dram']
        runs=raw['runs'];frames=[s for r in runs for s in r['samples']]
        events=[e for r in runs for e in r['replacement_events']]
        pings=[p for r in runs for p in r['usb_pings']]
        def gaps(field):return [b[field]-a[field] for r in runs for a,b in zip(r['samples'],r['samples'][1:])]
        section={'repetitions':len(runs),'physical_outputs_measured':len(frames),
                 'static_ram_bytes':static,'firmware_image_bytes':build['artifacts']['component_device.bin']['bytes'],
                 'resident_ram_before_components_bytes':max(static+r['stats']['heap_total']-r['stats']['heap_baseline_free'] for r in runs),
                 'peak_ram_conservative_bytes':max(static+r['stats']['heap_total']-r['stats']['heap_sdk_min_free'] for r in runs),
                 'peak_ram_sampled_bytes':max(static+r['stats']['heap_total']-r['stats']['heap_min_free'] for r in runs),
                 'phase_ram_sampled_bytes':{name:max(static+r['stats']['heap_total']-r['service_gaps'][i]['heap_min_free'] for r in runs) for i,name in enumerate(('baseline','replacement','after'))},
                 'smallest_largest_free_data_block_bytes':min(r['stats']['largest_min'] for r in runs),
                 'worker_stack_spare_min_bytes':min(r['stats']['worker_stack_free_min'] for r in runs),
                 'largest_component_step_elapsed_us':max(r['stats']['max_step_us'] for r in runs),
                 'memory_probe_elapsed_us_by_run':[r['stats']['memory_probe_us'] for r in runs],
                 'memory_probe_max_elapsed_us':max(r['stats']['memory_probe_max_us'] for r in runs),
                 'render_call_elapsed_us':summary([s['call_elapsed_us'] for s in frames]),
                 'rmt_submit_to_callback_us':summary([s['complete_us']-s['submit_us'] for s in frames]),
                 'completion_gap_us':summary(gaps('complete_us')),
                 'completion_gaps_over_25ms':sum(x>25000 for x in gaps('complete_us')),
                 'fps_by_run':[(len(r['samples'])-1)*1e6/(r['samples'][-1]['complete_us']-r['samples'][0]['complete_us']) for r in runs],
                 'load_elapsed_us':summary([e['load_us'] for e in events]),
                 'preparation_poll_elapsed_us':summary([e['prepare_us'] for e in events]),
                 'activation_elapsed_us':summary([e['activate_us'] for e in events]),
                 'load_start_to_activation_us':summary([e['end_us']-e['begin_us'] for e in events]),
                 'retained_handoffs':sum(e['pending'] for e in events),
                 'service_gap_max_us':{name:max(r['service_gaps'][i]['max_us'] for r in runs) for i,name in enumerate(('baseline','replacement','after'))},
                 'usb_roundtrip_us':{name:summary([p['latency_us'] for p in pings if p['phase']==name]) for name in ('baseline','replacement','after')},
                 'usb_requests_acknowledged':len(pings),
                 'errors':sum(r['stats']['errors']+r['stats']['hash_failures']+r['stats']['tx_drops']+r['stats']['overflow'] for r in runs)}
        section['frame_gap_by_phase_us']={}
        for i,name in enumerate(('baseline','replacement','after')):
            values=[]
            for run in runs:
                boundaries=[0,run['phases'][0]['at_us'],run['phases'][1]['at_us'],2**32]
                values += [b['complete_us']-a['complete_us'] for a,b in zip(run['samples'],run['samples'][1:])
                           if boundaries[i]<=b['submit_us']<boundaries[i+1]]
            section['frame_gap_by_phase_us'][name]=summary(values)
        result['backends'][backend]=section
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
