#!/usr/bin/env python3
"""Exercise ESP32 layer fairness and asynchronous preparation via explicit pins."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import zlib
import board_monitor
from benchmark_saved_preset import sample
from benchmark_controller import DEFAULT_PINS
from client import Client, SerialTransport, ProcessTransport, MANAGE, ProtocolError
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from lightvm.compiler import compile_source

EMPTY={'schema':'lightvm.config','outputs':[]}
HEAVY='''from lightvm import profile,init,render,rgb,FixedArray
profile("animation")
@init
def setup(ctx,runtime):
 ctx.buffer=FixedArray(2048)
 value=0
 for i in range(30000):
  value=(value+i)%251
 ctx.value=value
@render
def draw(ctx,runtime,pixels):
 pixels.fill(rgb(ctx.value,17,29))
'''

def run(args):
    c=Client(SerialTransport(args.port,no_reset=True) if args.port else ProcessTransport(args.process),timeout=30)
    board_monitor.attach(c,args)
    result={'firmware_sha256':hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
            'fairness':[],'preparation':[],'cancellations':[],'heavy_source':HEAVY}
    created=[];created_presets=[];created_transition=False
    def save():args.result.write_text(json.dumps(result,indent=2)+'\n')
    def configure(count=200,two=False):
        c.configure(EMPTY)
        outputs=[{'id':'check','type':'ws2812b','config':{'pin':args.pin,'count':count,'brightness_limit':32}}]
        if two:outputs.append({'id':'other','type':'ws2812b','config':{'pin':args.second_pin,'count':count,'brightness_limit':32}})
        c.configure({'schema':'lightvm.config','outputs':outputs})
    def layers(names):return [{'id':f'layer{i}','program':'sch_'+n} for i,n in enumerate(names)]
    def raw_heavy():
        started=time.monotonic()
        c.require_ok(c.command(MANAGE,json.dumps({'op':'program_activate','name':'sch_heavy','output':'check','action':'play'}).encode()))
        accepted=time.monotonic()
        operation=json.loads(c.data_reply)['operation']
        return operation,started,(accepted-started)*1000
    try:
        original=c.get_config();result['before']=c.describe()
        if result['before']['board']!=args.board:raise ValueError('unexpected board')
        names=['map','scalar','sprites','heavy']
        if {'sch_'+n for n in names}.intersection(p['name'] for p in c.programs()):raise ValueError('test names already exist')
        manifest=json.loads((args.packages/'manifest.json').read_text())
        heavy=compile_source(HEAVY)
        if not heavy.success:raise ValueError(heavy.diagnostics)
        result['package_sha256']={}
        for name in names:
            data=heavy.bytecode if name=='heavy' else (args.packages/(name+'.lbc')).read_bytes()
            digest=hashlib.sha256(data).hexdigest()
            if name!='heavy' and digest!=manifest[name]['bytecode_sha256']:raise ValueError('package mismatch')
            result['package_sha256'][name]=digest;c.save_program('sch_'+name,data);created.append('sch_'+name)
        for label,names in [('two-maps',['map']*2),('two-images',['sprites']*2),
                            ('four-scalars',['scalar']*4),('asymmetric',['map','scalar']),
                            ('eight-layers',['map','scalar']*4)]:
            configure()
            try:c.configure_composition(layers(names),output='check')
            except ProtocolError as error:
                if label!='eight-layers' or isinstance(error,board_monitor.BoardMonitorError) or not any(word in str(error).lower() for word in ('memory','heap','allocation','admission')):raise
                result['fairness'].append({'case':label,'admitted':False,'error':str(error)})
                save();continue
            board_monitor.sleep(c,2)
            row=sample(c,'check',10);row['case']=label
            if any(v['updates']<=0 or v['after']['distinct_presentations']<=v['before']['distinct_presentations'] for v in row['layers'].values()):raise RuntimeError('layer failed to make visible progress: '+label)
            result['fairness'].append(row);save()
        configure(two=True)
        for output in ('check','other'):c.configure_composition(layers(['map','scalar']),output=output)
        board_monitor.sleep(c,2)
        for output in ('check','other'):
            row=sample(c,output,10);row['case']='two-outputs/'+output
            if any(v['updates']<=0 for v in row['layers'].values()):raise RuntimeError('output stalled')
            result['fairness'].append(row);save()
        configure()
        for slot in ('A','B'):c.configure_composition(layers(['map','scalar']),output='check',slot=slot)
        c.select_transition_effect('crossfade',output='check');c.set_progress(50,output='check');board_monitor.sleep(c,2)
        row=sample(c,'check',10);row['case']='two-layers-per-ab-slot'
        if len(row['layers'])!=4 or any(v['updates']<=0 for v in row['layers'].values()):raise RuntimeError('A/B slot stalled')
        result['fairness'].append(row);save()
        # Manual A/B deliberately keeps both slots available. Retirement is a
        # recall-owned fade property, so exercise an actual saved recall here.
        configure()
        fade=compile_source((Path(__file__).resolve().parents[1]/'examples/transitions/crossfade.py').read_text())
        if not fade.success:raise ValueError(fade.diagnostics)
        c.save_program('sch_fade',fade.bytecode);created_transition=True
        for name in ('map','sprites'):
            data=(args.packages/(name+'.lbc')).read_bytes()
            c.save_preset('sch_'+name,[{'id':'main','program':'sch_'+name,'crc32':zlib.crc32(data)}],
                          output='check',entry_transition={'program':'sch_fade','crc32':zlib.crc32(fade.bytecode),'duration_ms':100})
            created_presets.append('sch_'+name)
        c.recall_preset('sch_map',output='check');board_monitor.sleep(c,.3)
        c.recall_preset('sch_sprites',output='check');board_monitor.sleep(c,1)
        before=c.transition(output='check');board_monitor.sleep(c,1);after=c.transition(output='check')
        result['completed_fade']={'before':before,'after':after}
        outgoing={x['id']:x['frames'] for x in before['A'].get('layers',[])}
        if any(x['frames']!=outgoing.get(x['id']) for x in after['A'].get('layers',[])):raise RuntimeError('hidden outgoing layers kept rendering')
        configure()
        for trial in range(5):
            c.activate('sch_map',output='check');board_monitor.sleep(c,.2);old=c.program_state(output='check')
            op,started,acceptance=raw_heavy();old_progress=False
            while not op['terminal']:
                status=c.status_fields(output='check')
                old_progress|=int(status['generation'])==old['generation'] and int(status['frames'])>old['render_count']
                op=c.operation(op['boot_id'],op['id'])
            if op['code']:raise RuntimeError(op)
            result['preparation'].append({'trial':trial+1,'acceptance_ms':acceptance,'completion_ms':(time.monotonic()-started)*1000,'old_progress_observed':old_progress,'operation':op})
            save()
            if not old_progress:raise RuntimeError('old playback did not visibly progress during preparation')
        for trial in range(10):
            c.activate('sch_map',output='check');old=c.program_state(output='check');heap=c.status_fields(output='check')['heap']
            op,started,acceptance=raw_heavy()
            while not op['terminal'] and op['stage'] not in ('initialise','first_render'):op=c.operation(op['boot_id'],op['id'])
            if op['terminal']:raise RuntimeError('heavy candidate completed before cancellation')
            op=c.operation(op['boot_id'],op['id'],cancel=True)
            while not op['terminal']:op=c.operation(op['boot_id'],op['id'])
            if op['stage']!='cancelled' or c.program_state(output='check')['generation']!=old['generation']:raise RuntimeError('cancelled candidate changed live generation')
            if c.status_fields(output='check')['heap']!=heap:raise RuntimeError('cancelled candidate leaked VM heap')
            result['cancellations'].append({'trial':trial+1,'operation':op,'heap':heap});save()
        configure(1024);c.configure_composition(layers(['scalar']*4),output='check')
        result['before_empty_replacement']=c.describe();start=time.monotonic();c.configure(EMPTY)
        result['empty_replacement_ms']=(time.monotonic()-start)*1000
        result['after']=c.describe()
        if result['after']['boot_id']!=result['before']['boot_id']:raise RuntimeError('controller rebooted')
        result['passed']=True
    except BaseException as error:result['passed']=False;result['error']=str(error);raise
    finally:
        try:
            c.configure(EMPTY)
            if 'original' in locals():c.configure(original)
            for name in created_presets:c.delete_preset(name)
            for name in created:c.delete_program(name)
            if created_transition:c.delete_program('sch_fade',program_type='transition')
            result['cleanup_complete']=True
        except BaseException as error:result['cleanup_complete']=False;result['cleanup_error']=str(error);raise
        finally:
            try:c.close()
            finally:save()
    print('Layer progress, A/B retirement, preparation, cancellation and empty replacement passed.',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    group=parser.add_mutually_exclusive_group(required=True);group.add_argument('--port');group.add_argument('--process',type=Path)
    parser.add_argument('--board',required=True,choices=DEFAULT_PINS)
    parser.add_argument('--pin',required=True);parser.add_argument('--second-pin',required=True)
    parser.add_argument('--firmware',required=True,type=Path);parser.add_argument('--packages',required=True,type=Path);parser.add_argument('--result',required=True,type=Path)
    board_monitor.add_arguments(parser);args=parser.parse_args()
    if args.result.exists():parser.error('result exists; retain prior attempts')
    run(args)
