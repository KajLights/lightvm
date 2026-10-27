"""Work reservations include first evaluation, yields, faults and speculation."""
import json
import subprocess
import tempfile
import unittest

import test_bulk_numeric as bulk


class NumericWorkTests(unittest.TestCase):
    setUpClass = classmethod(bulk.BulkNumericTests.setUpClass.__func__)
    compile = bulk.BulkNumericTests.compile

    def package(self, expression, optimize=True):
        return self.compile('@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n  pixels[i]='+expression+'\n', optimize)[1]

    def run_work(self, package, count=129, workers=1, tick=0, budget=10000000):
        with tempfile.NamedTemporaryFile() as file:
            file.write(package); file.flush()
            run = subprocess.run([str(self.driver),'--numeric-package',file.name,str(count),str(tick),str(workers),str(budget)], capture_output=True,text=True,check=True)
        self.assertFalse(run.stderr)
        result=json.loads(run.stdout)
        self.assertLessEqual(result['work'],budget)
        return result

    def test_weighted_body_is_charged_even_with_few_dispatches(self):
        package=self.package('rgb(i%255,0,0)')
        normal=self.run_work(package)
        # MOD=8, RGB=1, STORE=1 per pixel; two MOVs, three chunks,
        # three instructions preserving the last Python loop value, END.
        self.assertEqual(normal['work'],129*10+2+3+3+1)
        self.assertLess(normal['dispatches'],13)
        yielded=self.run_work(package,tick=1000)
        self.assertEqual(yielded['work'],129*10+2+129+3+1)
        speculative=self.run_work(package,workers=2,tick=1000)
        self.assertGreater(speculative['work'],yielded['work'])
        self.assertEqual(speculative['pixels'],normal['pixels'])
        for workers in (1,2):
            limited=self.run_work(package,workers=workers,budget=100)
            self.assertEqual(limited['status'],-1)
            self.assertEqual(limited['work'],94)

    def test_invariant_first_pixel_and_fault_attempts_are_reserved(self):
        package=self.package('rgb(int(math.exp(0.5)),0,0)')
        normal=self.run_work(package)
        # EXP=128 then FTOI, RGB, STORE. Later pixels use three cached reads.
        self.assertEqual(normal['work'],3*131+126*4+2+3+3+1)
        yielded=self.run_work(package,tick=1000)
        self.assertEqual(yielded['work'],129*131+2+129+3+1)
        self.assertEqual(yielded['pixels'],normal['pixels'])
        self.assertEqual(self.run_work(package,budget=130)['status'],-1)
        fault=self.run_work(self.package('rgb(int(math.exp(1000.0)),0,0)'))
        self.assertEqual(fault['status'],-1)
        self.assertEqual(fault['work'],2+1+131)
        self.assertEqual(fault['pixels'],'00'*(129*4))

    def test_expensive_bodies_use_ordered_code_with_an_explicit_reason(self):
        for calls, expected_map in ((7,True),(8,False)):
            source='@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n'
            source+='  x=math.exp(0.5)\n'*calls+'  pixels[i]=rgb(int(x),0,0)\n'
            compiler,package,_=self.compile(source)
            self.assertEqual(any(i[0]==39 for i in compiler.code),expected_map)
            if not expected_map:self.assertIn('first-pixel work budget',compiler.map_reports[0][2])
            self.assertEqual(self.run_work(package)['status'],1)

    def test_native_binding_reserves_its_full_cost_before_execution(self):
        _,package,_=self.compile('@init\ndef setup(ctx,runtime):ctx.value=0.0\n'
                                 '@render\ndef draw(ctx,runtime,pixels):ctx.value=math.sin(0.5)\n',False)
        # Existing sine binding: 64, state assignment: 1, END: 1.
        rejected=self.run_work(package,budget=63)
        self.assertEqual((rejected['status'],rejected['work'],rejected['state']),(-1,0,[0]))
        for budget in (64,65):
            limited=self.run_work(package,budget=budget)
            self.assertEqual((limited['status'],limited['work']),(-1,budget))
        accepted=self.run_work(package,budget=66)
        self.assertEqual((accepted['status'],accepted['work']),(1,66))

    def test_first_pixel_budget_boundary_and_mutually_exclusive_paths(self):
        # 7*(EXP+MOV) + 12*(FDIV+MOV) + 5*(FADD+MOV) + FTOI+RGB+STORE = 1024.
        body='  x=math.exp(0.5)\n'*7+'  x=x/1.0\n'*12+'  x=x+0.0\n'*5
        prefix='@render\ndef draw(ctx,runtime,pixels):\n for i in range(pixels.count):\n'
        for extra,admitted in (('',True),('  x=x\n',False)):
            compiler,package,_=self.compile(prefix+body+extra+'  pixels[i]=rgb(int(x),0,0)\n')
            self.assertEqual(any(i[0]==39 for i in compiler.code),admitted)
            self.assertEqual(self.run_work(package,count=3)['status'],1)
        # Both arms are costly, but only one can execute for a pixel.
        body='  x=0.0\n  if i%2==0:\n'+'   x=math.exp(0.5)\n'*7
        body+='  else:\n'+'   x=math.exp(0.25)\n'*7
        compiler,package,_=self.compile(prefix+body+'  pixels[i]=rgb(int(x),0,0)\n')
        self.assertTrue(any(i[0]==39 for i in compiler.code))
        self.assertEqual(self.run_work(package,count=3)['status'],1)


if __name__=='__main__':unittest.main()
