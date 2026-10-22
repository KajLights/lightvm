"""Frozen high-precision references through shared native math and scalar VM."""
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
CORPUS=ROOT/'tests/fixtures/native-math-vectors.json'
NAMES=('abs','floor','ceil','trunc','sin','cos','sqrt','log','exp','pow','min','max')


def ordered(bits):return (~bits&0xffffffff) if bits>>31 else bits^0x80000000


def assess(corpus,lines):
    report={name:{'cases':0,'faults':0,'max_ulp':0,'max_absolute_error':0.0} for name in NAMES}
    assert len(lines)==len(corpus['vectors']),(len(lines),len(corpus['vectors']))
    for case,line in zip(corpus['vectors'],lines):
        ok,word=line.split();ok=int(ok);word=int(word,16);name=case['op'][4:];row=report[name];row['cases']+=1
        if case.get('fault'):
            assert not ok and word==0x5a5aa5a5,(case,line);row['faults']+=1;continue
        expected=int(case['result'],16)
        assert ok and word&0x7f800000!=0x7f800000,(case,line)
        distance=abs(ordered(word)-ordered(expected))
        if not expected&0x7fffffff:assert word==expected,(case,line,'zero sign')
        assert distance<=corpus['max_ulp'][name],(case,line,distance)
        actual,reference=[struct.unpack('<f',struct.pack('<I',bits))[0] for bits in (word,expected)]
        row['max_ulp']=max(row['max_ulp'],distance)
        row['max_absolute_error']=max(row['max_absolute_error'],abs(actual-reference))
    return report


class NativeMathTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.corpus=json.loads(CORPUS.read_text())

    def test_shared_math_against_frozen_high_precision_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            driver=Path(directory)/'math-driver'
            command=shlex.split(os.environ.get('CC','cc'))+['-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-I',str(ROOT/'src'),
                str(ROOT/'src/native_math.c'),str(ROOT/'tests/native_math_driver.c'),'-lm','-o',str(driver)]
            command+=shlex.split(os.environ.get('LIGHTVM_NUMERIC_CFLAGS',''))
            subprocess.run(command,check=True,capture_output=True,text=True)
            data=''.join(f'{NAMES.index(v["op"][4:])} {v["args"][0]} {v["args"][1] if len(v["args"])>1 else "0"}\n' for v in self.corpus['vectors'])
            run=subprocess.run([str(driver)],input=data,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr);self.assertFalse(run.stderr)
            report=assess(self.corpus,run.stdout.splitlines())
            if os.environ.get('LIGHTVM_MATH_REPORT'):Path(os.environ['LIGHTVM_MATH_REPORT']).write_text(json.dumps(report,indent=2)+'\n')

    def test_scalar_runtime_matches_the_reference(self):
        driver=Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME',ROOT/'build/host/test_platform'))
        if not driver.exists():self.skipTest('native test port not built')
        data=''.join(f'{v["op"]} {v["args"][0]} {v["args"][1] if len(v["args"])>1 else "0"}\n' for v in self.corpus['vectors'])
        run=subprocess.run([str(driver),'--numeric-driver'],input=data,capture_output=True,text=True)
        self.assertEqual(run.returncode,0,run.stderr);self.assertFalse(run.stderr)
        assess(self.corpus,run.stdout.splitlines())

    def test_reviewed_domains_and_zero_signs_are_retained(self):
        reviewed=json.loads((ROOT/'tests/fixtures/numeric-vectors.json').read_text())['native']
        by_key={(v['op'],tuple(v['args'])):v for v in self.corpus['vectors']}
        for case in reviewed:
            actual=by_key[case['op'],tuple(case['args'])]
            self.assertEqual(bool(actual.get('fault')),bool(case.get('fault')))
            if not case.get('fault'):self.assertEqual(actual['result'],case['result'])


if __name__=='__main__':unittest.main()
