"""Keep the frozen parameter matrix in the native conformance gate."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import numeric_matrix


class NumericMatrixTests(unittest.TestCase):
    def test_parameter_grids_ordered_controls_and_damping_history(self):
        driver=Path(os.environ.get('LIGHTVM_NUMERIC_RUNTIME',ROOT/'build/host/test_platform'))
        if not driver.exists():self.skipTest('native test port not built')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)
            numeric_matrix.prepare(path)
            numeric_matrix.replay(driver,path,(1,33,129),path/'results.json')


if __name__=='__main__':unittest.main()
