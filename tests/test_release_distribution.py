"""Real payload formats and failure cases for the shared release/discovery contract."""
import copy
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'shared/device-core/tools'))
import release_contract as contract
from discover_releases import discover, validate_published
from release import prepare


def image(payload, chip=9):
    header = bytearray(24)
    header[0:2] = bytes([0xe9, 1])
    header[23] = 1
    struct.pack_into('<H', header, 12, chip)
    struct.pack_into('<HH', header, 15, 0, 65535)
    payload += bytes(-len(payload) % 4)
    body = header + struct.pack('<II', 0x3c000020, len(payload)) + payload
    checksum = 0xef
    for byte in payload:
        checksum ^= byte
    body += bytes(15 - len(body) % 16) + bytes([checksum])
    return body + hashlib.sha256(body).digest()


class ReleaseDistributionTests(unittest.TestCase):
    def test_shared_browser_candidate_fixtures_match_python(self):
        fixture = contract.read_json(ROOT/'shared/device-core/contracts/candidate-cases.json')
        def merge(base, patch):
            for key, value in patch.items():
                base[key] = merge(base.get(key, {}), value) if isinstance(value, dict) else value
            return base
        for case in fixture['cases']:
            with self.subTest(name=case['name']):
                device = merge(copy.deepcopy(fixture['device']), case.get('device', {}))
                self.assertEqual(contract.assess(device, fixture['artifact'], case.get('required_features', fixture['required_features']),
                    case.get('method', fixture['method']), case.get('supported_tools', fixture['supported_tools'])), case['expected'])
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        config = contract.read_json(ROOT / 'release/targets.json')
        self.target = next(t for t in config['targets'] if t['id'] == 'lightvm-xiao_esp32s3-standard')
        self.config = {**config, 'targets': [self.target]}
        self.build_dir = self.work / 'build'
        self.build_dir.mkdir()
        features = sorted(self.target['required_features'])
        inputs = {'product': 'lightvm', 'version': '0.1.0', 'board': 'xiao_esp32s3', 'features': features,
                  'source': {'revision': '1'*40, 'dirty': False}, 'device_core': {'revision': '2'*40, 'dirty': False},
                  'release_tag': 'v0.1.0'}
        self.build = {'schema': 'kajlights.build', 'product': 'lightvm', 'version': '0.1.0', 'board': 'xiao_esp32s3',
            'firmware': {'build_id': 'sha256:' + contract.digest(inputs), 'source_revision': '1'*40,
                'device_core_revision': '2'*40, 'source_dirty': False, 'release_tag': 'v0.1.0',
                'profile': 'standard', 'features_hash': 'sha256:' + contract.digest(features),
                'layout_id': 'local-test', 'boot_contract': None}, 'features': features,
            'hardware': {'target': 'xiao_esp32s3', 'variant': None, 'chip': 'ESP32-S3',
                'declared_flash_bytes': 8388608, 'flash_bytes': None, 'flash_verified': False},
            'updates': {'supported': False, 'methods': ['usb-serial']}, 'inputs': inputs}
        self.write_build()
        rows = [struct.pack('<HBBII16sI', 0x50aa, kind, subtype, offset, size, name.encode(), 0)
                for name, kind, subtype, offset, size in [
                    ('nvs', 1, 2, 0x9000, 0x6000), ('phy_init', 1, 1, 0xf000, 0x1000),
                    ('factory', 0, 0, 0x10000, 0x30000), ('lightvm', 1, 0x40, 0x40000, 0x80000)]]
        table = b''.join(rows)
        table += b'\xeb\xeb' + b'\xff'*14 + hashlib.md5(table).digest()
        table += b'\xff' * (3072-len(table))
        for name, data in [('bootloader/bootloader.bin', image(b'bootloader')),
                           ('partition_table/partition-table.bin', table)]:
            path = self.build_dir / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.args = {'flash_settings': {'flash_mode': 'dio', 'flash_size': '8MB', 'flash_freq': '40m'},
            'extra_esptool_args': {'chip': 'esp32s3'},
            'flash_files': {'0x0': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'lightvm.bin'},
            'bootloader': {'offset': '0x0', 'file': 'bootloader/bootloader.bin'},
            'partition-table': {'offset': '0x8000', 'file': 'partition_table/partition-table.bin'},
            'app': {'offset': '0x10000', 'file': 'lightvm.bin'}}
        self.write_args()
        (self.build_dir / 'release-limits.json').write_text(json.dumps({'config_bytes': 8192, 'module_bytes': 32768,
            'pixel_capacity': 1024, 'storage_bytes': 524288, 'vm_heap_ceiling_bytes': 131072,
            'vm_heap_automatic': 1, 'native_headroom_bytes': 65536}))

    def write_build(self):
        record = self.build_dir / 'device-build/build.json'
        record.parent.mkdir(exist_ok=True)
        record.write_text(json.dumps(self.build))
        payload = contract.packed(self.build['firmware']).encode() + b'\0' + contract.packed(self.build['features']).encode()
        (self.build_dir / 'lightvm.bin').write_bytes(image(payload))

    def write_args(self):
        (self.build_dir / 'flasher_args.json').write_text(json.dumps(self.args))

    def package(self, name='bundle'):
        return contract.package(ROOT, self.config, self.target, self.build_dir, self.work / name)

    def test_complete_bundle_uses_actual_partition_and_capacity_and_asset_digests(self):
        artifact = self.package()
        self.assertEqual(artifact['capacity']['firmware_region_bytes'], 0x30000)  # Not a hardcoded 2 MiB.
        self.assertEqual(artifact['capacity']['storage_bytes'], 524288)
        self.assertIsNone(artifact['limits']['vm_heap_bytes'])  # ESP runtime budget is not a physical measurement.
        self.assertEqual(len(artifact['installation']['parts']), 3)
        self.assertFalse(artifact['validation']['ota'])
        self.assertEqual(artifact['recovery']['preservation'], 'export-and-clean-install')
        contract.verify_assets(artifact, self.work/'bundle')
        name = artifact['installation']['parts'][-1]['asset']
        with (self.work/'bundle'/name).open('ab') as output:
            output.write(b'corruption')
        with self.assertRaises(ValueError):
            contract.verify_assets(artifact, self.work/'bundle')

    def test_rejects_wrong_target_missing_part_bad_chip_and_truncated_or_corrupt_images(self):
        original = (self.build_dir/'lightvm.bin').read_bytes()
        for data in [b'bad', image(b'other build'), image(b'wrong chip', chip=5), original[:-1], original[:-32] + bytes(32)]:
            (self.build_dir/'lightvm.bin').write_bytes(data)
            destination = self.work/'bundle'
            shutil.rmtree(destination, ignore_errors=True)
            with self.subTest(data=data[:12]), self.assertRaises(ValueError):
                self.package()
        (self.build_dir/'lightvm.bin').write_bytes(original)
        self.args['flash_files'].pop('0x8000')
        self.write_args()
        shutil.rmtree(self.work/'bundle', ignore_errors=True)
        with self.assertRaises(ValueError):
            self.package()

    def test_rejects_mismatched_build_inputs_profile_and_unimplemented_ota(self):
        for mutate in [lambda b: b['inputs'].update(version='0.1.1'),
                       lambda b: b['firmware'].update(profile='audio'),
                       lambda b: b['updates'].update(supported=True),
                       lambda b: b['hardware'].update(variant='sense_pdm')]:
            original = copy.deepcopy(self.build)
            mutate(self.build)
            self.write_build()
            shutil.rmtree(self.work/'bundle', ignore_errors=True)
            with self.assertRaises(ValueError):
                self.package()
            self.build = original

    def test_partition_table_integrity_overlap_and_image_boundaries(self):
        table_path = self.build_dir/'partition_table/partition-table.bin'
        data = bytearray(table_path.read_bytes())
        data[10] ^= 1
        table_path.write_bytes(data)
        with self.assertRaises(ValueError):
            self.package()
        with self.assertRaises(ValueError):
            contract.partition_table(bytes(32))
        self.assertEqual(contract.esp_image(image(b'test'), 'ESP32-S3'), {'minimum': 0, 'maximum': None})

    def test_rejects_duplicate_json_members_and_unsafe_or_symlink_assets(self):
        path = self.work/'bad.json'
        for text in ['{"schema":1,"schema":2}', '{"value":NaN}', '{"value":Infinity}']:
            path.write_text(text)
            with self.assertRaises(ValueError):
                contract.read_json(path)
        (self.work/'link').symlink_to(ROOT/'LICENSE')
        for name in ['../LICENSE', '/etc/passwd', 'link']:
            with self.assertRaises(ValueError):
                contract.safe_file(self.work, name)

    def test_release_assembly_requires_complete_matrix_exact_tag_and_clean_identity(self):
        self.package()
        bundle = self.work/'bundle'
        manifest = contract.assemble(self.config, [bundle], self.work/'release', 'v0.1.0', public=True)
        self.assertEqual(manifest['channel'], 'stable')
        self.assertTrue(manifest['provenance']['attestation_required'])
        self.assertTrue((self.work/'release/SHA256SUMS').is_file())
        for config, bundles, tag in [(self.config, [], 'v0.1.0'), (self.config, [bundle, bundle], 'v0.1.0'),
                                     (self.config, [bundle], 'v0.1.1'),
                                     (contract.read_json(ROOT/'release/targets.json'), [bundle], 'v0.1.0')]:
            with self.assertRaises(ValueError):
                contract.assemble(config, bundles, self.work/('bad-'+str(len(list(self.work.iterdir())))), tag)
        dirty = contract.read_json(bundle/'artifact.json')
        dirty['build']['firmware']['source_dirty'] = True
        (bundle/'artifact.json').write_text(json.dumps(dirty))
        with self.assertRaises(ValueError):
            contract.assemble(self.config, [bundle], self.work/'dirty', 'v0.1.0')

    def test_candidate_matching_needs_real_geometry_tools_and_requested_features(self):
        artifact = self.package()
        device = {**copy.deepcopy(artifact['build']), 'controller_id': 'device-one', 'boot_id': 'boot-one'}
        def assess(required=(), method='usb-serial', tools=('lightvm-0.1.0',)):
            return contract.assess(device, artifact, required, method, tools)
        self.assertEqual(assess()['reasons'], ['physical_flash_probe_required'])
        device['hardware'].update(flash_verified=True, flash_bytes=8388608)
        self.assertEqual(assess()['state'], 'installed')
        self.assertEqual(assess(tools=())['state'], 'matching_tools_required')
        self.assertEqual(assess(['mqtt'])['reasons'], ['missing_features'])
        self.assertEqual(assess(method='http-push')['reasons'], ['unsupported_method'])
        device['firmware']['build_id'] = 'sha256:' + '3'*64
        self.assertEqual(assess()['state'], 'clean_local_install')  # Same product version, different build.
        device['hardware']['flash_bytes'] = 4194304
        self.assertEqual(assess()['reasons'], ['wrong_flash_geometry'])
        device['board'] = 'xiao_esp32c3'
        self.assertIn('wrong_board', assess()['reasons'])

    def test_release_discovery_handles_pagination_drafts_prereleases_and_missing_manifests(self):
        good = {'id': 1, 'draft': False, 'published_at': '2026-10-30T00:00:00Z', 'prerelease': False,
                'assets': [{'name': 'release.json', 'state': 'uploaded'}]}
        rows = [dict(good, id=n) for n in range(100)]
        tail = [dict(good, id=101, draft=True), dict(good, id=102, prerelease=True),
                dict(good, id=103, assets=[]), dict(good, id=104)]
        pages = []
        def fetch(page, count):
            pages.append((page, count))
            return rows if page == 1 else tail
        releases = contract.published_releases(fetch)
        self.assertEqual(len(releases), 101)
        self.assertEqual(pages, [(1, 100), (2, 100)])
        self.assertEqual(len(contract.published_releases(fetch, True)), 102)
        self.assertEqual(contract.published_releases(lambda p, c: []), [])
        with self.assertRaises(ValueError):
            contract.published_releases(lambda p, c: {'error': 'rate limited'})

    def test_published_identity_and_provenance_gate_cannot_admit_a_draft_or_changed_tag(self):
        self.package()
        manifest = contract.assemble(self.config, [self.work/'bundle'], self.work/'release', 'v0.1.0', public=True)
        release = {'draft': False, 'published_at': 'now', 'prerelease': False, 'tag_name': 'v0.1.0'}
        validate_published(manifest, release, 'KajLights/lightvm', '1'*40)
        for change in [dict(release, draft=True), dict(release, prerelease=True), dict(release, tag_name='v0.1.1')]:
            with self.assertRaises(ValueError):
                validate_published(manifest, change, 'KajLights/lightvm', '1'*40)
        with self.assertRaises(ValueError):
            validate_published(manifest, release, 'KajLights/lightvm', '4'*40)
        with patch('discover_releases.api', return_value={'private': True}), self.assertRaises(ValueError):
            discover('KajLights/kaj-fi')

    def test_prepare_uses_the_current_version_without_creating_a_tag(self):
        result = prepare(ROOT, contract.read_json(ROOT/'release/targets.json'), None)
        self.assertEqual(result['version'], '0.1.0')
        self.assertEqual(len(result['pico']['include']), 5)
        self.assertEqual(len(result['esp32']['include']), 6)
        with self.assertRaises(ValueError):
            prepare(ROOT, self.config, 'v1.0.0')

    def test_verified_mirror_callback_runs_after_all_hashes_and_provenance_pass(self):
        self.package()
        release_dir = self.work/'release'
        manifest = contract.assemble(self.config, [self.work/'bundle'], release_dir, 'v0.1.0', public=True)
        (release_dir/'release.sigstore.json').write_text('{}')
        row = {'id': 9, 'draft': False, 'published_at': '2026-10-30T00:00:00Z', 'prerelease': False,
               'tag_name': 'v0.1.0', 'html_url': 'https://github.com/KajLights/lightvm/releases/tag/v0.1.0',
               'assets': [{'name': p.name, 'state': 'uploaded', 'size': p.stat().st_size} for p in release_dir.iterdir()]}
        commands, observed = [], []
        def api(endpoint):
            return {'private': False} if endpoint == 'repos/KajLights/lightvm' else [row]
        def run(command, **kwargs):
            commands.append(command)
            if command[1:3] == ['release', 'download']:
                name = command[command.index('--pattern')+1]
                shutil.copyfile(release_dir/name, Path(command[command.index('--dir')+1])/name)
        def verified(record, directory):
            self.assertEqual(record['manifest'], manifest)
            for artifact in manifest['artifacts']:
                contract.verify_assets(artifact, directory)
            observed.append(record['release_id'])
        with patch('discover_releases.api', side_effect=api), patch('discover_releases.resolve_tag', return_value='1'*40), patch('discover_releases.subprocess.run', side_effect=run):
            discover('KajLights/lightvm', on_verified=verified)
            self.assertEqual(observed, [9])
            verification = next(c for c in commands if c[1:3] == ['attestation', 'verify'])
            self.assertIn('--source-digest', verification)
            self.assertIn('--signer-workflow', verification)
            observed.clear()
            asset = manifest['artifacts'][0]['assets'][0]['name']
            (release_dir/asset).write_bytes(bytes((release_dir/asset).stat().st_size))
            with self.assertRaises(ValueError):
                discover('KajLights/lightvm', on_verified=verified)
            self.assertEqual(observed, [])


if __name__ == '__main__':
    unittest.main()
