"""Standalone numeric conformance; no VM, board build or external Python packages.

Run: python3 -B -m unittest discover -s tests -p test_numeric.py -v
Set CC and LIGHTVM_NUMERIC_CFLAGS to select the compiler or sanitizer flags.
"""
import itertools
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
import unittest

import numeric_oracle as oracle

ROOT = Path(__file__).resolve().parents[1]
VECTORS = ROOT / 'tests/fixtures/numeric-vectors.json'
CATALOGUE = ROOT / 'docs/design/numeric-operations.json'
BINARY = ('q16.add', 'q16.sub', 'q16.mul', 'q16.div', 'q16.mod',
          'q16.min', 'q16.max', 'q16.lt', 'q16.eq', 'i32.and', 'i32.or', 'i32.xor')
UNARY = ('q16.neg', 'q16.sqrt', 'q16.from_i32', 'q16.to_i32',
         'q16.to_f32', 'q16.bits', 'q16.from_bits', 'i32.not')
BOUNDARIES = (-(1 << 31), -(1 << 31) + 1, -16777217, -16777216,
              -131073, -65537, -65536, -65535, -32769, -32768, -3, -2, -1,
              0, 1, 2, 3, 32767, 32768, 32769, 65535, 65536, 65537,
              131073, 16777215, 16777216, 16777217, (1 << 31) - 2, (1 << 31) - 1)


