#!/usr/bin/env python3
"""Fetch the exact portable audio dependency, without touching other checkouts."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
revision=(ROOT/'kaj-audio.rev').read_text().strip()
assert len(revision)==40 and all(c in '0123456789abcdef' for c in revision)
destination=ROOT/'.deps/kaj-fi'
if not destination.exists():
    destination.parent.mkdir(exist_ok=True)
    subprocess.run(['git','clone','https://github.com/KajLights/kaj-fi.git',str(destination)],check=True)
if subprocess.check_output(['git','-C',str(destination),'status','--porcelain']).strip():
    raise SystemExit('Audio dependency checkout has local edits; preserve them before updating it.')
subprocess.run(['git','-C',str(destination),'fetch','origin',revision],check=True)
subprocess.run(['git','-C',str(destination),'checkout','--detach',revision],check=True)
print(destination/'core')
