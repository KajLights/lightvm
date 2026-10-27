"""Beginner language rules compared with independent Python execution.

Only these repository-owned test snippets run in CPython. Learner source is
always parsed and compiled, never executed by the application.
"""
import array
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import textwrap
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from lightvm.compiler import compile_source

PREFIX = 'from lightvm import profile, init, render, rgb\nfrom array import array\nimport math\nprofile("animation")\n'


class Pixels(list):
    @property
    def count(self): return len(self)
    def fill(self, colour, start=0, stop=None):
        for i in range(start, len(self) if stop is None else stop): self[i] = colour


class TeachingLanguageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.driver = Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME', ROOT / 'build/teaching-host/test_platform'))
        if not cls.driver.exists(): raise unittest.SkipTest('Build test_platform and set LIGHTVM_NUMERIC_RUNTIME first.')
    def test_constants_defaults_and_parameter_scope(self):
        self.compare('''
            value = 4
            ORANGE = rgb(40, 8, 0)
            def amount(result=value):
                return result
            def caller(value):
                value += 1
                return amount() + value
            @render
            def draw(state, runtime, pixels):
                pixels.fill(ORANGE)
                pixels[0] = rgb(caller(7), 0, 0)
        ''')

    def compare(self, source, count=25):
        source = textwrap.dedent(source)
        entries = {}
        def entry(name):
            def decorate(fn): entries[name] = fn; return fn
            return decorate
        namespace = dict(init=entry('init'), render=entry('render'),
                         rgb=lambda r, g, b: (r << 16) | (g << 8) | b,
                         array=array.array, math=math)
        exec(source, namespace)
        state = SimpleNamespace(); runtime = SimpleNamespace(pixel_count=count, time=1.25, delta=0.25)
        pixels = Pixels([None] * count)
        if 'init' in entries: entries['init'](state, runtime)
        entries['render'](state, runtime, pixels)
        expected = b''.join(bytes(4) if c is None else bytes([c >> 16, (c >> 8) & 255, c & 255, 255]) for c in pixels).hex()
        driver = str(self.driver)
        for optimize in (False, True):
            with self.subTest(optimize=optimize):
                result = compile_source(PREFIX + source, optimize=optimize)
                self.assertTrue(result.success, result.diagnostics)
                with tempfile.NamedTemporaryFile() as file:
                    file.write(result.bytecode); file.flush()
                    run = subprocess.run([driver, '--numeric-package', file.name, str(count), '0', '1'],
                                         text=True, capture_output=True, timeout=10, check=True)
                self.assertFalse(run.stderr)
                actual = json.loads(run.stdout)
                self.assertTrue(actual.get('load'), actual)
                self.assertEqual(actual.get('status'), 1, actual)
                self.assertEqual(actual['pixels'], expected)

    def test_loop_target_is_last_value_and_body_can_reassign_it(self):
        for assignment in ('', 'i = 7'):
            self.compare(f'''
                @render
                def draw(state, runtime, pixels):
                    total = 0
                    for i in range(3):
                        total += 1
                        {assignment or 'pass'}
                    pixels[0] = rgb(i, total, 0)
            ''')

    def test_empty_loop_preserves_existing_value(self):
        for bound in (0, -4):
            self.compare(f'''
                @render
                def draw(state, runtime, pixels):
                    i = 7
                    for i in range({bound}):
                        pixels[i] = rgb(int(1.0 / 0.0), 0, 0)
                    pixels[0] = rgb(i, 0, 0)
            ''')

    def test_empty_loop_does_not_create_a_value(self):
        result = compile_source(PREFIX + '@render\ndef draw(s,r,p):\n for i in range(0): pass\n p[0]=rgb(i,0,0)\n')
        self.assertFalse(result.success)
        self.assertIn('Give "i" a value', result.diagnostics[0].message)

    def test_loop_assignment_break_continue_and_nested_name(self):
        self.compare('''
            @render
            def draw(state, runtime, pixels):
                i = 9
                total = 0
                for i in range(4):
                    if i == 1:
                        i = 7
                        continue
                    for i in range(2):
                        total += 1
                    if total > 5:
                        i = 8
                        break
                pixels[0] = rgb(i, total, 0)
        ''')

    def test_range_forms_and_overflow_termination(self):
        for args in ('5', '2, 7', '1, 8, 2', '8, 1, -2', '7, 3', '1, 8, -2',
                     '2147483646, 2147483647, 2', '-2147483647, -2147483648, -2',
                     '2147483647, -2147483648, -2147483648'):
            self.compare(f'''
                @render
                def draw(state, runtime, pixels):
                    total = 0
                    for i in range({args}):
                        total += 1
                    pixels[0] = rgb(total, 0, 0)
            ''')

    def test_range_dynamic_step_and_arguments_are_evaluated_once(self):
        for step in (2, -2):
            self.compare(f'''
                @init
                def setup(state, runtime):
                    state.step = {step}
                    state.calls = 0
                def end(state):
                    state.calls += 1
                    return 7
                @render
                def draw(state, runtime, pixels):
                    total = 0
                    for i in range(1, end(state), state.step):
                        total += 1
                        state.step = 1
                    pixels[0] = rgb(total, state.calls, 0)
            ''')

    def test_array_iteration_and_target_assignment(self):
        self.compare('''
            @init
            def setup(state, runtime):
                state.values = array('i', [2, 4, 6])
            @render
            def draw(state, runtime, pixels):
                total = 0
                for value in state.values:
                    total += value
                    value = 3
                pixels[0] = rgb(total, value, 0)
        ''')

    def test_boolean_values_lazy_effects_and_chained_comparison(self):
        for expression in ('2 and 3', '2 or 3', '0 or 3', '0 and 3', '2 and 0 or 4',
                           '0 and next_value(state)', '2 or next_value(state)',
                           '2 and next_value(state)', '0 or next_value(state)'):
            self.compare(f'''
                @init
                def setup(state, runtime):
                    state.calls = 0
                def next_value(state):
                    state.calls += 1
                    return 3
                @render
                def draw(state, runtime, pixels):
                    result = {expression}
                    pixels[0] = rgb(result, state.calls, 0)
            ''')
        self.compare('''
            @init
            def setup(state, runtime): state.calls = 0
            def next_value(state):
                state.calls += 1
                return state.calls
            @render
            def draw(state, runtime, pixels):
                colour = rgb(0, 0, 0)
                if 0 < next_value(state) < 2:
                    colour = rgb(40, 0, 0)
                if 9 < next_value(state) < next_value(state):
                    colour = rgb(0, 40, 0)
                pixels[0] = colour
                pixels[1] = rgb(state.calls, 0, 0)
        ''')

    def test_both_branch_values_and_exiting_branch(self):
        for test in ('runtime.time < 1.0', 'runtime.time > 1.0'):
            self.compare(f'''
                def choose(value):
                    if value < 0:
                        return 3
                    else:
                        result = 4
                    return result
                @render
                def draw(state, runtime, pixels):
                    if {test}:
                        value = 10
                    else:
                        value = 20
                    pixels[0] = rgb(value, choose(1), 0)
            ''')

    def test_procedure_defaults_and_named_arguments(self):
        self.compare('''
            def marker(pixels, position, colour=40):
                if position < 0:
                    return
                pixels[position] = rgb(colour, 0, 0)
            @render
            def draw(state, runtime, pixels):
                marker(pixels, 0)
                marker(pixels, colour=20, position=1)
                marker(pixels, -1)
        ''')

    def test_earlier_state_operand_is_saved_before_mutation(self):
        self.compare('''
            @init
            def setup(state, runtime): state.value = 2
            def change(state):
                state.value = 5
                return 1
            def combine(first, second): return first + second
            @render
            def draw(state, runtime, pixels):
                a = state.value + change(state)
                state.value = 2
                b = combine(state.value, change(state))
                pixels[0] = rgb(a, b, 0)
        ''')

    def test_invalid_logic_and_procedure_value_have_source_diagnostics(self):
        for body in ('value = 1 and 2.0', 'value = paint(pixels)'):
            result = compile_source(PREFIX + 'def paint(pixels):\n pixels.fill(rgb(1,0,0))\n@render\ndef draw(state,runtime,pixels):\n ' + body + '\n')
            self.assertFalse(result.success)

    def test_fill_saves_colour_and_evaluates_named_arguments_in_source_order(self):
        self.compare('''
            @init
            def setup(state, runtime):
                state.colour = rgb(20, 0, 0)
                state.start = 0
            def change(state):
                state.colour = rgb(0, 20, 0)
                state.start = 2
                return 3
            @render
            def draw(state, runtime, pixels):
                pixels.fill(state.colour, stop=change(state), start=state.start)
        ''')


if __name__ == '__main__': unittest.main()
