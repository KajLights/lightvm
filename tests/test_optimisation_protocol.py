"""Guard against false performance passes, using synthetic measured windows."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from compare_optimisation import compare


def run_document(run_id):
    layer = {'updates_per_second': 40, 'mean_render_us': 2000,
             'before': {'generation': 3}, 'after': {'generation': 3}}
    rows = [{'case': 'mixed', 'trial': trial, 'settings': {}, 'seconds': 30,
             'layers': {'A/layer0': copy.deepcopy(layer), 'A/layer1': copy.deepcopy(layer)},
             'description_before': {'boot_id': 'boot'}, 'description_after': {'boot_id': 'boot'},
             'status_latency_ms': {'samples': [5] * 600},
             'foreground_gap_samples': [{'ingress_over_10ms': 20}, {'ingress_over_10ms': 20}],
             'heap_samples': [{'heap': 12000}, {'heap': 12000}]}
            for trial in range(1, 6)]
    return {'schema': 'lightvm.optimisation-run.v1', 'run_id': run_id,
            'measurement_complete': True, 'temporary_cleanup_complete': True,
            'board': 'pico_w', 'rig': 'pico-w-01', 'pin': 'GP16', 'pixels': 200,
            'brightness_limit': 32, 'output_type': 'ws2812b', 'network_regime': 'idle',
            'warmup_seconds': 7, 'seconds_requested': 30, 'poll_hz': 20,
            'packages': {'map': {'bytecode_sha256': 'a' * 64}},
            'selected_cases': ['mixed'], 'samples_requested': 5,
            'firmware_sha256': run_id * 64, 'ingress_instrumented': True, 'samples': rows}


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.baseline, self.candidate = run_document('a'), run_document('b')

    def compare(self):
        with tempfile.TemporaryDirectory() as directory:
            a, b = [Path(directory) / (name + '.json') for name in ('a', 'b')]
            a.write_text(json.dumps(self.baseline))
            b.write_text(json.dumps(self.candidate))
            return compare([a], [b])

    def test_matched_windows_pass(self):
        self.assertEqual(self.compare()['verdict'], 'pass')

    def test_one_slow_layer_cannot_hide_behind_faster_presentations(self):
        for row in self.candidate['samples']:
            row['presentations_per_second'] = 150
            row['layers']['A/layer0']['updates_per_second'] = 100
            row['layers']['A/layer1']['updates_per_second'] = 30
        self.assertEqual(self.compare()['verdict'], 'fail')

    def test_render_cost_regression_fails_even_when_rate_is_unchanged(self):
        for row in self.candidate['samples']:
            row['layers']['A/layer0']['mean_render_us'] = 2200
        self.assertEqual(self.compare()['verdict'], 'fail')

    def test_stall_or_heap_growth_fails(self):
        for field, observations in (
            ('foreground_gap_samples', [{'ingress_over_10ms': 20}, {'ingress_over_10ms': 21}]),
            ('heap_samples', [{'heap': 12000}, {'heap': 12016}]),
        ):
            with self.subTest(field=field):
                self.candidate = run_document('b')
                self.candidate['samples'][0][field] = observations
                self.assertEqual(self.compare()['verdict'], 'fail')

    def test_latency_relative_and_absolute_gates(self):
        for latency in (6.1, 26):
            with self.subTest(latency=latency):
                for row in self.candidate['samples']:
                    row['status_latency_ms']['samples'] = [latency] * 600
                self.assertEqual(self.compare()['verdict'], 'fail')

    def test_uninstrumented_zero_and_short_screens_are_incomplete(self):
        self.candidate['ingress_instrumented'] = False
        self.assertEqual(self.compare()['verdict'], 'incomplete')
        self.candidate['ingress_instrumented'] = True
        for row in self.candidate['samples']:
            row['status_latency_ms']['samples'] = [30] * 60
        self.assertEqual(self.compare()['verdict'], 'incomplete')

    def test_incompatible_runs_are_rejected(self):
        for key, value in (('board', 'xiao_esp32c3'), ('rig', 'other-board'),
                           ('pixels', 500), ('network_regime', 'loaded'), ('poll_hz', 0),
                           ('packages', {'map': {'bytecode_sha256': 'b' * 64}})):
            with self.subTest(key=key):
                self.candidate = run_document('b')
                self.candidate[key] = value
                with self.assertRaises(ValueError):
                    self.compare()

    def test_partial_duplicate_and_failed_cleanup_are_rejected(self):
        for change in ('missing', 'duplicate', 'cleanup', 'run_id', 'settings', 'layer', 'reboot', 'nan'):
            with self.subTest(change=change):
                self.candidate = run_document('b')
                if change == 'missing': self.candidate['samples'].pop()
                if change == 'duplicate': self.candidate['samples'].append(self.candidate['samples'][0])
                if change == 'cleanup': self.candidate['temporary_cleanup_complete'] = False
                if change == 'run_id': self.candidate['run_id'] = 'a'
                if change == 'settings': self.candidate['samples'][0]['settings'] = {'capacity': 1}
                if change == 'layer': self.candidate['samples'][0]['layers'].pop('A/layer1')
                if change == 'reboot': self.candidate['samples'][0]['description_after']['boot_id'] = 'new'
                if change == 'nan': self.candidate['samples'][0]['layers']['A/layer0']['mean_render_us'] = float('nan')
                with self.assertRaises(ValueError): self.compare()

    def test_stochastic_saved_stars_is_diagnostic(self):
        for doc in (self.baseline, self.candidate):
            doc['selected_cases'] = ['saved-stars']
            for row in doc['samples']:
                row['case'] = 'saved-stars'
                row['layers']['A/layer0']['updates_per_second'] = 0
                row['layers']['A/layer0']['mean_render_us'] = None
        for row in self.candidate['samples']:
            row['layers']['A/layer1']['updates_per_second'] = 20
            row['layers']['A/layer1']['mean_render_us'] = 4000
        result = self.compare()
        self.assertEqual(result['cases']['saved-stars']['layers']['A/layer1']['verdict'], 'diagnostic')
        self.assertEqual(result['verdict'], 'incomplete')


if __name__ == '__main__':
    unittest.main()
