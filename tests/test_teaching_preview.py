"""Exercise the real C VM through its browser adapter, including source watches."""
import json
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source

PREFIX = 'from lightvm import profile, init, render, rgb, Input\nfrom lightvm.motion import wrap_index\nimport random\nprofile("animation")\n'


class TeachingPreviewTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT / 'build/editor-assets/preview/lightvm-preview.wasm').exists():
            raise unittest.SkipTest('Run npm run build:preview before the browser VM checks.')
    def run_source(self, source, *, debug=False, **options):
        result = compile_source(PREFIX + source, debug=debug)
        self.assertTrue(result.success, result.diagnostics)
        self.assertEqual(len(result.debug['instructions']), int.from_bytes(result.bytecode[12:16], 'little'))
        request = {'artifact': {'bytecode': list(result.bytecode), 'manifest': result.manifest, 'debug': result.debug, 'diagnostics': []}, **options}
        return json.loads(subprocess.check_output(['node', '--disable-warning=ExperimentalWarning', 'tests/preview_replay.mjs'], input=json.dumps(request), text=True, cwd=ROOT, timeout=20))

    def test_loop_and_branch_output_matches_with_and_without_optimisation(self):
        source = '''@render
def draw(state, runtime, pixels):
    for position in range(pixels.count):
        if position % 2 == 0:
            pixels[position] = rgb(position, 0, 0)
        else:
            pixels[position] = rgb(0, position, 0)
'''
        for count in (1, 2, 12, 16, 24, 25, 31, 32, 33, 50, 63, 64, 65, 100):
            ordinary = self.run_source(source, count=count)[-1]
            stepped = self.run_source(source, count=count, debug=True)[-1]
            self.assertEqual(ordinary['pixels'], stepped['pixels'])
            self.assertEqual(ordinary['phase'], 2)

    def test_faults_point_to_source_including_map_body(self):
        cases = [('pixels[pixels.count] = rgb(1, 2, 3)', 'outside', 7),
                 ('for i in range(pixels.count):\n        pixels[i] = rgb(i + 256, 0, 0)', '255', 8),
                 ('while True:\n        pass', 'loop', 7)]
        for body, reason, line in cases:
            for debug in (False, True):
                with self.subTest(body=body, debug=debug):
                    result = self.run_source('@render\ndef draw(state, runtime, pixels):\n    ' + body + '\n', debug=debug)[-1]
                    self.assertEqual(result['phase'], 3)
                    self.assertIn(reason, result['error']['message'])
                    self.assertEqual(result['error']['line'], line)

    def test_incomplete_frame_keeps_last_complete_pixels(self):
        result = self.run_source('''@render
def draw(state, runtime, pixels):
    pixels.fill(rgb(20, 0, 0))
    if runtime.time > 0.0:
        pixels[0] = rgb(0, 20, 0)
        pixels[pixels.count] = rgb(1, 0, 0)
''', commands=[{'type': 'open'}, {'type': 'finish'}, {'type': 'frame', 'delta': 0.25}])
        self.assertEqual(result[-1]['phase'], 3)
        self.assertEqual(result[-1]['pixels'], result[-2]['pixels'])

    def test_watches_keep_named_values_when_temporary_registers_are_reused(self):
        result = self.run_source('''@render
def draw(state, runtime, pixels):
    first = 4
    second = 5
    total = first + second
    first, second = second, first
    pixels.fill(rgb(total, first, second))
''', debug=True, commands=[{'type': 'open'}] + [{'type': 'step'}] * 8)
        values = [r['values'] for r in result]
        self.assertTrue(any(v.get('first') == 4 and v.get('second') == 5 and v.get('total') == 9 for v in values), values)
        self.assertTrue(any(v.get('first') == 5 and v.get('second') == 4 and v.get('total') == 9 for v in values), values)

    def test_seeded_choices_restart_and_memory_is_released(self):
        result = self.run_source('''@init
def start(state, runtime): random.seed(17)
@render
def draw(state, runtime, pixels):
    pixels.fill(rgb(random.randint(1, 200), 0, 0))
''', commands=[{'type': 'open'}, {'type': 'finish'}, {'type': 'open'}, {'type': 'finish'}, {'type': 'close'}])
        self.assertEqual(result[1]['pixels'], result[3]['pixels'])
        self.assertEqual(result[1]['heap'], result[3]['heap'])
        self.assertEqual(result[-1]['heap'], 0)

    def test_ring_reversal_at_irregular_times(self):
        commands = [{'type': 'open'}, {'type': 'finish'}] + [{'type': 'frame', 'delta': d} for d in (0, .016, .033, .25, 1)]
        source = '''@render
def draw(state, runtime, pixels):
    position = wrap_index(-int(runtime.time * 10.0), pixels.count)
    pixels[position] = rgb(20, 0, 0)
'''
        a = self.run_source(source, commands=commands)
        b = self.run_source(source, commands=commands, debug=True)
        self.assertEqual([r['pixels'] for r in a], [r['pixels'] for r in b])
        self.assertTrue(all(r['phase'] == 2 for r in a[1:]))


if __name__ == '__main__': unittest.main()
