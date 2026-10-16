#!/usr/bin/env python3
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
BUILD=ROOT/'build/portable-bytecode'
def main():
    p=argparse.ArgumentParser();p.add_argument('--device',action='store_true');p.add_argument('--sanitize',action='store_true');p.add_argument('--idf',type=Path,default=Path('/tmp/lightvm-esp-deps/esp-idf'));p.add_argument('--idf-tools',type=Path,default=Path('/tmp/lightvm-esp-deps/tools'));p.add_argument('--cmake-bin',type=Path)
    p.add_argument('--pico',action='store_true');p.add_argument('--pico-sdk',type=Path);p.add_argument('--arm-toolchain',type=Path)
    p.add_argument('--pico-config',type=Path,help='existing lightvm.config/1 JSON; use its first configured strip')
    a=p.parse_args();BUILD.mkdir(parents=True,exist_ok=True)
    flags=['-O3','-Wall','-Wextra','-Werror','-ffp-contract=off']
    if a.sanitize:flags+=['-fsanitize=address,undefined','-g']
    subprocess.run(['cc',*flags,str(HERE/'vm.c'),str(HERE/'host.c'),'-o',str(BUILD/('host-sanitized' if a.sanitize else 'host'))],check=True)
    if a.pico:
        env=os.environ.copy()
        if a.pico_sdk:env['PICO_SDK_PATH']=str(a.pico_sdk.resolve())
        if not env.get('PICO_SDK_PATH'):p.error('--pico requires --pico-sdk or PICO_SDK_PATH')
        if a.arm_toolchain:env['PICO_TOOLCHAIN_PATH']=str(a.arm_toolchain.resolve())
        if a.cmake_bin:env['PATH']=str(a.cmake_bin.resolve())+os.pathsep+env['PATH']
        cmd=['cmake','-S',str(HERE/'pico'),'-B',str(BUILD/'pico-firmware'),'-DPICO_BOARD=pico_w','-DCMAKE_BUILD_TYPE=Release']
        if a.pico_config:
            config=json.loads(a.pico_config.read_text())
            if not config.get('devices'):p.error('Pico PoC requires a configured device')
            device=config['devices'][0]
            if device['type']!='ws2812b':p.error('Pico PoC requires a ws2812b first device')
            strip=device['config'];pin=strip['pin']
            if not pin.startswith('GP') or not pin[2:].isdigit():p.error('Pico pin must be GP<number>')
            count=strip['count'];brightness=strip.get('brightness_limit',255);order=strip.get('order','GRB')
            if type(count)!=int or not 1<=count<=1024 or type(brightness)!=int or not 0<=brightness<=255 or sorted(order)!=['B','G','R']:p.error('invalid strip configuration')
            cmd += [f'-DPOC_PIXELS={count}',f'-DPOC_GPIO={int(pin[2:])}',f'-DPOC_BRIGHTNESS={brightness}',f'-DPOC_ORDER={order}']
        with (BUILD/'pico-build.log').open('w') as f:
            result=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
            if not result.returncode:result=subprocess.run(['cmake','--build',str(BUILD/'pico-firmware'),'-j4'],env=env,stdout=f,stderr=subprocess.STDOUT)
        if result.returncode:print((BUILD/'pico-build.log').read_text()[-12000:]);sys.exit(result.returncode)
        print('Firmware built:',BUILD/'pico-firmware/bytecode_poc.uf2')
    if a.device:
        env=os.environ.copy();env['IDF_TOOLS_PATH']=str(a.idf_tools);env['IDF_COMPONENT_MANAGER']='0'
        if a.cmake_bin:env['PATH']=str(a.cmake_bin.resolve())+os.pathsep+env['PATH']
        raw=subprocess.check_output(['bash','-c','. '+shlex.quote(str(a.idf/'export.sh'))+' >/dev/null 2>&1 && env -0'],env=env)
        env=dict(item.decode().split('=',1) for item in raw.split(b'\0') if b'=' in item)
        cmd=[env['IDF_PYTHON_ENV_PATH']+'/bin/python',env['IDF_PATH']+'/tools/idf.py','-C',str(HERE/'device'),'-B',str(BUILD/'firmware'),'build']
        with (BUILD/'build.log').open('w') as f:result=subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT)
        if result.returncode:print((BUILD/'build.log').read_text()[-12000:]);sys.exit(result.returncode)
        print('Firmware built:',BUILD/'firmware/bytecode_poc.bin')
if __name__=='__main__':main()
