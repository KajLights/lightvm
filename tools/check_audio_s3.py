#!/usr/bin/env python3
"""Compare local PCM with the physical S3 diagnostic firmware and native core.

Build tests/audio_s3_probe with ESP-IDF 5.5.3 and the pinned KAJ_AUDIO_CORE.
Flash only its app at 0x10000, run this check, then restore the Sense application.
This tool streams fixtures over USB only. It never plays or records sound.
"""
import argparse
import ctypes as C
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from audio_dependency import verify

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'shared/device-core/python'))
from device_client import SerialTransport
HEADER=struct.Struct('<4sIII')
DEFAULTS=dict(gain_db=0,floor_db=-50,attack_ms=30,release_ms=220,onset_threshold=.08,onset_refractory_ms=200)

class Probe:
    def __init__(self,port):
        self.transport=SerialTransport(port,no_reset=True)
        self.serial=self.transport.serial;self.serial.timeout=5;self.serial.write_timeout=5
    def read(self,size):
        data=self.serial.read(size)
        if len(data)!=size:raise RuntimeError(f'S3 USB response truncated: {len(data)}/{size}')
        return data
    def call(self,command,payload=b''):
        self.serial.write(HEADER.pack(b'KAP1',command,len(payload),zlib.crc32(payload))+payload)
        magic,status,size,crc=HEADER.unpack(self.read(HEADER.size))
        assert magic==b'KAP1' and size<=1024,(magic,status,size)
        data=self.read(size)
        assert zlib.crc32(data)==crc,'S3 response CRC mismatch'
        assert not status,('S3 command rejected',command,status)
        return data
    def start(self,profile,bands,config):
        data=struct.pack('<II5fI',profile,bands,config['gain_db'],config['floor_db'],config['attack_ms'],config['release_ms'],config['onset_threshold'],config['onset_refractory_ms'])
        return struct.unpack('<'+'H'*(bands+1),self.call(1,data))
    def push(self,pcm,end):return struct.unpack('<74f',self.call(2,struct.pack('<Q',end)+pcm))
    def info(self):return json.loads(self.call(3))

class Native:
    def __init__(self,library,profile,bands,config):
        self.lib=library
        self.handle=library.kf_host_new(profile,config['gain_db'],config['floor_db'],config['attack_ms'],config['release_ms'],bands,config['onset_threshold'],config['onset_refractory_ms'])
        assert self.handle,'native initialization failed'
        self.pcm=library.kf_host_pcm(self.handle);self.out=library.kf_host_output(self.handle)
    def push(self,pcm,end):
        C.memmove(self.pcm,pcm,len(pcm));ready=self.lib.kf_host_push(self.handle,end)
        return (ready,*self.out[:73])
    def close(self):self.lib.kf_host_destroy(self.handle)

def native_library(core,work):
    path=work/'analysis.so'
    subprocess.run([os.environ.get('CC','cc'),'-O2','-std=c11','-shared','-fPIC','-ffp-contract=off','-I'+str(core),
                    *[str(core/name) for name in ('features.c','onset.c','fft_portable.c','host.c')],'-lm','-o',str(path)],check=True)
    lib=C.CDLL(str(path));lib.kf_host_new.restype=C.c_void_p
    lib.kf_host_new.argtypes=[C.c_uint,C.c_float,C.c_float,C.c_float,C.c_float,C.c_uint,C.c_float,C.c_uint]
    for name in ('kf_host_destroy','kf_host_reset'):
        getattr(lib,name).argtypes=[C.c_void_p];getattr(lib,name).restype=None
    for name,kind in (('pcm',C.c_int16),('output',C.c_float),('edges',C.c_uint16)):
        fn=getattr(lib,'kf_host_'+name);fn.argtypes=[C.c_void_p];fn.restype=C.POINTER(kind)
    lib.kf_host_push.argtypes=[C.c_void_p,C.c_double];lib.kf_host_push.restype=C.c_int
    return lib

