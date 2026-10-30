"""Firmware release packaging and website-facing validation, with no device writes."""
import hashlib
from functools import reduce
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import struct

from jsonschema import Draft202012Validator
from referencing import Registry, Resource
from build_metadata import digest, packed
from firmware_payloads import validate_pico_uf2, PICO_FLASH_START

CONTRACTS = Path(__file__).resolve().parents[1] / 'contracts'
MAX_MANIFEST_BYTES = 1024 * 1024
ASSET_NAME = re.compile(r'[a-zA-Z0-9][a-zA-Z0-9_.-]{0,180}')


def read_json(path):
    data = Path(path).read_bytes()
    if len(data) > MAX_MANIFEST_BYTES:
        raise ValueError('Manifest exceeds 1 MiB')
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('Duplicate JSON member: ' + key)
            result[key] = value
        return result
    def invalid_constant(value):
        raise ValueError('Non-JSON numeric constant: ' + value)
    return json.loads(data, object_pairs_hook=unique, parse_constant=invalid_constant)


def validate(document, kind):
    schema = read_json(CONTRACTS / (kind + '.schema.json'))
    resources = [read_json(CONTRACTS / (name + '.schema.json')) for name in ('build', 'firmware', 'release')]
    registry = Registry().with_resources((item['$id'], Resource.from_contents(item)) for item in resources)
    Draft202012Validator(schema, registry=registry).validate(document)


def safe_file(root, name):
    relative = PurePosixPath(name)
    root = Path(root).resolve()
    path = root.joinpath(*relative.parts)
    if relative.is_absolute() or '..' in relative.parts or str(relative) != name or not path.resolve().is_relative_to(root):
        raise ValueError('Unsafe artifact path: ' + name)
    if not path.is_file() or path.is_symlink():
        raise ValueError('Missing/non-ordinary artifact: ' + name)
    return path


