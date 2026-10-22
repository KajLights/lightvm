#!/usr/bin/env python3
"""Replay the frozen corpus on the isolated Pico numeric test image."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
from pathlib import Path
import sys
import time
import serial

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_native_math import NAMES,assess


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--firmware',type=Path,required=True)
    parser.add_argument('--result',type=Path,required=True)
    args=parser.parse_args()
    corpus_path=ROOT/'tests/fixtures/native-math-vectors.json'
    corpus=json.loads(corpus_path.read_text());cases=corpus['vectors'];lines=[]
    result={'started_utc':datetime.now(timezone.utc).isoformat(),
            'corpus_sha256':hashlib.sha256(corpus_path.read_bytes()).hexdigest(),
            'firmware_sha256':hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
            'cases':len(cases),'raw_results':lines}
    started=time.monotonic()
    try:
        with serial.Serial(args.port,115200,timeout=10,write_timeout=10) as port:
            port.reset_input_buffer()
            for first in range(0,len(cases),64):
                batch=cases[first:first+64]
                data=''.join(f'{NAMES.index(v["op"][4:])} {v["args"][0]} {v["args"][1] if len(v["args"])>1 else "0"}\n' for v in batch)
                port.write(data.encode('ascii'));port.flush()
                for _ in batch:
                    line=port.readline().decode('ascii').strip()
                    if not line:raise RuntimeError('no numeric probe response at vector '+str(len(lines)))
                    lines.append(line)
                if first%1024==0:print(f'{len(lines)}/{len(cases)} vectors',flush=True)
        result['summary']=assess(corpus,lines);result['passed']=True
    except Exception as error:
        result['passed']=False;result['error']=str(error);raise
    finally:
        result['elapsed_seconds']=time.monotonic()-started
        args.result.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result['summary'],indent=2),flush=True)


if __name__=='__main__':main()
