#!/usr/bin/env python3
"""Build all matched C3 images using the installed, pinned ESP-IDF toolchain."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
BUILD=ROOT/'build/component-runtime/device'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf',type=Path,default=Path('/tmp/lightvm-esp-deps/esp-idf'))
    p.add_argument('--tools',type=Path,default=Path('/tmp/lightvm-esp-deps/tools'))
    p.add_argument('--backend',action='append',choices=['wamr','native','typed'])
    args=p.parse_args();BUILD.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy();env['IDF_TOOLS_PATH']=str(args.tools);env['IDF_COMPONENT_MANAGER']='0'
    env['PATH']=str(ROOT/'build/component-runtime/deps/wamr-python/cmake/data/bin')+os.pathsep+env['PATH']
    script=f'. {shlex.quote(str(args.idf/"export.sh"))} >/dev/null 2>&1 && env -0'
    raw=subprocess.check_output(['bash','-c',script],env=env)
    env=dict(item.decode().split('=',1) for item in raw.split(b'\0') if b'=' in item)
    python=Path(env['IDF_PYTHON_ENV_PATH'])/'bin/python'
    reports={}
    for name in args.backend or ['wamr','native','typed']:
        subprocess.run([sys.executable,str(HERE/f'build_{name}.py')],cwd=ROOT,check=True)
        dest=BUILD/f'{name}-firmware'
        command=[str(python),str(args.idf/'tools/idf.py'),'-C',str(HERE),'-B',str(dest),'-D',f'CR_BACKEND={name}','build']
        with (BUILD/f'{name}-build.log').open('w') as log:
            subprocess.run(command,cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
        size=json.loads(subprocess.check_output([str(python),str(args.idf/'tools/idf_size.py'),'--format','json',str(dest/'component_device.map')],env=env))
        (dest/'size.json').write_text(json.dumps(size,indent=2)+'\n')
        artifacts={n:{'bytes':(dest/n).stat().st_size,'sha256':hashlib.sha256((dest/n).read_bytes()).hexdigest()}
                   for n in ['component_device.bin','component_device.elf','sdkconfig']}
        reports[name]={'size':size,'artifacts':artifacts,'command':command}
        print(f'{name}: built, static shared instruction/data RAM {size["used_diram"]} bytes',flush=True)
    (BUILD/'builds.json').write_text(json.dumps(reports,indent=2)+'\n')

if __name__=='__main__':main()