def file_record(path, name=None):
    path = Path(path)
    name = name or path.name
    if not ASSET_NAME.fullmatch(name) or not path.is_file() or not path.stat().st_size:
        raise ValueError('Invalid/empty release asset: ' + name)
    return {'name': name, 'bytes': path.stat().st_size, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def verify_assets(artifact, directory):
    validate(artifact, 'firmware')
    names = set()
    for record in artifact['assets']:
        if record['name'] in names or file_record(safe_file(directory, record['name'])) != record:
            raise ValueError('Duplicate or changed release asset: ' + record['name'])
        names.add(record['name'])
    for region in artifact['installation']['parts']:
        if region['asset'] not in names:
            raise ValueError('Installation references an absent asset')
        record = next(a for a in artifact['assets'] if a['name'] == region['asset'])
        if artifact['installation']['uf2_family'] is None and record['bytes'] > region['max_bytes']:
            raise ValueError('Image exceeds its declared installation region')
    if artifact['capacity']['firmware_bytes'] > artifact['capacity']['firmware_region_bytes']:
        raise ValueError('Firmware exceeds its linked region')
    return artifact


def verify_build(build, raw_image):
    validate(build, 'build')
    features = sorted(set(build['features']))
    catalogue = read_json(CONTRACTS / 'features.json')['features']
    known = {f['id']: f['requires'] for f in catalogue}
    if features != build['features'] or any(f not in known or not set(known[f]).issubset(features) for f in features):
        raise ValueError('Invalid resolved features')
    fw = build['firmware']
    inputs = build['inputs']
    if fw['build_id'] != 'sha256:' + digest(inputs) or fw['features_hash'] != 'sha256:' + digest(features):
        raise ValueError('Build inputs or feature digest do not match the embedded identity')
    for name in ('product', 'version', 'board', 'features'):
        if build[name] != inputs[name]:
            raise ValueError('Conflicting build member: ' + name)
    if fw['source_revision'] != inputs['source']['revision'] or fw['device_core_revision'] != inputs['device_core']['revision']:
        raise ValueError('Conflicting source revision')
    if fw['source_dirty'] != (inputs['source']['dirty'] or inputs['device_core']['dirty']):
        raise ValueError('Conflicting source cleanliness')
    if fw['release_tag'] != inputs.get('release_tag'):
        raise ValueError('Conflicting release-label build input')
    if packed(fw).encode() not in raw_image or packed(features).encode() not in raw_image:
        raise ValueError('Linked image does not contain the declared build/feature descriptors')
    if build['updates']['supported'] or fw['boot_contract'] is not None or 'ota' in features:
        raise ValueError('No validated OTA release contract exists yet')


def partition_table(data):
    """Read ESP-IDF's binary table, checking its MD5 and every region boundary."""
    rows = []
    for at in range(0, len(data), 32):
        entry = data[at:at+32]
        if len(entry) != 32:
            raise ValueError('Truncated ESP partition table')
        if entry[:2] == b'\xeb\xeb':
            if entry[16:] != hashlib.md5(data[:at]).digest():
                raise ValueError('ESP partition table MD5 mismatch')
            return rows
        if entry == b'\xff' * 32:
            break
        magic, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', entry)
        if magic != 0x50aa or not size or offset % 4096 or size % 4096 or flags:
            raise ValueError('Unsupported ESP partition entry')
        if any(offset < row['offset'] + row['bytes'] and row['offset'] < offset + size for row in rows):
            raise ValueError('Overlapping ESP partitions')
        rows.append({'name': label.rstrip(b'\0').decode('ascii'), 'type': kind, 'subtype': subtype,
                     'offset': offset, 'bytes': size})
    raise ValueError('ESP partition table has no verified MD5 record')


def esp_image(data, chip):
    """Validate the ESP image header, segment checksum and appended SHA-256."""
    chips = {'ESP32-C3': 5, 'ESP32-S3': 9, 'ESP32-C6': 13}
    if len(data) < 24 or data[0] != 0xe9 or not 1 <= data[1] <= 16 or struct.unpack_from('<H', data, 12)[0] != chips.get(chip):
        raise ValueError('ESP binary header/chip does not match its target')
    at, checksum = 24, 0xef
    for _ in range(data[1]):
        if at + 8 > len(data):
            raise ValueError('Truncated ESP segment header')
        size = struct.unpack_from('<I', data, at + 4)[0]
        at += 8
        if at + size > len(data):
            raise ValueError('Truncated ESP segment')
        checksum = reduce(int.__xor__, data[at:at+size], checksum)
        at += size
    end = at + 16 - at % 16
    if end > len(data) or data[end-1] != checksum:
        raise ValueError('ESP segment checksum mismatch')
    if data[23] != 1 or len(data) != end + 32 or data[end:] != hashlib.sha256(data[:end]).digest():
        raise ValueError('ESP image digest mismatch or unsupported signed/encrypted image')
    minimum, maximum = struct.unpack_from('<HH', data, 15)
    return {'minimum': max(minimum, data[14] * 100), 'maximum': maximum if maximum not in (0, 65535) else None}


def package(root, config, target, directory, output):
    """Package one approved target; no inferred profiles or arbitrary image lists."""
    root, directory, output = Path(root), Path(directory), Path(output)
    matches = list(directory.rglob('device-build/build.json'))
    if len(matches) != 1:
        raise ValueError('Expected exactly one generated build record')
    build = read_json(matches[0])
    if (build['product'], build['board'], build['firmware']['profile'], build['hardware'].get('variant')) != (
            config['product'], target['board'], target['profile'], target['variant']):
        raise ValueError('Generated device description does not match the approved target')
    if not set(target['required_features']).issubset(build['features']) or set(target['excluded_features']) & set(build['features']):
        raise ValueError('Build does not implement the approved profile')
    if build['inputs'].get('settings', {}).get('LIGHTVM_STORAGE_BACKEND', 'store_extent_backend') != 'store_extent_backend':
        raise ValueError('The registered content contract requires the extent storage backend')
    key = target['id'] + '-' + build['firmware']['build_id'].split(':')[1][:16]
    if output.exists() and any(output.iterdir()):
        raise ValueError('Packaging destination must be empty')
    output.mkdir(parents=True, exist_ok=True)
    assets = []
    def add(path, suffix):
        name = key + '-' + suffix
        shutil.copyfile(path, output / name)
        assets.append(file_record(output / name))
        return name
    flash_bytes = build['hardware']['declared_flash_bytes']
    if flash_bytes != target['flash_bytes']:
        raise ValueError('Flash declaration differs from approved target')
    if target['platform'] == 'esp32':
        args = read_json(directory / 'flasher_args.json')
        chip = args['extra_esptool_args']['chip'].upper().replace('ESP32', 'ESP32-')
        if chip != build['hardware']['chip'] or args['flash_settings']['flash_size'] != str(flash_bytes // 1048576) + 'MB':
            raise ValueError('ESP flasher geometry does not match the descriptor')
        table_info, app_info = args['partition-table'], args['app']
        table = partition_table(safe_file(directory, table_info['file']).read_bytes())
        applications = [row for row in table if row['type'] == 0]
        if len(applications) != 1 or applications[0]['subtype'] != 0 or applications[0]['offset'] != int(app_info['offset'], 0):
            raise ValueError('Only the current single-factory-app local layout is supported')
        app = applications[0]
        if max(row['offset'] + row['bytes'] for row in table) > flash_bytes:
            raise ValueError('ESP partition exceeds declared flash')
        app_bytes = safe_file(directory, app_info['file']).read_bytes()
        revisions = [esp_image(app_bytes, chip), esp_image(safe_file(directory, args['bootloader']['file']).read_bytes(), chip)]
        chip_revision = {'minimum': max(r['minimum'] for r in revisions),
                         'maximum': min((r['maximum'] for r in revisions if r['maximum'] is not None), default=None)}
        verify_build(build, app_bytes)
        expected = {int(args[name]['offset'], 0): args[name]['file'] for name in ('bootloader', 'partition-table', 'app')}
        actual = {int(offset, 0): name for offset, name in args['flash_files'].items()}
        if expected != actual or len(actual) != 3:
            raise ValueError('Incomplete or unexpected ESP local-install parts')
        parts = []
        previous_end = 0
        table_offset = int(table_info['offset'], 0)
        for offset, name in sorted(actual.items()):
            source = safe_file(directory, name)
            limit = table_offset - offset if offset < table_offset else min(row['offset'] for row in table) - offset if offset == table_offset else app['bytes']
            if offset < previous_end or source.stat().st_size > limit:
                raise ValueError('ESP payload exceeds its actual region')
            previous_end = offset + source.stat().st_size
            parts.append({'asset': add(source, Path(name).name), 'offset': offset, 'max_bytes': limit})
        stores = [row for row in table if row['name'] == ('lightvm' if config['product'] == 'lightvm' else 'device')]
        if len(stores) != 1:
            raise ValueError('Missing application storage region')
        capacity = {'firmware_bytes': len(app_bytes), 'firmware_region_bytes': app['bytes'],
                    'storage_bytes': stores[0]['bytes'], 'ram': None}
        installation = {'methods': ['usb-serial'], 'parts': parts, 'flash_settings': args['flash_settings'],
                        'partitions': table, 'uf2_family': None, 'chip_revision': chip_revision}
    elif target['platform'] == 'pico':
        from firmware_payloads import PICO_FAMILIES
        layout = read_json(directory / 'storage-layout.json')
        if layout['board'] != target['board'] or layout['flash_bytes'] != flash_bytes or layout['storage_start'] + layout['storage_bytes'] != PICO_FLASH_START + flash_bytes or layout['firmware_end'] not in (layout['storage_start'], layout['storage_start'] - 8192):
            raise ValueError('Inconsistent linked Pico storage layout')
        if layout['storage_bytes'] < 16384 or layout['storage_bytes'] % 8192 or layout['storage_start'] - layout['firmware_end'] != (8192 if 'boot_sequence' in build['features'] else 0):
            raise ValueError('Pico storage/boot journal differs from compiled features')
        data = (directory / 'lightvm.uf2').read_bytes()
        validate_pico_uf2(data, layout['firmware_end'], target['board'])
        blocks = [data[at:at+512] for at in range(0, len(data), 512)]
        blocks = [block for block in blocks if struct.unpack_from('<I', block, 8)[0] == 0x2000]
        raw = b''.join(block[32:288] for block in blocks)
        verify_build(build, raw)
        ram = read_json(directory / 'ram-report.json')
        stacks = read_json(directory / 'stack-paths.json')
        if ram['remaining_bytes_after_reservations'] < 0 or not stacks.get('software_gate_passed') or stacks['elf_sha256'] != hashlib.sha256((directory / 'lightvm.elf').read_bytes()).hexdigest():
            raise ValueError('Pico RAM/stack software gates did not pass')
        capacity = {'firmware_bytes': len(raw), 'firmware_region_bytes': layout['firmware_end'] - PICO_FLASH_START,
                    'storage_bytes': layout['storage_bytes'], 'ram': ram}
        installation = {'methods': build['updates']['methods'], 'parts': [
            {'asset': add(directory / 'lightvm.uf2', 'firmware.uf2'), 'offset': PICO_FLASH_START,
             'max_bytes': capacity['firmware_region_bytes']}], 'flash_settings': None,
             'partitions': [], 'uf2_family': PICO_FAMILIES[target['board']], 'chip_revision': None}
        add(directory / 'stack-paths.json', 'stack-report.json')
    else:
        raise ValueError('Unsupported release platform')
    if config['product'] == 'lightvm':
        limits = read_json(directory / 'release-limits.json')
        if limits['storage_bytes'] != capacity['storage_bytes']:
            raise ValueError('Compiled and linked storage capacities disagree')
        limits['vm_heap_bytes'] = capacity['ram']['vm_heap_capacity'] if capacity['ram'] else None
        if limits['vm_heap_automatic']:
            limits['vm_heap_ceiling_bytes'] = None
    else:
        limits = config['limits']
        if packed(limits).encode() not in app_bytes:
            raise ValueError('Linked analyser limits differ from the release contract')
    notice = []
    for name in config['notices']:
        notice.append('===== ' + name + ' =====\n' + safe_file(root, name).read_text())
    notice.append('===== Shared device services (MIT) =====\n' + (CONTRACTS.parent / 'LICENSE').read_text())
    (output / (key + '-NOTICES.txt')).write_text('\n\n'.join(notice) + '\n')
    assets.append(file_record(output / (key + '-NOTICES.txt')))
    add(matches[0], 'build.json')
    artifact = {'schema': 'kajlights.firmware', 'manifest_version': 1, 'id': target['id'], 'build': build,
                'assets': assets, 'installation': installation, 'capacity': capacity, 'limits': limits,
                'content': config['content'], 'tools': config['tools'],
                'validation': {'physical_install': False, 'ota': False},
                'recovery': {'preservation': 'export-and-clean-install', 'guide': config['recovery']}}
    verify_assets(artifact, output)
    (output / 'artifact.json').write_text(packed(artifact) + '\n')
    return artifact


def assemble(config, bundles, output, tag=None, public=False):
    output = Path(output)
    if output.exists() and any(output.iterdir()):
        raise ValueError('Release destination must be empty')
    output.mkdir(parents=True, exist_ok=True)
    artifacts = [verify_assets(read_json(Path(b) / 'artifact.json'), b) for b in bundles]
    ids = [a['id'] for a in artifacts]
    if sorted(ids) != sorted(t['id'] for t in config['targets']):
        raise ValueError('Release matrix is incomplete or contains duplicate/unapproved builds')
    identities = {(a['build']['product'], a['build']['version'], a['build']['firmware']['source_revision']) for a in artifacts}
    if len(identities) != 1:
        raise ValueError('Mixed product versions or source revisions')
    product, version, revision = identities.pop()
    if product != config['product'] or tag and tag != 'v' + version:
        raise ValueError('Release tag must exactly match the product version')
    if tag and any(a['build']['firmware']['release_tag'] != tag or a['build']['firmware']['source_dirty'] for a in artifacts):
        raise ValueError('Release builds must be clean checkouts of the exact version tag')
    for artifact in artifacts:
        target = next(t for t in config['targets'] if t['id'] == artifact['id'])
        build = artifact['build']
        if (build['board'], build['firmware']['profile'], build['hardware'].get('variant'), build['hardware']['declared_flash_bytes']) != (target['board'], target['profile'], target['variant'], target['flash_bytes']):
            raise ValueError('Bundle differs from its registered target')
        if not set(target['required_features']).issubset(build['features']) or set(target['excluded_features']) & set(build['features']):
            raise ValueError('Bundle differs from its registered feature selection')
    seen = set()
    for directory, artifact in zip(bundles, artifacts):
        for record in artifact['assets']:
            if record['name'] in seen:
                raise ValueError('Duplicate release asset name')
            seen.add(record['name'])
            shutil.copyfile(Path(directory) / record['name'], output / record['name'])
    release = {'schema': 'kajlights.release', 'manifest_version': 1, 'repository': config['repository'],
               'product': product, 'version': version, 'source_revision': revision, 'tag': tag,
               'channel': 'development' if tag is None else 'prerelease' if '-' in version.split('+')[0] else 'stable',
               'distribution': 'public' if public else 'private',
               'provenance': {'workflow': '.github/workflows/release.yml', 'attestation_required': bool(tag and public)},
               'artifacts': sorted(artifacts, key=lambda a: a['id'])}
    validate(release, 'release')
    (output / 'release.json').write_text(packed(release) + '\n')
    checksum = '\n'.join(hashlib.sha256(path.read_bytes()).hexdigest() + '  ' + path.name for path in sorted(output.iterdir())) + '\n'
    (output / 'SHA256SUMS').write_text(checksum)
    return release


def assess(device, artifact, required_features, method, supported_tools=()):
    """Pure candidate preflight; publication/provenance is verified by the importer."""
    validate(artifact, 'firmware')
    build = artifact['build']
    reasons = []
    for key in ('product', 'board'):
        if device.get(key) != build[key]:
            reasons.append('wrong_' + key)
    hardware = device.get('hardware', {})
    for key in ('variant', 'chip'):
        if hardware.get(key) != build['hardware'].get(key):
            reasons.append('wrong_' + key)
    if not set(required_features).issubset(build['features']):
        reasons.append('missing_features')
    if method not in artifact['installation']['methods']:
        reasons.append('unsupported_method')
    if reasons:
        return {'state': 'incompatible', 'reasons': reasons}
    if not device.get('controller_id') or not device.get('boot_id'):
        return {'state': 'cannot_assess', 'reasons': ['device_identity_required']}
    if hardware.get('flash_verified') is not True or hardware.get('flash_bytes') is None:
        return {'state': 'cannot_assess', 'reasons': ['physical_flash_probe_required']}
    if hardware['flash_bytes'] != build['hardware']['declared_flash_bytes']:
        return {'state': 'incompatible', 'reasons': ['wrong_flash_geometry']}
    revisions = artifact['installation']['chip_revision']
    if revisions and (revisions['minimum'] or revisions['maximum'] is not None):
        revision = hardware.get('chip_revision')
        if revision is None:
            return {'state': 'cannot_assess', 'reasons': ['chip_revision_probe_required']}
        if revision < revisions['minimum'] or revisions['maximum'] is not None and revision > revisions['maximum']:
            return {'state': 'incompatible', 'reasons': ['wrong_chip_revision']}
    if artifact['tools']['contract'] not in supported_tools:
        return {'state': 'matching_tools_required', 'reasons': ['tool_contract_unavailable']}
    state = 'installed' if device.get('firmware', {}).get('build_id') == build['firmware']['build_id'] else 'clean_local_install'
    return {'state': state, 'reasons': [] if state == 'installed' else ['export_and_clean_install_required']}


def published_releases(fetch_page, include_prereleases=False):
    """GitHub Releases pagination, explicitly excluding privileged draft visibility."""
    result = []
    for page in range(1, 1001):
        rows = fetch_page(page, 100)
        if not isinstance(rows, list):
            raise ValueError('Invalid release-list response')
        for row in rows:
            if row.get('draft') is not False or not row.get('published_at'):
                continue
            if row.get('prerelease') and not include_prereleases:
                continue
            assets = [a for a in row.get('assets', []) if a.get('name') == 'release.json' and a.get('state') == 'uploaded']
            if len(assets) == 1:
                result.append(row)
        if len(rows) < 100:
            return result
    raise ValueError('Release pagination limit exceeded; refusing a partial catalogue')
