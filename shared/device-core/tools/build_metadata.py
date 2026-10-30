#!/usr/bin/env python3
"""Generate a common firmware descriptor from source bytes and resolved build inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def packed(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=True)


def digest(value):
    return hashlib.sha256(packed(value).encode()).hexdigest()


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], stderr=subprocess.DEVNULL)


def source(root):
    paths = git(root, 'ls-files', '-z', '--cached', '--others', '--exclude-standard').decode().split('\0')
    hashes = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
              for name in paths if name and (root / name).is_file()}
    revision = git(root, 'rev-parse', 'HEAD').decode().strip()
    dirty = bool(git(root, 'status', '--porcelain', '--untracked-files=normal'))
    return {'revision': revision, 'content_hash': digest(hashes), 'dirty': dirty}


def cache_value(value):
    """Hash file inputs without encoding checkout locations or SDK directory lists."""
    parts = re.split(r'(?<!\\);', value)
    entries = []
    for part in parts:
        part = part.replace(r'\;', ';')
        path = Path(part)
        if path.is_absolute():
            if path.is_file():
                entries.append({'content_hash': hashlib.sha256(path.read_bytes()).hexdigest()})
        else:
            entries.append(part)
    return (entries[0] if len(parts) == 1 else entries) if entries else None


def generate(args):
    root, core = args.root.resolve(), args.core.resolve()
    features = sorted(set(filter(None, args.features.split(','))))
    catalogue = json.loads((core / 'contracts/features.json').read_text())
    known = {item['id']: item['requires'] for item in catalogue['features']}
    for feature in features:
        if feature not in known or not set(known[feature]).issubset(features):
            raise ValueError('Unknown feature or missing feature dependency: ' + feature)
    if 'ota' in features:
        raise ValueError('OTA requires an implemented boot/update contract; it is not available yet')
    settings = {}
    if args.cache.exists():
        for line in args.cache.read_text().splitlines():
            match = re.match(r'((?:LIGHTVM_|PICO_|KAJ_|CMAKE_(?:C|CXX)_FLAGS)[^:]*):[^=]+=(.*)', line)
            if match:
                key, value = match.groups()
                normalized = cache_value(value)
                if normalized is not None:
                    settings[key] = normalized
    extra = {}
    for name in ('sdkconfig', 'layout'):
        path = getattr(args, name)
        if path and Path(path).is_file():
            extra[name] = hashlib.sha256(Path(path).read_bytes()).hexdigest()
    sdk = None
    if args.sdk_root:
        if Path(git(args.sdk_root, 'rev-parse', '--show-toplevel').decode().strip()).resolve() != Path(args.sdk_root).resolve():
            raise ValueError('SDK provenance requires its own pinned Git checkout')
        sdk = {'revision': git(args.sdk_root, 'rev-parse', 'HEAD').decode().strip(),
               'changes_hash': hashlib.sha256(git(args.sdk_root, 'diff', 'HEAD', '--binary')).hexdigest()}
    inputs = {'product': args.product, 'version': args.version, 'board': args.board,
              'platform': args.platform, 'features': features, 'settings': settings,
              'source': source(root), 'device_core': source(core), 'compiler': args.compiler,
              'build_type': args.build_type, 'sdk': sdk, 'flash_bytes': args.flash_bytes, **extra}
    feature_hash = 'sha256:' + digest(features)
    layout = {'board': args.board, 'layout': extra.get('layout'),
              'flash_bytes': args.flash_bytes, 'backend': settings.get('LIGHTVM_STORAGE_BACKEND', 'store_extent_backend'),
              'storage': settings.get('LIGHTVM_STORAGE_BYTES', 131072 if args.product == 'kaj-fi' else 524288),
              'boot_journal': settings.get('LIGHTVM_BOOT_SEQUENCE', 'OFF')}
    profile = 'mqtt-audio' if 'mqtt' in features and 'audio_subscriptions' in features else 'mqtt' if 'mqtt' in features else 'audio' if 'audio_subscriptions' in features else 'standard'
    tag = None
    if not inputs['source']['dirty'] and not inputs['device_core']['dirty']:
        try:
            expected = 'v' + args.version
            exact = git(root, 'rev-parse', '--verify', 'refs/tags/' + expected + '^{commit}').decode().strip()
            if exact == inputs['source']['revision']:
                tag = expected
        except subprocess.CalledProcessError:
            pass
    # The embedded release label changes bytes, so it is a resolved build input.
    inputs['release_tag'] = tag
    firmware = {'build_id': 'sha256:' + digest(inputs), 'source_revision': inputs['source']['revision'],
                'source_dirty': inputs['source']['dirty'] or inputs['device_core']['dirty'],
                'release_tag': tag, 'profile': profile, 'features_hash': feature_hash,
                'layout_id': 'local-' + digest(layout)[:24], 'boot_contract': None,
                'device_core_revision': inputs['device_core']['revision']}
    chip = 'RP2350' if args.board == 'pico2_w' else 'RP2040' if args.platform == 'pico' else args.board.replace('xiao_', '').upper().replace('ESP32', 'ESP32-') if args.platform == 'esp32' else 'host'
    hardware = {'target': args.board, 'variant': 'sense_pdm' if 'pdm_microphone' in features else 'synthetic' if 'synthetic_audio' in features else None,
                'chip': chip, 'declared_flash_bytes': args.flash_bytes or None,
                'flash_bytes': None, 'flash_verified': False}
    methods = ['uf2', 'picoboot'] if args.platform == 'pico' and args.board != 'pico2_w' else ['uf2'] if args.platform == 'pico' else ['usb-serial'] if args.platform == 'esp32' else []
    updates = {'supported': False, 'methods': methods}
    result = {'schema': 'kajlights.build', 'product': args.product, 'version': args.version,
              'board': args.board, 'firmware': firmware, 'features': features,
              'hardware': hardware, 'updates': updates, 'inputs': inputs}
    args.output.mkdir(parents=True, exist_ok=True)
    content = '#include "device/description.h"\nconst char device_product_version[] = ' + json.dumps(args.version) + ';\n'
    for name, value in [('firmware', firmware), ('features', features), ('hardware', hardware), ('updates', updates)]:
        content += 'const char device_' + name + '_json[] = ' + json.dumps(packed(value)) + ';\n'
    for name, body in [('build.c', content), ('build.json', json.dumps(result, indent=2) + '\n')]:
        path = args.output / name
        if not path.exists() or path.read_text() != body:
            path.write_text(body)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('root', 'core', 'cache', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    for name in ('product', 'version', 'board', 'platform', 'features', 'compiler'):
        parser.add_argument('--' + name, required=True)
    parser.add_argument('--flash-bytes', type=int, default=0)
    parser.add_argument('--sdkconfig', default='')
    parser.add_argument('--layout', default='')
    parser.add_argument('--build-type', default='')
    parser.add_argument('--sdk-root', default='')
    generate(parser.parse_args())
