#!/usr/bin/env python3
"""Produce verified firmware bundles and a GitHub release discovery manifest."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from release_contract import assemble, package, read_json


def product_version(root, config):
    path = root / config['version_file']
    if path.suffix == '.json':
        return read_json(path)['version']
    matches = re.findall(r'set\(PROJECT_VER "([^"]+)"\)', path.read_text())
    if len(matches) != 1:
        raise ValueError('Expected one authoritative PROJECT_VER')
    return matches[0]


def revision_of(root):
    return subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()


def prepare(root, config, tag):
    revision = revision_of(root)
    version = product_version(root, config)
    if tag:
        if tag != 'v' + version:
            raise ValueError('Tag must exactly match the authoritative product version')
        resolved = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--verify', 'refs/tags/' + tag + '^{commit}'], text=True).strip()
        if resolved != revision:
            raise ValueError('Release tag does not resolve to the checked-out source')
        if subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain']):
            raise ValueError('Release checkout is dirty')
        subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', revision, 'origin/main'], check=True)
        if os.environ.get('GITHUB_ACTIONS') == 'true':
            runs = json.loads(subprocess.check_output(['gh', 'api',
                'repos/' + config['repository'] + '/actions/workflows/build.yml/runs?head_sha=' + revision + '&per_page=100']))['workflow_runs']
            if not any(r['head_sha'] == revision and r['conclusion'] == 'success' and r['event'] in ('push', 'workflow_dispatch') for r in runs):
                raise ValueError('The exact release source must already have a successful build.yml run')
    matrix = {'include': config['targets']}
    return {'revision': revision, 'version': version, 'tag': tag or '',
            'pico': {'include': [t for t in matrix['include'] if t['platform'] == 'pico']},
            'esp32': {'include': [t for t in matrix['include'] if t['platform'] == 'esp32']}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('.'))
    commands = parser.add_subparsers(dest='command', required=True)
    init = commands.add_parser('prepare')
    init.add_argument('--tag')
    init.add_argument('--github-output', type=Path)
    pack = commands.add_parser('package')
    pack.add_argument('--target', required=True)
    pack.add_argument('--build', type=Path, required=True)
    pack.add_argument('--output', type=Path, required=True)
    collect = commands.add_parser('assemble')
    collect.add_argument('--bundles', type=Path, required=True)
    collect.add_argument('--output', type=Path, required=True)
    collect.add_argument('--tag')
    collect.add_argument('--public', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    config = read_json(root / 'release/targets.json')
    if args.command == 'prepare':
        result = prepare(root, config, args.tag)
        if args.github_output:
            with args.github_output.open('a') as output:
                for key, value in result.items():
                    output.write(key + '=' + (json.dumps(value, separators=(',', ':')) if isinstance(value, dict) else value) + '\n')
    elif args.command == 'package':
        matches = [t for t in config['targets'] if t['id'] == args.target]
        if len(matches) != 1:
            raise ValueError('Target is not in the approved release matrix')
        result = package(root, config, matches[0], args.build, args.output)
        if result['build']['version'] != product_version(root, config):
            raise ValueError('Linked version differs from the authoritative version')
        if result['build']['firmware']['source_revision'] != revision_of(root) or result['build']['firmware']['device_core_revision'] != revision_of(Path(__file__).resolve().parents[1]):
            raise ValueError('Linked product/core revision differs from the packaging checkout')
    else:
        bundles = sorted(p.parent for p in args.bundles.rglob('artifact.json'))
        result = assemble(config, bundles, args.output, args.tag, args.public)
        if result['source_revision'] != revision_of(root) or result['version'] != product_version(root, config):
            raise ValueError('Release bundle source differs from the selected checkout')
    print(json.dumps({'command': args.command, 'result': result.get('source_revision', result.get('revision', result.get('id')))}))


if __name__ == '__main__':
    main()
