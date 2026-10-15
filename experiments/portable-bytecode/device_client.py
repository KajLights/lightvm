#!/usr/bin/env python3
"""Upload portable packages and measure the standalone PoC over USB."""
import argparse
import json
from pathlib import Path
import statistics
import time
import zlib
import serial

class Client:
    def __init__(self,port,dtr=False):
        self.serial=serial.Serial(None,115200,timeout=.1,write_timeout=3)
        self.serial.dtr=dtr;self.serial.rts=False;self.serial.port=port;self.serial.open()
        self.serial.reset_input_buffer();self.pending=[]
    def close(self):self.serial.close()
    def send(self,line):self.serial.write((line+'\n').encode());self.serial.flush()
    def read(self,events,timeout=10):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            line=self.serial.readline()
            if not line:continue
            try:r=json.loads(line)
            except (ValueError,UnicodeError):continue
            if r.get('event') in events:return r
            if r.get('event') in ('error','rejected','fault'):raise RuntimeError(r)
        raise TimeoutError(events)
    def command(self,line,event=None):
        self.send(line);return self.read({event or line.split()[0]})
    def upload(self,path,corrupt=False):
        raw=Path(path).read_bytes();crc=zlib.crc32(raw)^(1 if corrupt else 0)
        self.command(f'begin {len(raw)} {crc}','erasing');self.read({'ready'})
        for offset in range(0,len(raw),128):self.command(f'data {offset} {raw[offset:offset+128].hex()}','ack')
        self.send('load')
        if corrupt:return self.read({'rejected'})
        self.read({'validating'});return self.read({'activated'})
    def stop(self):
        self.command('stop','stopped');time.sleep(.03)
    def benchmark(self,frames=1000):
        self.stop();self.command(f'benchmark {frames}','benchmarking');return self.read({'benchmark'},60)
    def measure(self,seconds=5):
        self.command('measure','measuring');self.command('play','playing')
        end=time.monotonic()+seconds;latencies=[]
        while time.monotonic()<end:
            start=time.monotonic();self.command(f'ping {len(latencies)}','pong');latencies.append((time.monotonic()-start)*1e6);time.sleep(.02)
        self.stop();stats=self.command('stats');samples=[self.command(f'sample {n}','sample') for n in range(stats['samples'])]
        for key in ('render_us','gap_us'):
            vals=sorted(s[key] for s in samples if s[key])
            stats[key]={'median':statistics.median(vals),'p95':vals[int(.95*(len(vals)-1))],'max':max(vals)}
        stats['usb_rtt_us']={'median':statistics.median(latencies),'max':max(latencies)}
        return {'stats':stats,'samples':samples}

def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/cu.usbmodem1101');p.add_argument('--dtr',action='store_true',help='assert DTR for Pico USB CDC');p.add_argument('--upload',type=Path);p.add_argument('--benchmark',type=int);p.add_argument('--measure',type=float);p.add_argument('--command');p.add_argument('--output',type=Path);a=p.parse_args()
    client=Client(a.port,a.dtr)
    try:
        result={'device':client.command('hello')}
        if a.upload:result['upload']=client.upload(a.upload)
        if a.benchmark:result['benchmark']=client.benchmark(a.benchmark)
        if a.measure:result['measure']=client.measure(a.measure)
        if a.command:
            client.send(a.command);result['command']=client.read({a.command.split()[0],'playing','stopped','restarting','mode'})
        if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v['stats'] if k=='measure' else v for k,v in result.items()},indent=2))
    finally:client.close()
if __name__=='__main__':main()
