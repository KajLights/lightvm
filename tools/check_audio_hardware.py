#!/usr/bin/env python3
"""Short Sense/Pico subscription check; always restores the Pico light target.

Uses the existing no-flash preset and room microphone input. No speaker playback,
configuration edits, extra presets, raw-audio capture or extended stress loop.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from client import Client,SerialTransport,HttpTransport
from device_client import DeviceClient

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,required=True)
    parser.add_argument('--pico-port',required=True);parser.add_argument('--sense-port',required=True)
    parser.add_argument('--pico-url',required=True);parser.add_argument('--sense-ip',required=True)
    parser.add_argument('--preset',default='audio_spectrum_no_flash')
    parser.add_argument('--restart-sense',action='store_true');parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();started=time.monotonic()
    sys.path.insert(0,str(args.core.resolve().parent/'tools'))
    from subscriptions import Subscriber
    pico=Client(SerialTransport(args.pico_port,no_reset=True))
    sense_transport=SerialTransport(args.sense_port,no_reset=True);sense=DeviceClient(sense_transport)
    original=pico.light_state();config=pico.get_config();programs=pico.programs();presets=pico.presets()
    tuning=sense.document('config_get');subscriber=None;http=None
    report={'physical_sense':True,'physical_pico':True,'speaker_playback':False,'checks':[],
            'before':{'pico':pico.describe(),'sense':sense.document('stats'),'light':original},'result':'incomplete'}
    def audio():
        pico.manage('audio_status');return json.loads(pico.data_reply)['providers']
    def wait(predicate,timeout=10):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            if subscriber:subscriber.poll()
            if predicate():return
            time.sleep(.02)
        raise AssertionError('condition did not settle within '+str(timeout)+' seconds')
    def connected():
        providers=audio()
        return len(providers)==1 and providers[0]['status']=='connected' and providers[0]['packets']>=3
    def heap():return int(pico.status_fields()['heap'])
    try:
        wait(lambda:pico.network_status()['state']=='connected')
        pico.set_power(False);wait(lambda:not audio())
        subscriber=Subscriber(args.sense_ip)
        wait(lambda:subscriber.received>=3)
        pico.set_power(True,preset=args.preset);wait(connected)
        wait(lambda:sense.document('stats')['subscribers']==2)
        provider=audio()[0]
        assert int(provider['source_id'],16)==subscriber.source
        assert int(provider['session'],16)==subscriber.session
        assert not provider['synthetic'] and len(pico.composition(slot='B')['layers'])==3
        assert provider['references']==1 and provider['charged_bytes']<=2048
        report['two_receivers']={'pico':provider,'host_received':subscriber.received,'host_lease':f'{subscriber.lease:016x}',
                                 'sense':sense.document('stats')}
        report['checks'].append('Sense delivers one stream to Pico and host through two active leases; three Pico layers share one provider')
        http=Client(HttpTransport(args.pico_url,timeout=5))
        durations={'usb_ms':[],'http_ms':[]}
        for _ in range(5):
            subscriber.poll()
            for name,client in (('usb_ms',pico),('http_ms',http)):
                before=time.monotonic();description=client.describe();durations[name].append((time.monotonic()-before)*1000)
                assert description['controller_id']==report['before']['pico']['controller_id']
        http.close();http=None
        report['management_latency']=durations
        report['running']={'description':pico.describe(),'status':pico.status_fields(),'sense':sense.document('stats')}
        off_heaps=[]
        for cycle in range(3):
            pico.set_power(False);wait(lambda:not audio())
            wait(lambda:sense.document('stats')['subscribers']==1)
            off_heaps.append(heap())
            pico.set_power(True,preset=args.preset);wait(connected)
        assert len(set(off_heaps))==1,('retained heap grew',off_heaps)
        report['off_heap_bytes']=off_heaps
        report['checks'].append('Three off/on cycles release the Pico lease and retain a constant charged heap')
        if args.restart_sense:
            previous=audio()[0]['session'];sense_transport.close();sense_transport=None
            log=ROOT/'build/audio-sense-restart.log'
            with log.open('w') as output:
                process=subprocess.Popen([sys.executable,'-m','esptool','--chip','esp32s3','--port',args.sense_port,
                                          '--before','default_reset','--after','hard_reset','chip_id'],stdout=output,stderr=subprocess.STDOUT)
                unavailable=False;deadline=time.monotonic()+20
                try:
                    while time.monotonic()<deadline:
                        subscriber.poll();providers=audio()
                        unavailable|=not providers or providers[0]['status']!='connected'
                        if providers and providers[0]['status']=='connected' and providers[0]['session']!=previous and subscriber.session==int(providers[0]['session'],16):break
                        time.sleep(.03)
                    else:raise AssertionError('receivers did not reacquire restarted Sense')
                finally:
                    process.wait(timeout=5)
            assert process.returncode==0 and unavailable
            sense_transport=SerialTransport(args.sense_port,no_reset=True);sense=DeviceClient(sense_transport)
            report['restart']={'previous_session':previous,'current_provider':audio()[0],'saw_unavailable':unavailable,
                               'host_session':f'{subscriber.session:016x}'}
            report['checks'].append('Both receivers reacquire a new Sense session; Pico becomes unavailable during the capture gap')
        assert sense.document('config_get')==tuning
        assert pico.get_config()==config and pico.programs()==programs and pico.presets()==presets
        report['after']={'pico':pico.describe(),'sense':sense.document('stats')}
        assert report['after']['pico']['main_stack_peak_bytes']<16384-2048
        assert report['after']['pico']['worker_stack_peak_bytes']<8192-1024
        assert report['after']['sense']['overruns']==0
        report['result']='passed'
    finally:
        if http:http.close()
        if subscriber:subscriber.close();subscriber=None
        try:
            pico.set_light(power=original['power'],preset=original['preset'],brightness=original['brightness'],color=original['color'])
            pico.set_power(original['power'])
            restored=pico.light_state()
            for field in ('power','preset','brightness','color'):assert restored[field]==original[field]
            report['restored']={'light':restored,'providers':audio(),'program_count':len(pico.programs()),'preset_count':len(pico.presets())}
            if not original['power']:assert not report['restored']['providers']
            if sense_transport:
                wait(lambda:sense.document('stats')['subscribers']==(1 if original['power'] else 0))
                report['restored']['sense']=sense.document('stats')
        finally:
            pico.close()
            if sense_transport:sense_transport.close()
            report['elapsed_seconds']=time.monotonic()-started
            args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'result':report['result'],'checks':report['checks'],'restored':report['restored'],'elapsed_seconds':report['elapsed_seconds']},indent=2))

if __name__=='__main__':main()
