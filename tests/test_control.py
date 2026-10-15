"""Compiler subset and existing program parameter/lifecycle API contracts."""
import os
import json
import struct
import sys
import time
import unittest
from bytecode_fixtures import ROOT, color, sample, program
sys.path.insert(0,str(ROOT / 'tools'))
from client import Client, ProcessTransport, ProtocolError, FRAME, PLAY, RESET, STOP
from lightvm.compiler import compile_source
from test_configuration import config

class CompilerTests(unittest.TestCase):
    def test_target_admission_and_portable_bytes(self):
        source=(ROOT/'samples/python/rainbow.py').read_text()
        result=compile_source(source)
        self.assertTrue(result.success)
        self.assertEqual(result.bytecode[:8],b'LBC1\x02\x00\x00\x00')
        target={'module_limit':32768,'control_abi':2,'program_schemas':['lightvm.program/3'],'bytecode_format':'LBC1/2'}
        self.assertEqual(result.bytecode,compile_source(source,target=target).bytecode)
        for key,value in (('module_limit',100),('control_abi',1),('program_schemas',[]),('bytecode_format','WASM')):
            self.assertFalse(compile_source(source,target={**target,key:value}).success)

    def test_diagnostics_and_restricted_source_never_execute(self):
        for source in ('import os','from os import system','open("/tmp/never-create-this", "w")',
                       'class Effect: pass', 'from lightvm import *',
                       'from lightvm import kernel\n@kernel\ndef draw():\n while True: pass',
                       'from lightvm import kernel\n@kernel\ndef draw():\n return 1',
                       'from lightvm import kernel\nx=1e400\n@kernel\ndef draw(): pass',
                       'from lightvm import kernel\nx=2147483648\n@kernel\ndef draw(): pass'):
            with self.subTest(source=source):
                result=compile_source(source,'bad.py')
                self.assertFalse(result.success)
                self.assertEqual(result.diagnostics[0].filename,'bad.py')
                self.assertGreater(result.diagnostics[0].line,0)

    def test_parameter_metadata_bounds_and_limits(self):
        result=compile_source((ROOT/'samples/python/rainbow.py').read_text())
        p=result.manifest['parameters'][0]
        self.assertEqual((p['name'],p['type'],p['minimum'],p['maximum']),('speed','float',-10.0,10.0))
        for decl in ('x=parameter(1, minimum=2)', 'x=parameter(True, minimum=0)',
                     'x=parameter(1.0, maximum=0.0)', 'x=parameter(1e100)',
                     '\n'.join(f'p{i}=parameter({i})' for i in range(9))):
            self.assertFalse(compile_source('from lightvm import parameter,kernel\n'+decl+'\n@kernel\ndef draw(): pass').success)

    def test_browser_default_source_compiles_with_the_shipped_compiler(self):
        line=next(line for line in (ROOT/'web/src/types.ts').read_text().splitlines() if line.startswith('export const DEFAULT_SOURCE = '))
        source=json.loads(line.split(' = ',1)[1].removesuffix(';'))
        self.assertTrue(compile_source(source).success)
        self.assertEqual(compile_source(source).bytecode,sample('rainbow'))

    def test_all_six_samples_compile_deterministically(self):
        for name in ('color','pattern','rainbow','chase','wipe','twinkle'):
            self.assertEqual(sample(name),sample(name))
            self.assertLess(len(sample(name)),3000)

class ControlTests(unittest.TestCase):
    def setUp(self):
        self.transport=ProcessTransport(os.environ.get('LIGHTVM_RECEIVER',str(ROOT/'build/host/lightvm')))
        self.client=Client(self.transport)
        self.client.configure(config())

    def tearDown(self): self.transport.close()

    def step(self,ms=0):
        self.client.require_ok(self.client.command(FRAME,struct.pack('<I',ms)))
        return bytes(self.client.pixel_snapshot)

    def test_parameters_generation_revision_bounds_and_atomic_update(self):
        self.client.upload(sample('rainbow'))
        self.step()
        original=self.client.program_parameters()
        self.client.set_parameters({'speed':2.0,'scale':3.0})
        current=self.client.program_parameters()
        self.assertEqual(current['values'],{'speed':2.0,'scale':3.0})
        self.assertEqual(current['parameter_revision'],original['parameter_revision']+1)
        for values in ({'speed':11.0},{'unknown':1},{'speed':1.0,'scale':-1.0},{'speed':1e100}):
            with self.assertRaises(ProtocolError):self.client.set_parameters(values)
            self.assertEqual(self.client.program_parameters()['values'],current['values'])
        with self.assertRaises(ProtocolError):
            self.client.manage('program_parameters_set',expected_generation=original['generation'],expected_parameter_revision=original['parameter_revision'],values={'speed':1.0})
        self.client.upload(sample('rainbow'))
        with self.assertRaises(ProtocolError):
            self.client.manage('program_pause',expected_generation=original['generation'])

    def test_pause_resume_clock_snapshot_and_stop(self):
        self.client.upload(sample('color'))
        self.step(17)
        state,pixels=self.client.snapshot()
        self.assertEqual(pixels,bytes([255,64,0])*5)
        self.assertEqual(self.client.program_parameters()['program_us'],17000)
        self.client.set_parameters({'color':0x112233})
        self.assertEqual(self.step(0),bytes.fromhex('112233')*5)
        self.client.command(PLAY);time.sleep(.04)
        self.client.pause_program()
        paused=self.client.program_parameters()['program_us'];time.sleep(.03)
        self.assertEqual(self.client.program_parameters()['program_us'],paused)
        self.client.resume_program();time.sleep(.04)
        self.assertGreater(self.client.program_parameters()['program_us'],paused)
        self.client.command(STOP)
        self.assertEqual(self.client.snapshot()[1],bytes(15))
        self.client.command(RESET)
        self.assertEqual(self.client.status_fields()['heap'],'0')

    def test_signed_integer_arithmetic_and_state(self):
        self.client.upload(program('from lightvm import rgb,step,kernel\nx=0\n@step\ndef update():\n global x\n x=x+1\n@kernel\ndef draw():\n pixels[0]=rgb(-7//3+3,-7%3,7%-3+3)\n pixels[1]=rgb(x,0,0)\n'))
        frame=self.step();self.assertEqual(frame[:6],bytes([0,2,1,1,0,0]))
        self.assertEqual(self.step()[3],2)

    def test_partial_fault_does_not_publish_partial_pixels(self):
        self.client.upload(program('from lightvm import parameter,rgb,kernel\nbad=parameter(False)\n@kernel\ndef draw():\n pixels.fill(rgb(7,8,9))\n if bad:\n  pixels[count]=rgb(1,2,3)\n'))
        before=self.step()
        self.client.set_parameters({'bad':True})
        self.assertEqual(self.client.command(FRAME,bytes(4)).code,4)
        state,pixels=self.client.snapshot()
        self.assertEqual(pixels,before)
        self.assertTrue(state['state']['faulted'])
