#!/usr/bin/env python3
"""Recover hash-verified public benchmark packages without rebuilding bytecode."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]
RECORD = ROOT / 'docs/benchmarks/c3-performance-2026-10-24.json'
PREFIX = 'c3-performance-2026-10-24/'


def prepare(destination, suite='animation'):
    record = json.loads(RECORD.read_text())
    archive = RECORD.parent / record['evidence_archive']['file']
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != record['evidence_archive']['sha256']:
        raise ValueError('evidence archive hash mismatch')
    folder = 'campaign-packages' if suite == 'animation' else 'matrix'
    relative = 'pico-evidence/' + folder + '/'
    files = {}
    with tarfile.open(archive) as bundle:
        inventory = json.load(bundle.extractfile(PREFIX + 'MANIFEST.json'))
        for member in bundle.getmembers():
            if not member.name.startswith(PREFIX + relative):
                continue
            name = member.name.removeprefix(PREFIX + relative)
            if not name or '/' in name or name in ('.', '..') or not member.isfile():
                raise ValueError('unexpected package member: ' + member.name)
            data = bundle.extractfile(member).read()
            expected = inventory[relative + name]
            if len(data) != expected['bytes'] or hashlib.sha256(data).hexdigest() != expected['sha256']:
                raise ValueError('evidence member hash mismatch: ' + name)
            files[name] = data
    manifest = json.loads(files['manifest.json'])
    for name, row in manifest.items():
        if hashlib.sha256(files[name + '.lbc']).hexdigest() != row['bytecode_sha256']:
            raise ValueError('bytecode hash mismatch: ' + name)
    destination.mkdir(parents=True, exist_ok=False)
    for name, data in files.items():
        (destination / name).write_bytes(data)
    (destination / 'provenance.json').write_text(json.dumps({
        'suite': suite, 'record': str(RECORD.relative_to(ROOT)),
        'archive_sha256': digest, 'member_directory': relative,
    }, indent=2) + '\n')
    return len(manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='new package directory')
    parser.add_argument('--suite', choices=('animation', 'numeric'), default='animation')
    args = parser.parse_args()
    count = prepare(args.output, args.suite)
    print(f'Verified {count} frozen packages in {args.output}')


if __name__ == '__main__':
    main()
