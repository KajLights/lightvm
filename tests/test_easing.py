"""Equation references and native f32 execution for normalized guest helpers."""
import math
import struct
import unittest

import test_bulk_numeric as bulk
from lightvm.compiler_core import CompileError
from lightvm import easing

CURVES = tuple(f'{family}_{direction}' for family in ('sine','circular','exponential')
               for direction in ('in','out','in_out'))


def reference(name, t):
    t = min(1.0, max(0.0, t))
    if t == 0.0 or t == 1.0: return t
    family, direction = name.split('_', 1)
    def inward(x):
        if family == 'sine': return 1.0-math.cos(math.pi*x/2.0)
        if family == 'circular': return 1.0-math.sqrt(1.0-x*x)
        return 2.0**(10.0*x-10.0)
    if direction == 'in': return inward(t)
    if direction == 'out': return 1.0-inward(1.0-t)
    if family == 'sine': return (1.0-math.cos(math.pi*t))/2.0
    return inward(2.0*t)/2.0 if t < 0.5 else 1.0-inward(2.0-2.0*t)/2.0


class EasingTests(unittest.TestCase):
    setUpClass = classmethod(bulk.BulkNumericTests.setUpClass.__func__)
    compile = bulk.BulkNumericTests.compile
    run_package = bulk.BulkNumericTests.run_package
    compare = bulk.BulkNumericTests.compare

    def test_native_endpoints_neighbors_and_equations(self):
        samples = [-2.0, -0.0, 0.0, 2.0**-149, 2.0**-24,
                   *[i/16.0 for i in range(1,16)],
                   0.5-2.0**-25, 0.5+2.0**-24, 1.0-2.0**-24, 1.0, 2.0]
        samples.sort()
        for name in CURVES:
            source = 'from lightvm.easing import '+name+'\n@init\ndef setup(ctx,runtime):\n'
            source += ''.join(f' ctx.v{i}={name}({t!r})\n' for i,t in enumerate(samples))
            source += '@render\ndef draw(ctx,runtime,pixels): pass\n'
            _, package, manifest = self.compile(source)
            self.assertEqual(manifest['requires'], '0.1.0')
            result = self.run_package(package)
            self.assertEqual(result['status'], 1)
            values = [struct.unpack('<f', struct.pack('<I', w))[0] for w in result['state']]
            with self.subTest(curve=name):
                self.assertEqual(values, sorted(values))
                for t,v,word in zip(samples,values,result['state']):
                    self.assertGreaterEqual(v,0.0); self.assertLessEqual(v,1.0)
                    self.assertAlmostEqual(v, reference(name,t), delta=2e-6)
                    self.assertAlmostEqual(getattr(easing,name)(t), reference(name,t), delta=1e-12)
                    if t <= 0.0: self.assertEqual(word,0)
                    if t >= 1.0: self.assertEqual(word,0x3f800000)

    def test_scalar_maps_workers_and_yields(self):
        for name in CURVES:
            with self.subTest(curve=name):
                self.compare(f'''from lightvm.easing import {name}
@init
def setup(ctx,runtime):ctx.last=0.0
@render
def draw(ctx,runtime,pixels):
 value=0.0
 for i in range(pixels.count):
  value={name}((i-1)/float(pixels.count-2))
  pixels[i]=rgb(int(value*255.0),0,0)
 ctx.last=value
''', counts=(3,33,129))

    def test_library_math_is_hygienic_and_fixed_conversion_is_explicit(self):
        source = '''from lightvm.easing import sine_in as ease
def math(t):return 0.0
def sin(t):return 0.0
def linear(t):return 0.0
@init
def setup(ctx,runtime):ctx.value=ease(VALUE)
@render
def draw(ctx,runtime,pixels):pass
'''
        source = bulk.PREFIX.replace('import math\n','') + source
        with self.assertRaises(CompileError): bulk.PortableCompiler(source.replace('VALUE','fixed(0.5)')).compile()
        package, _ = bulk.PortableCompiler(source.replace('VALUE','float(fixed(0.5))')).compile()
        value = struct.unpack('<f',struct.pack('<I',self.run_package(package)['state'][0]))[0]
        self.assertAlmostEqual(value,reference('sine_in',0.5),delta=1e-7)


if __name__ == '__main__': unittest.main()
