#!/usr/bin/env python3
"""Reproducible device correctness, persistence, replacement and timing exercise."""
import argparse
import json
from pathlib import Path
import struct
import time
import zlib
from device_client import Client
HERE=Path(__file__).resolve().parent;BUILD=HERE.parents[1]/'build/portable-bytecode'
def main():
    p=argparse.ArgumentParser();p.add_argument('--port',default='/dev/cu.usbmodem1101');p.add_argument('--dtr',action='store_true');p.add_argument('--output',type=Path,default=BUILD/'c3-results.json');a=p.parse_args()
    c=Client(a.port,a.dtr);r={}
    try:
        r['boot']=c.command('hello');r['uploads']=[]
        for name in ('color','pattern','chase','wipe','twinkle','rainbow'):
            event=c.upload(BUILD/f'{name}.lbc');r['uploads'].append({'effect':name,**event});time.sleep(.4)
        c.stop();r['benchmarks']=[c.benchmark(1000) for _ in range(3)]
        assert all(x['mismatches']==0 for x in r['benchmarks'])
        r['bytecode']=c.measure(5)
        c.command('mode native','mode');r['native']=c.measure(5);c.command('mode bytecode','mode')
        # Replacement while rendering and output are active.
        c.command('measure','measuring');c.command('play','playing');time.sleep(.2)
        revision=c.command('hello')['revision'];r['corrupt']=c.upload(BUILD/'rainbow.lbc',corrupt=True)
        assert c.command('hello')['revision']==revision
        # A package with valid transport CRC but invalid instruction must be rejected.
        raw=bytearray((BUILD/'rainbow.lbc').read_bytes());regs=struct.unpack_from('<I',raw,8)[0];raw[64+regs*8]=255
        invalid=BUILD/'invalid.lbc';invalid.write_bytes(raw)
        try:c.upload(invalid)
        except RuntimeError as exc:r['invalid']=str(exc)
        else:raise AssertionError('invalid code accepted')
        assert c.command('hello')['revision']==revision
        r['live_replacement']=c.upload(BUILD/'pattern.lbc');time.sleep(.5)
        r['rainbow_restore']=c.upload(BUILD/'rainbow.lbc');time.sleep(.5)
        r['replacement_stats']=c.command('stats')
        revision=c.command('hello')['revision']
        # Interrupt a persistent installation after an actual flash write.
        raw=(BUILD/'pattern.lbc').read_bytes();c.command(f'begin {len(raw)} {zlib.crc32(raw)}','erasing');c.read({'ready'})
        c.command('data 0 '+raw[:128].hex(),'ack');c.command('restart','restarting');c.close();time.sleep(2)
        c=Client(a.port,a.dtr);r['interrupted_upload_reboot']=c.command('hello');assert r['interrupted_upload_reboot']['revision']==revision
        c.command('restart','restarting');c.close();time.sleep(2);c=Client(a.port,a.dtr)
        r['persistent_reboot']=c.command('hello');assert r['persistent_reboot']['revision']==revision
        r['final_stats']=c.command('stats');assert r['final_stats']['errors']==0
        c.command('play','playing')
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(r,indent=2)+'\n')
        print(json.dumps({'benchmarks':r['benchmarks'],'bytecode':r['bytecode']['stats'],'native':r['native']['stats'],'replacement':r['replacement_stats'],'reboot':r['persistent_reboot']},indent=2))
    finally:c.close()
if __name__=='__main__':main()
