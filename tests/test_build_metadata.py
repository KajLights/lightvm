"""Build identity changes with selected inputs, without changing the application version."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'shared/device-core'
spec = importlib.util.spec_from_file_location('build_metadata', CORE / 'tools/build_metadata.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BuildMetadataTests(unittest.TestCase):
    def test_exact_version_tag_is_an_identity_input_without_creating_tags(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            args = argparse.Namespace(root=ROOT, core=CORE, output=work/'out', cache=work/'absent',
                features='usb_management,device_metadata,configuration', product='lightvm',
                version='0.1.0', board='pico_w', platform='pico', flash_bytes=2097152,
                sdkconfig='', layout='', compiler='test', build_type='Release', sdk_root='')
            present = False
            def git(root, *command):
                self.assertEqual(command, ('rev-parse', '--verify', 'refs/tags/v0.1.0^{commit}'))
                if not present:
                    raise subprocess.CalledProcessError(128, command)
                return ('1'*40).encode()
            with patch.object(module, 'source', return_value={'revision': '1'*40, 'dirty': False}), patch.object(module, 'git', side_effect=git):
                development = module.generate(args)
                present = True
                released = module.generate(args)
            self.assertIsNone(development['firmware']['release_tag'])
            self.assertEqual(released['firmware']['release_tag'], 'v0.1.0')
            self.assertEqual(released['inputs']['release_tag'], 'v0.1.0')
            self.assertNotEqual(development['firmware']['build_id'], released['firmware']['build_id'])

    def test_cmake_path_lists_hash_files_and_omit_sdk_directory_locations(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            # Pico's computed component list exceeds Linux's single-path limit.
            directories = [work / ('component_' + str(i)) for i in range(200)]
            for path in directories:
                path.mkdir()
            paths = ';'.join(map(str, directories))
            self.assertIsNone(module.cache_value(paths))
            source = work / 'board;variant.h'
            source.write_text('#define BOARD_FLASH_SIZE 2097152\n')
            escaped = str(source).replace(';', r'\;')
            first = module.cache_value(paths + ';' + escaped + ';ON')
            self.assertEqual(first, [module.cache_value(escaped), 'ON'])
            source.write_text('#define BOARD_FLASH_SIZE 4194304\n')
            self.assertNotEqual(module.cache_value(escaped), first[0])
            self.assertEqual(module.cache_value(''), '')

    def test_schema_and_identity_include_features_build_type_and_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            cache = work / 'cache.txt'
            cache.write_text('LIGHTVM_STORAGE_BYTES:STRING=524288\nLIGHTVM_MQTT:BOOL=OFF\n')
            args = argparse.Namespace(root=ROOT, core=CORE, output=work / 'out', cache=cache,
                features='usb_management,device_metadata,configuration', product='lightvm',
                version='0.1.0', board='pico_w', platform='pico', flash_bytes=2097152,
                sdkconfig='', layout='', compiler='test-compiler', build_type='Release', sdk_root='')
            first = module.generate(args)
            schema = json.loads((CORE / 'contracts/build.schema.json').read_text())
            Draft202012Validator.check_schema(schema)
            Draft202012Validator(schema).validate(first)
            args.features = 'configuration,device_metadata,usb_management,usb_management'
            self.assertEqual(module.generate(args), first)
            args.features += ',mqtt,home_assistant'
            cache.write_text('LIGHTVM_STORAGE_BYTES:STRING=524288\nLIGHTVM_MQTT:BOOL=ON\n')
            mqtt = module.generate(args)
            self.assertEqual(mqtt['version'], first['version'])
            self.assertNotEqual(mqtt['firmware']['build_id'], first['firmware']['build_id'])
            self.assertNotEqual(mqtt['firmware']['features_hash'], first['firmware']['features_hash'])
            self.assertEqual(mqtt['firmware']['layout_id'], first['firmware']['layout_id'])
            args.build_type = 'Debug'
            self.assertNotEqual(module.generate(args)['firmware']['build_id'], mqtt['firmware']['build_id'])
            cache.write_text('LIGHTVM_STORAGE_BYTES:STRING=1048576\nLIGHTVM_MQTT:BOOL=ON\n')
            self.assertNotEqual(module.generate(args)['firmware']['layout_id'], mqtt['firmware']['layout_id'])
            for features in ['not_a_feature', 'home_assistant', 'ota']:
                args.features = features
                with self.subTest(features=features), self.assertRaises(ValueError):
                    module.generate(args)


if __name__ == '__main__':
    unittest.main()
