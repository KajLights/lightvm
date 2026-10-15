"""Compiler, independent effect replay, verifier and runtime regression checks."""
import importlib.util
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from compiler import Compiler, CompileError, OP
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
BUILD=ROOT/'build/portable-bytecode'
sys.path.insert(0,str(ROOT))
import lightvm

class PocTests(unittest.TestCase):
    host=BUILD/'host'
    def run_bytes(self,raw,count=200,frames=1):
        with tempfile.NamedTemporaryFile(suffix='.lbc') as f:
            f.write(raw);f.flush()
            return subprocess.run([str(self.host),f.name,str(count),str(frames)],capture_output=True)
    def test_samples_retain_effects(self):
        for name in ('color','pattern','rainbow','chase','wipe','twinkle'):
            raw,_=Compiler((HERE/'samples'/f'{name}.py').read_text()).compile()
            for count in (1,17,175,200):
                for frames in (1,37,143):
                    with self.subTest(name=name,count=count,frames=frames):
                        # Only trusted checked-in original scripts are executed as an oracle.
                        program=runpy.run_path(str(ROOT/'samples/python'/f'{name}.py'))
                        pixels=lightvm.Pixels();pixels.bind(count)
                        for frame in range(frames):
                            pixels.begin();program['draw'](types.SimpleNamespace(pixels=pixels,time=(frame+1)/60,delta=1/60));pixels.commit()
                        expected=b''.join(c.to_bytes(3,'big') for c in pixels.committed)
                        result=self.run_bytes(raw,count,frames)
                        self.assertEqual(result.returncode,0,result.stderr)
                        if name in ('rainbow','twinkle'):
                            self.assertLessEqual(max(abs(a-b) for a,b in zip(expected,result.stdout)),1)
                        else:self.assertEqual(expected,result.stdout)
    def test_native_rainbow_matches(self):
        result=subprocess.run([str(self.host),str(BUILD/'rainbow.lbc'),'200','5000','bench'],capture_output=True,check=True)
        self.assertTrue(json.loads(result.stdout)['identical'])
    def test_reject_compiler_state_mutation_in_shade(self):
        source='from lightvm_poc import shade, rgb\nx = 0\n@shade\ndef pixel():\n global x\n x = 1\n return rgb(0,0,0)\n'
        with self.assertRaises(CompileError):Compiler(source).compile()
    def test_malformed_packages(self):
        raw=(BUILD/'rainbow.lbc').read_bytes();regs=struct.unpack_from('<I',raw,8)[0];code=64+regs*8
        cases=[raw[:i] for i in (0,3,32,63,len(raw)-1)]+[raw+b'!']
        for offset,value in [(0,0),(4,255),(8,255),(32,0),(64,0),(65,5),(code,255),(code+1,255)]:
            changed=bytearray(raw);changed[offset]=value;cases.append(changed)
        # Shader may not write state, inputs or constants; nor jump across entries.
        shader=struct.unpack_from('<I',raw,28)[0]
        for instr in (struct.pack('<BBBBI',OP['MOV'],0,0,0,0),struct.pack('<BBBBI',OP['JMP'],0,0,0,0)):
            changed=bytearray(raw);changed[code+shader*8:code+shader*8+8]=instr;cases.append(changed)
        for n,case in enumerate(cases):
            with self.subTest(n=n):self.assertEqual(self.run_bytes(case).returncode,3)
    def test_dynamic_bounds_and_budget(self):
        raw=(BUILD/'pattern.lbc').read_bytes();regs=struct.unpack_from('<I',raw,8)[0];code=64+regs*8
        changed=bytearray(raw)
        # Deliberately jump to self in shade. Valid encoding must yield, then fault.
        shader=struct.unpack_from('<I',raw,28)[0]
        changed[code+shader*8:code+shader*8+8]=struct.pack('<BBBBI',OP['JMP'],0,0,0,shader)
        self.assertEqual(self.run_bytes(changed).returncode,4)
        source='from lightvm_poc import shade, rgb\npalette=(rgb(1,2,3),)\n@shade\ndef pixel():\n return palette[count]\n'
        self.assertEqual(self.run_bytes(Compiler(source).compile()[0]).returncode,4)
    def test_fusion_preserves_output(self):
        source=(HERE/'samples/rainbow.py').read_text()
        fast=Compiler(source,True).compile()[0];plain=Compiler(source,False).compile()[0]
        self.assertLess(len(fast),len(plain))
        for frames in (1,37,600):
            self.assertEqual(self.run_bytes(fast,175,frames).stdout,self.run_bytes(plain,175,frames).stdout)
    def test_kernel_dynamic_pixel_bounds(self):
        source='from lightvm_poc import kernel, rgb\n@kernel\ndef draw():\n pixels[count] = rgb(1,2,3)\n'
        self.assertEqual(self.run_bytes(Compiler(source).compile()[0]).returncode,4)
    def test_division_zero(self):
        source='from lightvm_poc import shade, hsv\n@shade\ndef pixel():\n return hsv(1.0 / 0.0, 1.0, 1.0)\n'
        self.assertEqual(self.run_bytes(Compiler(source).compile()[0]).returncode,4)

if __name__=='__main__':
    if '--sanitize' in sys.argv:PocTests.host=BUILD/'host-sanitized';sys.argv.remove('--sanitize')
    unittest.main()
