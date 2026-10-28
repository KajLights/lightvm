#!/usr/bin/env python3
"""Export exact upstream Git blobs; never edit bundled kaj-fi files by hand."""
import argparse
import gzip
import hashlib
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PATHS = ('core/', 'tools/build_wasm.py', 'tests/fixtures/', 'main/fft.c',
         'main/runtime.h', 'components/esp-dsp/')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', type=Path, required=True)
    parser.add_argument('--revision', default=(ROOT/'kaj-audio.rev').read_text().strip())
    args = parser.parse_args()
    if not re.fullmatch('[0-9a-f]{40}', args.revision):
        parser.error('--revision must be a full lowercase Git commit ID')
    def git(*command):
        return subprocess.check_output(['git', '-C', str(args.repository), *command])
    assert git('rev-parse', args.revision+'^{commit}').decode().strip() == args.revision
    names = git('ls-tree', '-r', '--name-only', args.revision).decode().splitlines()
    files = {name: git('show', args.revision+':'+name) for name in names
             if any(name.startswith(prefix) if prefix.endswith('/') else name == prefix for prefix in PATHS)}
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w', format=tarfile.PAX_FORMAT) as archive:
        for name, content in sorted(files.items()):
            info = tarfile.TarInfo(name)
            info.size = len(content);info.mode = 0o644;info.mtime = 0
            archive.addfile(info, io.BytesIO(content))
    compressed = gzip.compress(buffer.getvalue(), mtime=0)
    destination = ROOT/'third_party/kaj-audio'
    destination.mkdir(parents=True, exist_ok=True)
    (destination/'source.tar.gz').write_bytes(compressed)
    (destination/'manifest.json').write_text(json.dumps(dict(
        repository='https://github.com/KajLights/kaj-fi', revision=args.revision,
        archive_sha256=hashlib.sha256(compressed).hexdigest(),
        files={name: hashlib.sha256(content).hexdigest() for name, content in sorted(files.items())}), indent=2)+'\n')
    (ROOT/'kaj-audio.rev').write_text(args.revision+'\n')
    print(f'Exported {len(files)} exact Git blobs, {len(compressed)} bytes, revision {args.revision}')


if __name__ == '__main__':
    main()
