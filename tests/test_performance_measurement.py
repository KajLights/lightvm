import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from benchmark_saved_preset import sample
from summarize_pico_campaign import summarize


class PerformanceMeasurementTests(unittest.TestCase):
    def capture(self, transition):
        class Client:
            calls = 0

            def describe(self):
                return {'boot_id': 'same'}

            def transition(self, **kwargs):
                self.calls += 1
                frame = 100 + 20 * (self.calls - 1)
                layer = {'id': 'main', 'generation': 3, 'frames': frame,
                         'render_total_us': frame * 100}
                return {'active': transition, 'frames': frame if transition else 0,
                        'A': {'frames': frame, 'layers': [layer]}, 'B': {}}

        with patch('benchmark_saved_preset.time.monotonic', side_effect=range(10)):
            return sample(Client(), 'lights', 0)

    def test_physical_counter_uses_composition_without_transition(self):
        for transition, path in ((False, 'A.frames'), (True, 'frames')):
            with self.subTest(transition=transition):
                result = self.capture(transition)
                self.assertEqual(result['presentation_counter_path'], path)
                self.assertEqual(result['presentations_per_second'], 20 / result['seconds'])
                self.assertGreater(result['presentations_per_second'], 0)

    def test_legacy_unused_counter_is_excluded_from_summary(self):
        result = self.capture(False)
        del result['presentation_counter_path']
        result['presentations_per_second'] = 0
        result.update(name='fixed12', case={'programs': ['solid', 'fixed12_original']})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'run.json'
            path.write_text(json.dumps({'samples': [result]}))
            row = summarize([path])['fixed12']
        self.assertIsNone(row['median_presentations_per_second'])
        self.assertEqual(row['valid_presentation_trials'], 0)
        self.assertGreater(row['layers']['A/main']['median_updates_per_second'], 0)


if __name__ == '__main__':
    unittest.main()
