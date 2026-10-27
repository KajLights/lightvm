#!/usr/bin/env python3
"""Build/copy the pinned shared analyser and capture adapter for the editor."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core',type=Path,default=Path(os.environ.get('KAJ_AUDIO_CORE',ROOT/'.deps/kaj-fi/core')))
parser.add_argument('--emcc',default=os.environ.get('EMCC','emcc'))
args=parser.parse_args()
revision=(ROOT/'kaj-audio.rev').read_text().strip()
head=subprocess.check_output(['git','-C',str(args.core),'rev-parse','HEAD'],text=True).strip()
if head!=revision:raise SystemExit('Audio core differs from kaj-audio.rev')
if subprocess.check_output(['git','-C',str(args.core.parent),'status','--porcelain','--','core','tools/build_wasm.py']).strip():
    raise SystemExit('Audio core/build inputs have uncommitted changes; commit and pin them before building.')
output=ROOT/'build/audio'
subprocess.run([sys.executable,str(args.core.parent/'tools/build_wasm.py'),'--output',str(output),'--emcc',args.emcc],check=True)
for name in ('pcm-adapter.mjs','audio-worklet.mjs'):shutil.copyfile(ROOT/'web/audio'/name,output/name)
(output/'revision.txt').write_text(revision+'\n')
print(output)
