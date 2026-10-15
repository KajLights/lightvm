#!/usr/bin/env python3
"""Measure one installed component-runtime firmware over real USB serial."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import queue
import statistics
import threading
import time
import zlib
import serial

BACKENDS={"wamr":"wamr-aot","native":"direct-native","typed":"typed-interpreter"}
ROOT=Path(__file__).resolve().parents[3]

def oracle(seed,frame):
    x=(seed^(frame*0x9e3779b9))&0xffffffff
    h=2166136261
    for i in range(200):
        x=(x^(x<<13))&0xffffffff;x^=x>>17;x=(x^(x<<5))&0xffffffff
        x=(x+i+0x6d2b79f5)&0xffffffff
        for shift in (0,8,16):h=((h^((x>>shift)&255))*16777619)&0xffffffff
    return h

def distribution(values):
    if not values:return None
    values=sorted(values)
    def p(q):return values[min(len(values)-1,int((len(values)-1)*q))]
    return {"count":len(values),"min":values[0],"median":statistics.median(values),
            "p95":p(.95),"p99":p(.99),"max":values[-1],"mean":statistics.mean(values)}

class Link:
    def __init__(self,port):
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=3)
        self.serial.dtr=False;self.serial.rts=False;self.serial.port=port;self.serial.open()
        self.lock=threading.Lock();self.stop=threading.Event();self.ping_stop=threading.Event()
        self.events=queue.Queue();self.pending={};self.latencies=[];self.invalid=[];self.sequence=0;self.phase="warmup"
        self.reader=threading.Thread(target=self.read,daemon=True);self.reader.start()
    def send(self,line):
        with self.lock:self.serial.write((line+'\n').encode())
    def read(self):
        buffer=bytearray()
        while not self.stop.is_set():
            try:data=self.serial.read(self.serial.in_waiting or 1)
            except serial.SerialException:return
            buffer.extend(data)
            while b'\n' in buffer:
                line,_,rest=buffer.partition(b'\n');buffer=bytearray(rest)
                try:event=json.loads(line)
                except (ValueError,UnicodeError):
                    if line:self.invalid.append(line.decode(errors='replace'))
                    continue
                if event.get('event')=='pong':
                    item=self.pending.pop(event['id'],None)
                    if item:
                        start,phase=item;self.latencies.append({'id':event['id'],'phase':phase,'sent_ns':start,'latency_us':(time.monotonic_ns()-start)/1000})
                else:self.events.put(event)
    def wait(self,name,timeout=8):
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            try:event=self.events.get(timeout=max(.01,deadline-time.monotonic()))
            except queue.Empty:break
            if event.get('event')==name:return event
            if event.get('event') in ('fault','error','rejected'):raise RuntimeError(event)
            raise RuntimeError(f'Expected {name}, received {event}')
        raise TimeoutError(f'No {name}; recent serial: {self.invalid[-60:]}')
    def command(self,line,name):self.send(line);return self.wait(name)
    def between_transfers(self,line,event):
        for _ in range(100):
            try:return self.command(line,event)
            except RuntimeError as error:
                if 'output_busy' not in str(error):raise
                time.sleep(.003)
        raise RuntimeError(f'Could not {line} between transfers')
    def start_pings(self):
        def loop():
            due=time.monotonic()
            while not self.ping_stop.is_set():
                self.sequence+=1;seq=self.sequence;self.pending[seq]=(time.monotonic_ns(),self.phase)
                self.send(f'ping {seq}');due+=.02
                self.ping_stop.wait(max(0,due-time.monotonic()))
        self.pinger=threading.Thread(target=loop,daemon=True);self.pinger.start()
    def stop_pings(self):
        self.ping_stop.set()
        if hasattr(self,'pinger'):self.pinger.join(timeout=4)
        time.sleep(.15)
    def upload(self,path,revision,corrupt=False):
        data=path.read_bytes();crc=zlib.crc32(data)^(1 if corrupt else 0)
        start=time.monotonic_ns()
        self.command(f'begin {len(data)} {crc} {revision}','ack')
        for offset in range(0,len(data),192):
            chunk=data[offset:offset+192]
            result=self.command(f'data {offset} {chunk.hex()}','ack')
            assert result['offset']==offset+len(chunk)
        self.command('load','queued')
        result=self.wait('rejected' if corrupt else 'ready' if not hasattr(self,'loaded') else 'activated')
        if not corrupt:self.loaded=True
        return {'bytes':len(data),'revision':revision,'corrupt_crc':corrupt,
                'host_duration_us':(time.monotonic_ns()-start)/1000,'response':result}
    def close(self):
        self.stop_pings();self.stop.set();self.reader.join(timeout=2);self.serial.close()

def run(port,backend,v1,v2,smoke=False):
    link=Link(port)
    try:
        identity=link.command('hello','hello')
        assert identity['backend']==BACKENDS[backend] and identity['pixels']==200
        initial=link.upload(v1,1)
        link.start_pings();time.sleep(1 if smoke else 3)
        if smoke:
            replacement=link.upload(v2,2);time.sleep(.2);link.stop_pings()
            return {'identity':identity,'initial':initial,'replacement':replacement,'stats':link.between_transfers('stats','stats'),'invalid_serial':link.invalid}
        link.between_transfers('measure','measuring')
        link.phase='baseline';time.sleep(4)
        uploads=[]
        phase1=link.command('phase 1','phase');link.phase='replacement'
        # Rejection must preserve the old effect, then three real code changes.
        uploads.append(link.upload(v2,2,corrupt=True))
        for path,revision in [(v2,2),(v1,1),(v2,2)]:
            uploads.append(link.upload(path,revision));time.sleep(1.5)
        phase2=link.command('phase 2','phase');link.phase='after';time.sleep(2)
        link.stop_pings();stats=link.between_transfers('stats','stats')
        samples=[link.command(f'sample {i}','sample') for i in range(stats['samples'])]
        events=[link.command(f'event {i}','replacement') for i in range(stats['events'])]
        gaps=[link.command(f'gap {i}','gap') for i in range(3)]
        assert not stats['errors'] and not stats['hash_failures'] and not stats['overflow'] and not stats['tx_drops'],stats
        assert samples and all(s['hash']==oracle(s['seed'],s['frame']) for s in samples)
        assert all(s['complete_us']>=s['submit_us'] for s in samples), 'incomplete physical output'
        assert not link.pending, f'{len(link.pending)} USB replies lost'
        assert len(events)==3 and all(e['end_us'] and e['pending'] and e['retired_us']>=e['end_us'] for e in events),events
        submits=[s['submit_us'] for s in samples]
        completes=[s['complete_us'] for s in samples]
        latencies={phase:distribution([v['latency_us'] for v in link.latencies if v['phase']==phase]) for phase in ('baseline','replacement','after')}
        return {'identity':identity,'initial':initial,'uploads':uploads,'stats':stats,'samples':samples,
                'replacement_events':events,'phases':[phase1,phase2],'service_gaps':gaps,'usb_pings':link.latencies,
                'invalid_serial':link.invalid,'summary':{
                    'render_call_elapsed_us':distribution([s['call_elapsed_us'] for s in samples]),
                    'submit_gap_us':distribution([b-a for a,b in zip(submits,submits[1:])]),
                    'completion_gap_us':distribution([b-a for a,b in zip(completes,completes[1:])]),
                    'rmt_transfer_us':distribution([s['complete_us']-s['submit_us'] for s in samples]),
                    'completion_gaps_over_1_5_periods':sum(b-a>25000 for a,b in zip(completes,completes[1:])),
                    'usb_latency_us':latencies}}
    finally:link.close()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port',default='/dev/cu.usbmodem1101')
    p.add_argument('--backend',choices=BACKENDS,required=True);p.add_argument('--v1',type=Path,required=True);p.add_argument('--v2',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--repetitions',type=int,default=3);p.add_argument('--smoke',action='store_true')
    args=p.parse_args();report={'backend':args.backend,'status':'incomplete','scope':'ESP32-C3, GPIO3, 200 physical RMT pixels, USB 50 requests/second, no optical/Wi-Fi verification',
                              'artifacts':{str(v):hashlib.sha256(v.read_bytes()).hexdigest() for v in (args.v1,args.v2)},'runs':[]}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    def save():args.output.write_text(json.dumps(report,indent=2)+'\n')
    save()
    try:
        for i in range(args.repetitions):
            reset=Link(args.port);reset.send('restart');time.sleep(.2);reset.close();time.sleep(1)
            result=run(args.port,args.backend,args.v1,args.v2,args.smoke);report['runs'].append(result);save()
            print(f'{args.backend}: repetition {i+1} passed',flush=True)
        report['status']='passed';save()
    except Exception as error:
        report['status']='failed';report['error']=str(error);save();raise

if __name__=='__main__':main()
