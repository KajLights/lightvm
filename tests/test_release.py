"""Application compatibility, author declarations and compiler provenance."""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from lightvm.compiler import compile_source
from lightvm.release import VERSION, normalize_release, release_error

SOURCE = 'from lightvm import profile, requires, render\nprofile("animation")\n%s\n@render\ndef draw(ctx,runtime,pixels): pass\n'

class ReleaseTests(unittest.TestCase):
    def test_shared_release_ordering(self):
        for current, required, accepted in json.loads(Path(__file__).with_name('release_cases.json').read_text()):
            with self.subTest(current=current, required=required):
                self.assertEqual(release_error(current, required) is None, accepted)

    def test_minimum_declarations_are_normalized_and_target_checked(self):
        for declaration, expected in (('', '1.0.0'), ('requires("1")','1.0.0'), ('requires("1.2")','1.2.0'), ('requires("1.2.3")','1.2.3')):
            source = SOURCE % declaration
            result = compile_source(source, target={'version':'1.9.0'})
            self.assertTrue(result.success, result.diagnostics)
            self.assertEqual(result.manifest['requires'], expected)
            self.assertEqual(result.manifest['compiler_version'], VERSION)
            self.assertNotIn('abi', result.manifest)
        result = compile_source(SOURCE % 'requires("1.2")', target={'version':'1.1.0'})
        self.assertFalse(result.success)
        self.assertIn('requires LightVM 1.2.0', result.diagnostics[0].message)
        self.assertFalse(compile_source(SOURCE % '', target={}).success)

    def test_new_compilers_do_not_raise_existing_program_requirements(self):
        with patch('lightvm.portable.VERSION', '1.25.7+test'):
            result = compile_source(SOURCE % '', target={'version':'1.0.0'})
        self.assertTrue(result.success, result.diagnostics)
        self.assertEqual(result.manifest['compiler_version'], '1.25.7+test')
        self.assertEqual(result.manifest['requires'], '1.0.0')

    def test_invalid_duplicate_or_understated_source_requirements(self):
        for declaration in ('requires("1.02")', 'requires(1)', 'requires("1.2", "1.3")', 'requires(version="1")', 'requires("1")\nrequires("1")', 'requires("0.9")', 'requires("2")', 'requires("1.0.0-alpha")'):
            with self.subTest(declaration=declaration):
                self.assertFalse(compile_source(SOURCE % declaration).success)
        self.assertEqual(normalize_release('1.2.3-rc.1+local'), '1.2.3-rc.1+local')