class NumericContractTests(unittest.TestCase):
    driver_args = []
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='lightvm-numeric-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.driver = Path(cls.temp.name) / 'numeric-driver'
        command = shlex.split(os.environ.get('CC', 'cc')) + [
            '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
            '-ffp-contract=off', '-I', str(ROOT / 'src'),
            str(ROOT / 'src/numeric.c'), str(ROOT / 'tests/numeric_driver.c'),
            '-lm', '-o', str(cls.driver)]
        command += shlex.split(os.environ.get('LIGHTVM_NUMERIC_CFLAGS', ''))
        subprocess.run(command, check=True, capture_output=True, text=True)
        cls.vectors = json.loads(VECTORS.read_text())

    def compare(self, cases, rounding=None):
        cases = [(op, tuple(value & oracle.MASK for value in args)) for op, args in cases]
        data = ''.join(f'{op} {args[0]:08x} {(args[1] if len(args) > 1 else 0):08x}\n'
                       for op, args in cases)
        command = [str(self.driver), *self.driver_args] + ([rounding] if rounding else [])
        run = subprocess.run(command, input=data, text=True, capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stderr, '', 'sanitizer or runtime diagnostic')
        lines = run.stdout.splitlines()
        self.assertEqual(len(lines), len(cases))
        for (operation, args), line in zip(cases, lines):
            ok, result = (int(value, base) for value, base in zip(line.split(), (10, 16)))
            try:
                expected = (1, oracle.evaluate(operation, *args))
            except oracle.NumericFault:
                # Checked C operations must also preserve the caller's output.
                expected = (0, 0x5a5aa5a5)
            self.assertEqual((ok, result), expected, (operation, args))

    def test_reviewed_vectors_and_literal_rounding(self):
        cases = []
        for case in self.vectors['exact']:
            args = tuple(int(value, 16) for value in case['args'])
            if case.get('fault'):
                with self.assertRaises(oracle.NumericFault, msg=str(case)):
                    oracle.evaluate(case['op'], *args)
            else:
                self.assertEqual(oracle.evaluate(case['op'], *args), int(case['result'], 16), case)
            cases.append((case['op'], args))
        self.compare(cases)
        for case in self.vectors['literals']:
            if case.get('fault'):
                with self.assertRaises(oracle.NumericFault): oracle.literal(case['source'])
            else: self.assertEqual(oracle.literal(case['source']), int(case['result'], 16), case)

    def test_boundary_pairs(self):
        self.compare((op, pair) for pair in itertools.product(BOUNDARIES, repeat=2) for op in BINARY)
        self.compare((op, (value,)) for value in BOUNDARIES for op in UNARY)

    def test_seeded_raw_words(self):
        rng = random.Random(0x160016)
        pairs = [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(4096)]
        self.compare((op, pair) for pair in pairs for op in BINARY)
        self.compare((op, (pair[0],)) for pair in pairs for op in UNARY)

    def test_shift_counts_and_sign_extension(self):
        self.compare((op, (value, count)) for value in BOUNDARIES
                     for count in (-2147483648, -33, -1, *range(33), 63, 2147483647)
                     for op in ('i32.shl', 'i32.shr'))

    def test_binary32_exponents_subnormals_ties_and_nonfinite(self):
        words = [(sign << 31) | (exponent << 23) | fraction
                 for sign in (0, 1) for exponent in range(256)
                 for fraction in (0, 1, 0x3fffff, 0x400000, 0x7ffffe, 0x7fffff)]
        rng = random.Random(0xf3216)
        words += [rng.getrandbits(32) for _ in range(8192)]
        # Adjacent floats around half-unit ties and both range boundaries.
        words += [(base + offset) & oracle.MASK for base in
                  (0x37000000, 0x37c00000, 0xb7000000, 0xb7c00000, 0x47000000, 0xc7000000)
                  for offset in range(-16, 17)]
        self.compare(('q16.from_f32', (word,)) for word in words)

    def test_conversion_and_square_root_boundaries(self):
        self.compare(('q16.from_i32', (value,)) for value in range(-32770, 32770))
        raw = [sign * ((1 << top) + offset) for top in range(31) for sign in (-1, 1)
               for offset in range(-9, 10)]
        self.compare(('q16.to_f32', (value,)) for value in raw)
        # Cross square-root result transitions, including the largest root.
        raw = [root * root // 65536 + offset for root in
               (0, 1, 2, 255, 256, 257, 65535, 65536, 65537, 11863283)
               for offset in range(-3, 4)]
        self.compare(('q16.sqrt', (value,)) for value in raw)

    def test_conversion_is_independent_of_host_rounding(self):
        cases = [(case['op'], tuple(int(value, 16) for value in case['args']))
                 for case in self.vectors['exact']
                 if case['op'] in ('q16.from_f32', 'q16.to_f32')]
        for mode in ('nearest', 'down', 'up', 'zero'):
            with self.subTest(rounding=mode): self.compare(cases, rounding=mode)

    def test_catalogue_contract_coverage(self):
        catalogue = json.loads(CATALOGUE.read_text())
        names = [op['name'] for op in catalogue['operations']]
        self.assertEqual(len(names), len(set(names)))
        exact = {case['op'] for case in self.vectors['exact']}
        native = {case['op'] for case in self.vectors['native']}
        types = {'q16', 'i32', 'f32', 'bool', 'literal'}
        for op in catalogue['operations']:
            self.assertTrue(set(op['arguments']) <= types, op)
            self.assertIn(op['result'], types, op)
            self.assertEqual(op['effects'], [], op)
            self.assertTrue(op['semantics'] and op['accuracy'] and op['feature_floor'], op)
            if op['implementation'] == 'runtime':
                self.assertIn(op['name'], exact, op)
                self.assertEqual(op['wire_id']['kind'], 'instruction')
                self.assertEqual(op['feature_floor']['release'], '1.2.0')
            elif op['name'] == 'q16.literal':
                self.assertTrue(self.vectors['literals'])
                self.assertEqual(op['wire_id'], {'kind':'constant', 'type':7})
            else:
                self.assertIsNone(op['wire_id'])
                self.assertIn(op['name'], native, op)
            if op['faults'] and op['implementation'] == 'runtime':
                self.assertTrue(any(case['op'] == op['name'] and case.get('fault')
                                    for case in self.vectors['exact']), op)
        implemented = set(BINARY + UNARY + ('q16.from_f32', 'i32.shl', 'i32.shr'))
        self.assertEqual({op['name'] for op in catalogue['operations']
                          if op['implementation'] == 'runtime'}, implemented)


if __name__ == '__main__':
    unittest.main()