def production_code(application,probe):
    from elftools.elf.elffile import ELFFile
    def sections(build,name):
        path=next((build/'esp-idf/main/CMakeFiles/__idf_main.dir').rglob(name))
        with path.open('rb') as source:
            return {section.name:section.data() for section in ELFFile(source).iter_sections()
                    if section.name.startswith('.text') and section.data()}
    result={}
    for name in ('features.c.obj','onset.c.obj','fft.c.obj'):
        expected,actual=sections(application,name),sections(probe,name)
        assert expected==actual,('probe machine code differs from production application',name)
        result[name]=hashlib.sha256(b''.join(key.encode()+b'\0'+expected[key] for key in sorted(expected))).hexdigest()
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',type=Path,required=True);parser.add_argument('--port',required=True)
    parser.add_argument('--bands',default='16');parser.add_argument('--profiles',default='1,2')
    parser.add_argument('--firmware',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--application-build',type=Path,required=True)
    args=parser.parse_args();core=args.core.resolve();revision=verify(core)
    pcm=gzip.decompress((core.parent/'tests/fixtures/conformance.pcm.gz').read_bytes())
    manifest=json.loads((core.parent/'tests/fixtures/conformance.json').read_text())
    assert hashlib.sha256(pcm).hexdigest()==manifest['sha256']
    report={'physical_s3':True,'core_revision':revision,'fixture_sha256':manifest['sha256'],
            'firmware_sha256':hashlib.sha256(args.firmware.read_bytes()).hexdigest(),'speaker_playback':False,
            'microphone_capture':False,'parameters':DEFAULTS,'cases':[],'result':'incomplete',
            'production_matching_code_sha256':production_code(args.application_build,args.firmware.parent),
            'application_sha256':hashlib.sha256((args.application_build/'kaj_fi.bin').read_bytes()).hexdigest()}
    started=time.monotonic();probe=Probe(args.port)
    try:
        report['device']=probe.info()
        with tempfile.TemporaryDirectory(prefix='audio-s3-native-') as directory:
            library=native_library(core,Path(directory))
            for bands in map(int,args.bands.split(',')):
                for profile in map(int,args.profiles.split(',')):
                    native=Native(library,profile,bands,DEFAULTS)
                    try:
                        edges=probe.start(profile,bands,DEFAULTS)
                        assert edges==tuple(library.kf_host_edges(native.handle)[:bands+1]),'band edges differ'
                        maxima=[0.0]*74;expected=[];observed=[]
                        for hop,offset in enumerate(range(0,len(pcm),2048)):
                            block=pcm[offset:offset+2048];end=(hop+1)*1024*1000000//48000
                            reference=native.push(block,end);actual=probe.push(block,end)
                            assert actual[0]==reference[0],('readiness differs',profile,bands,hop)
                            if actual[0]!=1:continue
                            if reference[73]>0:expected.append(hop)
                            if actual[73]>0:observed.append(hop)
                            for index,(a,b) in enumerate(zip(reference,actual)):
                                assert math.isfinite(a) and math.isfinite(b),(profile,bands,hop,index)
                                maxima[index]=max(maxima[index],abs(a-b))
                        metrics=[maxima[i] for i in range(1,71) if i!=5]
                        case={'profile':profile,'bands':bands,'hops':len(pcm)//2048,'band_edges':edges,
                              'native_event_hops':expected,'s3_event_hops':observed,
                              'errors':{'metrics_max':max(metrics),'centroid_max':maxima[5],'novelty_max':maxima[71],
                                        'threshold_max':maxima[72],'strength_max':maxima[73]},'device':probe.info()}
                        report['cases'].append(case)
                        assert max(metrics)<=1e-4 and maxima[5]<=1e-3,('metric parity failed',case)
                        assert expected==observed,('occurrence parity failed',case)
                        assert case['device']['max_dsp_us']<21333,('analysis exceeds hop budget',case)
                        # A capture discontinuity is invalid, then warms up again.
                        zero=bytes(2048);end+=100000
                        assert native.push(zero,end)[0]==probe.push(zero,end)[0]==-1
                        for advance in (21333,42666,64000):
                            reference=native.push(zero,end+advance);actual=probe.push(zero,end+advance)
                            assert reference[0]==actual[0] and actual[73]==0,'gap replayed an event'
                        probe.call(4);library.kf_host_reset(native.handle)
                        for hop in range(30):
                            reference=native.push(zero,(hop+1)*1024*1000000//48000);actual=probe.push(zero,(hop+1)*1024*1000000//48000)
                            assert reference[0]==actual[0] and actual[73]==0,'reset/silence emitted an event'
                        case['gap_reset_silence']='passed'
                    finally:native.close()
        report['result']='passed'
    finally:
        probe.serial.close();report['elapsed_seconds']=time.monotonic()-started
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
