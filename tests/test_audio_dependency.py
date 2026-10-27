"""A clean build needs no private-repository token and never overwrites edits."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from audio_dependency import materialize, verify, manifest


class AudioDependencyTests(unittest.TestCase):
    def test_offline_extract_is_pinned_and_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)/'dependency'
            core = materialize(destination)
            self.assertEqual(verify(core), manifest()['revision'])
            self.assertEqual(materialize(destination), core)
            self.assertFalse((destination/'.git').exists())

    def test_modified_source_is_rejected_and_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory)/'dependency'
            core = materialize(destination)
            path = core/'onset.c'
            original = path.read_bytes()
            path.write_bytes(original+b'\n/* local edit */\n')
            with self.assertRaisesRegex(ValueError, 'onset.c'):
                materialize(destination)
            self.assertEqual(path.read_bytes(), original+b'\n/* local edit */\n')


if __name__ == '__main__':
    unittest.main()
